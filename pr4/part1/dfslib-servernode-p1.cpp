#include <map>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>
#include <exception>
#include <google/protobuf/util/time_util.h>

#include "src/dfs-utils.h"
#include "dfslib-shared-p1.h"
#include "dfslib-servernode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Server;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::ServerBuilder;

using namespace std;

using dfs_service::DFSService;
using dfs_service::FileName;
using dfs_service::FileRequest;
using dfs_service::FileContent;
using dfs_service::HelloReply;
using dfs_service::HelloRequest;
using dfs_service::Empty;
using dfs_service::FilesList;
using dfs_service::FileStat;
using dfs_service::FileStatus;

using google::protobuf::util::TimeUtil;
using google::protobuf::Timestamp;

int BUFSIZE_SERVER = 1024;

//
// STUDENT INSTRUCTION:
//
// DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in the `dfs-service.proto` file.
//
// You should add your definition overrides here for the specific
// methods that you created for your GRPC service protocol. The
// gRPC tutorial described in the readme is a good place to get started
// when trying to understand how to implement this class.
//
// The method signatures generated can be found in `proto-src/dfs-service.grpc.pb.h` file.
//
// Look for the following section:
//
//      class Service : public ::grpc::Service {
//
// The methods returning grpc::Status are the methods you'll want to override.
//
// In C++, you'll want to use the `override` directive as well. For example,
// if you have a service method named MyMethod that takes a MyMessageType
// and a ServerWriter, you'll want to override it similar to the following:
//
//      Status MyMethod(ServerContext* context,
//                      const MyMessageType* request,
//                      ServerWriter<MySegmentType> *writer) override {
//
//          /** code implementation here **/
//      }
//
class DFSServiceImpl final : public DFSService::Service {

private:

    /** The mount path for the server **/
    std::string mount_path;

    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }


public:

    DFSServiceImpl(const std::string &mount_path): mount_path(mount_path) {
    }

    ~DFSServiceImpl() {}

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // implementations of your protocol service methods
    //

    Status SayHello(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
        std::string prefix("Hello ");
        reply->set_message(prefix + request->name());
        return Status::OK;
    }

    Status StoreFile(ServerContext* context, ServerReader<FileContent>* file_request, FileName* response) override {
        const multimap<grpc::string_ref, grpc::string_ref>& file_metadata = context->client_metadata();
        auto file_name_check = file_metadata.find("file_name");

        if(file_name_check != file_metadata.end()) {
            string file_name((file_name_check->second).data(), (file_name_check->second).length());
            dfs_log(LL_SYSINFO) << "File name: " << file_name;

            const string& file_path = WrapPath(file_name);

            FileContent file_content;
            ofstream fd;
            fd.open(file_path);

            try {
                while(file_request->Read(&file_content)) {
                    if(context->IsCancelled()) {
                        stringstream deadline_err; 
                        deadline_err << "Past the deadline time." << endl;
                        dfs_log(LL_ERROR) << deadline_err.str();
                        return Status(StatusCode::DEADLINE_EXCEEDED, deadline_err.str());
                    }

                    fd << file_content.data();
                }

                fd.close();
            } catch (exception& e) {
                stringstream exception_err;
                exception_err << e.what() << endl;
                dfs_log(LL_ERROR) << "Store file expection: " << exception_err.str();
                return Status(StatusCode::CANCELLED, exception_err.str());
            }

            struct stat file_stats;
            int filestatval = stat(file_path.c_str(), &file_stats);
            if(filestatval != 0) {
                stringstream not_found_err;
                not_found_err << "File path: " << file_path << " was not found." << endl;
                dfs_log(LL_ERROR) << not_found_err.str();
                return Status(StatusCode::NOT_FOUND, not_found_err.str());
            }

            response->set_filename(file_name);
            return Status::OK;
        } else {
            stringstream file_name_err;
            file_name_err << "Couldn't get file name." << endl;
            dfs_log(LL_ERROR) << file_name_err.str();
            return Status(StatusCode::CANCELLED, file_name_err.str());
        }
    }

    Status FetchFile(ServerContext* context, const FileRequest* request, ServerWriter<FileContent>* response) override {
        const string& file_path = WrapPath(request->filename());

        struct stat file_stats;
        int filestatval = stat(file_path.c_str(), &file_stats);
        if(filestatval != 0) {
            stringstream not_found_err;
            not_found_err << "File path: " << file_path << " was not found." << endl;
            dfs_log(LL_ERROR) << not_found_err.str();
            return Status(StatusCode::NOT_FOUND, not_found_err.str());
        }

        int file_size = file_stats.st_size;
        FileContent file_content;
        ifstream fd(file_path);
        char buf[BUFSIZE_SERVER];
        int bytes_sent = 0;

        try {
            while(bytes_sent < file_size) {
                if(context->IsCancelled()) {
                    stringstream deadline_err; 
                    deadline_err << "Past the deadline time." << endl;
                    dfs_log(LL_ERROR) << deadline_err.str();
                    return Status(StatusCode::DEADLINE_EXCEEDED, deadline_err.str());
                }
                memset(buf, 0, sizeof buf);
                int bytes_to_send = 0;
                if(BUFSIZE_SERVER < file_size - bytes_sent) {
                    bytes_to_send = BUFSIZE_SERVER;
                } else {
                    bytes_to_send = file_size - bytes_sent;
                }

                fd.read(buf, bytes_to_send);
                file_content.set_size(bytes_to_send);
                file_content.set_data(buf, bytes_to_send);
                response->Write(file_content);

                bytes_sent += bytes_to_send;
                // dfs_log(LL_SYSINFO) << "Bytes sent: " << bytes_sent;
            }

            fd.close();
        } catch (exception& e) {
            stringstream file_err;
            file_err << "Couldn't send the file." << endl;
            dfs_log(LL_ERROR) << file_err.str();
            return Status(StatusCode::CANCELLED, file_err.str());
        }

        return Status::OK;
    }

    Status DeleteFile(ServerContext* context, const FileRequest* request, Empty* response) override {
        const string& file_path = WrapPath(request->filename());

        struct stat file_stats;
        int filestatval = stat(file_path.c_str(), &file_stats);
        if(filestatval != 0) {
            stringstream not_found_err;
            not_found_err << "File path: " << file_path << " was not found." << endl;
            dfs_log(LL_ERROR) << not_found_err.str();
            return Status(StatusCode::NOT_FOUND, not_found_err.str());
        }

        if(context->IsCancelled()) {
            stringstream deadline_err; 
            deadline_err << "Past the deadline time." << endl;
            dfs_log(LL_ERROR) << deadline_err.str();
            return Status(StatusCode::DEADLINE_EXCEEDED, deadline_err.str());
        }

        if(remove(file_path.c_str()) != 0) {
            stringstream delete_err;
            delete_err << "Error in deleting file: " << file_path << endl;
            dfs_log(LL_ERROR) << delete_err.str();
            return Status(StatusCode::CANCELLED, delete_err.str());
        }

        return Status::OK;
    }

    Status ListFiles(ServerContext* context, const Empty* request, FilesList* response) override {
        DIR *directory;
        struct dirent *ent;

        directory = opendir(mount_path.c_str());
        if(directory != NULL) {
            while((ent = readdir(directory)) != NULL) {
                if(context->IsCancelled()) {
                    stringstream deadline_err; 
                    deadline_err << "Past the deadline time." << endl;
                    dfs_log(LL_ERROR) << deadline_err.str();
                    return Status(StatusCode::DEADLINE_EXCEEDED, deadline_err.str());
                }

                const string& file_path = WrapPath(ent->d_name);

                struct stat file_stats;
                int filestatval = stat(file_path.c_str(), &file_stats);
                if(filestatval != 0) {
                    stringstream not_found_err;
                    not_found_err << "File path: " << file_path << " was not found." << endl;
                    dfs_log(LL_ERROR) << not_found_err.str();
                    return Status(StatusCode::NOT_FOUND, not_found_err.str());
                }

                if(!(file_stats.st_mode & S_IFREG)) {
                    dfs_log(LL_SYSINFO) << "not a file.";
                    continue;
                }

                FileStat* fs = response->add_file();
                fs->set_filename(ent->d_name);
                Timestamp* modified = new Timestamp(TimeUtil::TimeTToTimestamp(file_stats.st_mtime));
                fs->set_allocated_modified(modified);
            }
            closedir(directory);
        }

        return Status::OK;
    }

    Status GetFileStatus(ServerContext* context, const FileRequest* request, FileStatus* response) override {
        const string& file_path = WrapPath(request->filename());

        response->set_filename(request->filename());

        struct stat file_stats;
        int filestatval = stat(file_path.c_str(), &file_stats);
        if(filestatval != 0) {
            stringstream not_found_err;
            not_found_err << "File path: " << file_path << " was not found." << endl;
            dfs_log(LL_ERROR) << not_found_err.str();
            return Status(StatusCode::NOT_FOUND, not_found_err.str());
        }

        int file_size = file_stats.st_size;
        response->set_size(file_size);

        Timestamp* modified = new Timestamp(TimeUtil::TimeTToTimestamp(file_stats.st_mtime));
        Timestamp* created = new Timestamp(TimeUtil::TimeTToTimestamp(file_stats.st_ctime));

        response->set_allocated_modified(modified);
        response->set_allocated_created(created);

        return Status::OK;
    }
};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly,
// but be aware that the testing environment is expecting these three
// methods as-is.
//
/**
 * The main server node constructor
 *
 * @param server_address
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        std::function<void()> callback) :
    server_address(server_address), mount_path(mount_path), grader_callback(callback) {}

/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
    this->server->Shutdown();
}

/** Server start **/
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path);
    ServerBuilder builder;
    builder.AddListeningPort(this->server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    this->server = builder.BuildAndStart();
    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    this->server->Wait();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional DFSServerNode definitions here
//

