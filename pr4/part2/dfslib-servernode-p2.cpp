#include <map>
#include <mutex>
#include <shared_mutex>
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
#include <google/protobuf/util/time_util.h>
#include <shared_mutex>

#include "proto-src/dfs-service.grpc.pb.h"
#include "src/dfslibx-call-data.h"
#include "src/dfslibx-service-runner.h"
#include "dfslib-shared-p2.h"
#include "dfslib-servernode-p2.h"

using grpc::Status;
using grpc::Server;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::ServerBuilder;

using dfs_service::DFSService;

using namespace std;

using dfs_service::FileInfo;
using dfs_service::Result;
using dfs_service::DFSService;
using dfs_service::FileName;
using dfs_service::FileRequest;
using dfs_service::FileContent;
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
// Change these "using" aliases to the specific
// message types you are using in your `dfs-service.proto` file
// to indicate a file request and a listing of files from the server
//
using FileRequestType = FileInfo;
using FileListResponseType = FilesList;

extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// As with Part 1, the DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in your `dfs-service.proto` file.
//
// You may start with your Part 1 implementations of each service method.
//
// Elements to consider for Part 2:
//
// - How will you implement the write lock at the server level?
// - How will you keep track of which client has a write lock for a file?
//      - Note that we've provided a preset client_id in DFSClientNode that generates
//        a client id for you. You can pass that to the server to identify the current client.
// - How will you release the write lock?
// - How will you handle a store request for a client that doesn't have a write lock?
// - When matching files to determine similarity, you should use the `file_checksum` method we've provided.
//      - Both the client and server have a pre-made `crc_table` variable to speed things up.
//      - Use the `file_checksum` method to compare two files, similar to the following:
//
//          std::uint32_t server_crc = dfs_file_checksum(filepath, &this->crc_table);
//
//      - Hint: as the crc checksum is a simple integer, you can pass it around inside your message types.
//
class DFSServiceImpl final :
    public DFSService::WithAsyncMethod_CallbackList<DFSService::Service>,
        public DFSCallDataManager<FileRequestType , FileListResponseType> {

private:

    /** The runner service used to start the service and manage asynchronicity **/
    DFSServiceRunner<FileRequestType, FileListResponseType> runner;

    /** The mount path for the server **/
    std::string mount_path;

    /** Mutex for managing the queue requests **/
    std::mutex queue_mutex;

    /** The vector of queued tags used to manage asynchronous requests **/
    std::vector<QueueRequest<FileRequestType, FileListResponseType>> queued_tags;


    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }

    /** CRC Table kept in memory for faster calculations **/
    CRC::Table<std::uint32_t, 32> crc_table;

    // write mutex
    shared_timed_mutex writeLock;
    shared_timed_mutex directory_lock;
    map<string, string> filenameMap;
    map<string, unique_ptr<shared_timed_mutex>> writelockMap;

    void EraseClientId(string file_name) {
        writeLock.lock();
        dfs_log(LL_SYSINFO) << "Erasing filename from client id map.";
        filenameMap.erase(file_name);
        writeLock.unlock();
    }

    bool DoesLockExist(string file_name, string client_id) {
        if(filenameMap.find(file_name) == filenameMap.end()) {
            dfs_log(LL_SYSINFO) << "doesn't find the file";
            filenameMap.insert({ file_name, client_id });
            if(writelockMap.find(file_name) == writelockMap.end()) {
                writelockMap.insert({ file_name, make_unique<shared_timed_mutex>() });
            }
            return true;
        } else if((filenameMap.find(file_name)->second).compare(client_id) != 0) {
            dfs_log(LL_SYSINFO) << "different client has the lock";
            return false;
        } else if ((filenameMap.find(file_name)->second).compare(client_id) == 0) {
            dfs_log(LL_SYSINFO) << "correct client has the lock";
            return true;
        }
        return false;
    }

public:

    DFSServiceImpl(const std::string& mount_path, const std::string& server_address, int num_async_threads):
        mount_path(mount_path), crc_table(CRC::CRC_32()) {

        this->runner.SetService(this);
        this->runner.SetAddress(server_address);
        this->runner.SetNumThreads(num_async_threads);
        this->runner.SetQueuedRequestsCallback([&]{ this->ProcessQueuedRequests(); });

    }

    ~DFSServiceImpl() {
        this->runner.Shutdown();
    }

    void Run() {
        this->runner.Run();
    }

    /**
     * Request callback for asynchronous requests
     *
     * This method is called by the DFSCallData class during
     * an asynchronous request call from the client.
     *
     * Students should not need to adjust this.
     *
     * @param context
     * @param request
     * @param response
     * @param cq
     * @param tag
     */
    void RequestCallback(grpc::ServerContext* context,
                         FileRequestType* request,
                         grpc::ServerAsyncResponseWriter<FileListResponseType>* response,
                         grpc::ServerCompletionQueue* cq,
                         void* tag) {

        std::lock_guard<std::mutex> lock(queue_mutex);
        this->queued_tags.emplace_back(context, request, response, cq, tag);

    }

    /**
     * Process a callback request
     *
     * This method is called by the DFSCallData class when
     * a requested callback can be processed. You should use this method
     * to manage the CallbackList RPC call and respond as needed.
     *
     * See the STUDENT INSTRUCTION for more details.
     *
     * @param context
     * @param request
     * @param response
     */
    void ProcessCallback(ServerContext* context, FileRequestType* request, FileListResponseType* response) {

        //
        // STUDENT INSTRUCTION:
        //
        // You should add your code here to respond to any CallbackList requests from a client.
        // This function is called each time an asynchronous request is made from the client.
        //
        // The client should receive a list of files or modifications that represent the changes this service
        // is aware of. The client will then need to make the appropriate calls based on those changes.
        //

        Status list_status = this->CallbackList(context, request, response);
        if(!list_status.ok()) {
            dfs_log(LL_ERROR) << "Errored out listing files";
        } else {
            dfs_log(LL_SYSINFO) << "Successfully got the list of files";
        }

    }

    /**
     * Processes the queued requests in the queue thread
     */
    void ProcessQueuedRequests() {
        while(true) {

            //
            // STUDENT INSTRUCTION:
            //
            // You should add any synchronization mechanisms you may need here in
            // addition to the queue management. For example, modified files checks.
            //
            // Note: you will need to leave the basic queue structure as-is, but you
            // may add any additional code you feel is necessary.
            //


            // Guarded section for queue
            {
                dfs_log(LL_DEBUG2) << "Waiting for queue guard";
                std::lock_guard<std::mutex> lock(queue_mutex);


                for(QueueRequest<FileRequestType, FileListResponseType>& queue_request : this->queued_tags) {
                    this->RequestCallbackList(queue_request.context, queue_request.request,
                        queue_request.response, queue_request.cq, queue_request.cq, queue_request.tag);
                    queue_request.finished = true;
                }

                // any finished tags first
                this->queued_tags.erase(std::remove_if(
                    this->queued_tags.begin(),
                    this->queued_tags.end(),
                    [](QueueRequest<FileRequestType, FileListResponseType>& queue_request) { return queue_request.finished; }
                ), this->queued_tags.end());

            }
        }
    }

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // the implementations of your rpc protocol methods.
    //

    Status GetWriteLock(ServerContext* context, const FileInfo* file_req, Result* res) override {
        const multimap<grpc::string_ref, grpc::string_ref>& file_metadata = context->client_metadata();
        auto client_id_check = file_metadata.find("client_id");

        if(client_id_check != file_metadata.end()) {
            string client_id((client_id_check->second).data(), (client_id_check->second).length());
            dfs_log(LL_SYSINFO) << "Client id: " << client_id;

            writeLock.lock();
            
            auto clientidFromMap = filenameMap.find(file_req->name());
            dfs_log(LL_SYSINFO) << "Successfully locks to look for locks";
            if(clientidFromMap != filenameMap.end()) {
                string strClientId = clientidFromMap->second;
                if(strClientId.compare(client_id) != 0) {
                    writeLock.unlock();
                    stringstream no_lock_err;
                    no_lock_err << "File already has a different write lock on it.";
                    dfs_log(LL_SYSINFO) << "file already has a different write lock on it";
                    return Status(StatusCode::RESOURCE_EXHAUSTED, no_lock_err.str());
                } else {
                    writeLock.unlock();
                    dfs_log(LL_SYSINFO) << "client already has the file lock";
                    return Status::OK;
                }
            }

            dfs_log(LL_SYSINFO) << "Doesn't meet any of the conditionals";
            filenameMap.insert({ file_req->name(), client_id });
            if(writelockMap.find(file_req->name()) == writelockMap.end()) {
                dfs_log(LL_SYSINFO) << "Adding a lock for file";
                writelockMap.insert({ file_req->name(), make_unique<shared_timed_mutex>() });
            }
            writeLock.unlock();
            dfs_log(LL_SYSINFO) << "client got the lock";
            return Status::OK;
        } else {
            stringstream file_name_err;
            file_name_err << "Couldn't get file name." << endl;
            dfs_log(LL_ERROR) << file_name_err.str();
            return Status(StatusCode::CANCELLED, file_name_err.str());
        }

        stringstream eof_err;
        eof_err << "Error with function" << endl;
        return Status(StatusCode::CANCELLED, eof_err.str());
    }

    Status StoreFile(ServerContext* context, ServerReader<FileContent>* file_request, FileName* response) override {
        const multimap<grpc::string_ref, grpc::string_ref>& file_metadata = context->client_metadata();
        auto file_name_check = file_metadata.find("file_name");
        auto client_id_check = file_metadata.find("client_id");

        if(file_name_check != file_metadata.end()) {
            string file_name((file_name_check->second).data(), (file_name_check->second).length());
            dfs_log(LL_SYSINFO) << "File name: " << file_name;

            const string& file_path = WrapPath(file_name);
            if(client_id_check != file_metadata.end()) {
                string client_id((client_id_check->second).data(), (client_id_check->second).length());
                // case where no lock or other client was found for the clientid
                writeLock.lock_shared();
                dfs_log(LL_SYSINFO) << "Checking if lock exists";
                if(!DoesLockExist(file_name, client_id)) {
                    dfs_log(LL_ERROR) << "No lock found";
                    stringstream no_lock_err;
                    no_lock_err << "No lock was found for file: " << file_name << endl;
                    writeLock.unlock_shared();
                    return Status(StatusCode::RESOURCE_EXHAUSTED, no_lock_err.str());
                }
                writeLock.unlock_shared();

                uint32_t server_file_checksum = dfs_file_checksum(file_path, &this->crc_table);

                shared_timed_mutex* file_write_lock = writelockMap.find(file_name)->second.get();
                directory_lock.lock();
                file_write_lock->lock();
                FileContent file_content;
                ofstream fd;
                // fd.open(file_path);

                try {
                    while(file_request->Read(&file_content)) {
                        if(context->IsCancelled()) {
                            fd.close();
                            directory_lock.unlock();
                            file_write_lock->unlock();
                            EraseClientId(file_name);
                            stringstream deadline_err; 
                            deadline_err << "Past the deadline time." << endl;
                            dfs_log(LL_ERROR) << deadline_err.str();
                            return Status(StatusCode::DEADLINE_EXCEEDED, deadline_err.str());
                        }
                        if(server_file_checksum == file_content.size()) {
                            fd.close();
                            directory_lock.unlock();
                            file_write_lock->unlock();
                            EraseClientId(file_name);
                            stringstream checksum_err; 
                            checksum_err << "Checksum was the same as the file in the server." << endl;
                            dfs_log(LL_ERROR) << checksum_err.str();
                            return Status(StatusCode::ALREADY_EXISTS, checksum_err.str());
                        }
                        if(!fd.is_open()) {
                            fd.open(file_path);
                        }
                        fd << file_content.data();
                    }
                    EraseClientId(file_name);
                    fd.close();
                } catch (exception& e) {
                    stringstream exception_err;
                    exception_err << e.what() << endl;
                    EraseClientId(file_name);
                    fd.close();
                    directory_lock.unlock();
                    file_write_lock->unlock();
                    dfs_log(LL_ERROR) << "Store file expection: " << exception_err.str();
                    return Status(StatusCode::CANCELLED, exception_err.str());
                }

                struct stat file_stats;
                int filestatval = stat(file_path.c_str(), &file_stats);
                if(filestatval != 0) {
                    stringstream not_found_err;
                    not_found_err << "File path: " << file_path << " was not found." << endl;
                    dfs_log(LL_ERROR) << not_found_err.str();
                    EraseClientId(file_name);
                    directory_lock.unlock();
                    file_write_lock->unlock();
                    return Status(StatusCode::NOT_FOUND, not_found_err.str());
                }
                directory_lock.unlock();
                file_write_lock->unlock();

                response->set_filename(file_name);
                return Status::OK;

            }
        } else {
            stringstream file_name_err;
            file_name_err << "Couldn't get file name." << endl;
            dfs_log(LL_ERROR) << file_name_err.str();
            return Status(StatusCode::CANCELLED, file_name_err.str());
        }

        stringstream eof_err;
        eof_err << "Reached the end of the function without doing anything error." << endl;
        dfs_log(LL_ERROR) << eof_err.str();
        return Status(StatusCode::CANCELLED, eof_err.str());
    }

    Status FetchFile(ServerContext* context, const FileRequest* request, ServerWriter<FileContent>* response) override {
        const multimap<grpc::string_ref, grpc::string_ref>& file_metadata = context->client_metadata();
        auto client_id_check = file_metadata.find("client_id");

        if(client_id_check != file_metadata.end()) {
            const string& file_path = WrapPath(request->filename());
            string client_id((client_id_check->second).data(), (client_id_check->second).length());
            // case where no lock or other client was found for the clientid
            writeLock.lock_shared();
            dfs_log(LL_SYSINFO) << "Checking if lock exists";
            if(!DoesLockExist(request->filename(), client_id)) {
                dfs_log(LL_ERROR) << "No lock found";
                stringstream no_lock_err;
                no_lock_err << "No lock was found for file: " << request->filename() << endl;
                writeLock.unlock_shared();
                return Status(StatusCode::CANCELLED, no_lock_err.str());
            }
            writeLock.unlock_shared();

            uint32_t server_file_checksum = dfs_file_checksum(file_path, &this->crc_table);

            dfs_log(LL_SYSINFO) << "server checksum: " << server_file_checksum;
            if(server_file_checksum == request->checksum()) {
                EraseClientId(request->filename());
                stringstream checksum_err; 
                checksum_err << "Checksum was the same as the file in the server for file: " << request->filename();
                dfs_log(LL_ERROR) << checksum_err.str();
                return Status(StatusCode::ALREADY_EXISTS, checksum_err.str());
            }

            shared_timed_mutex* file_write_lock = writelockMap.find(request->filename())->second.get();

            directory_lock.lock();
            file_write_lock->lock();

            struct stat file_stats;
            int filestatval = stat(file_path.c_str(), &file_stats);
            if(filestatval != 0) {
                EraseClientId(request->filename());
                directory_lock.unlock();
                file_write_lock->unlock();
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
                        EraseClientId(request->filename());
                        directory_lock.unlock();
                        file_write_lock->unlock();
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
                EraseClientId(request->filename());
                directory_lock.unlock();
                file_write_lock->unlock();
                fd.close();
            } catch (exception& e) {
                EraseClientId(request->filename());
                directory_lock.unlock();
                file_write_lock->unlock();
                stringstream file_err;
                file_err << "Couldn't send the file." << endl;
                dfs_log(LL_ERROR) << file_err.str();
                return Status(StatusCode::CANCELLED, file_err.str());
            }

            return Status::OK;
        } else {
            stringstream client_id_err;
            client_id_err << "Couldn't get client id." << endl;
            dfs_log(LL_ERROR) << client_id_err.str();
            return Status(StatusCode::CANCELLED, client_id_err.str());
        }
        stringstream eof_err;
        eof_err << "Reached the end of the function without doing anything error." << endl;
        dfs_log(LL_ERROR) << eof_err.str();
        return Status(StatusCode::CANCELLED, eof_err.str());
    }

    Status DeleteFile(ServerContext* context, const FileRequest* request, Empty* response) override {
        const multimap<grpc::string_ref, grpc::string_ref>& file_metadata = context->client_metadata();
        auto client_id_check = file_metadata.find("client_id");

        if(client_id_check != file_metadata.end()) {
            const string& file_path = WrapPath(request->filename());
            string client_id((client_id_check->second).data(), (client_id_check->second).length());
            // case where no lock or other client was found for the clientid
            writeLock.lock_shared();
            dfs_log(LL_SYSINFO) << "Checking if lock exists";
            if(!DoesLockExist(request->filename(), client_id)) {
                dfs_log(LL_ERROR) << "No lock found";
                stringstream no_lock_err;
                no_lock_err << "No lock was found for file: " << request->filename() << endl;
                writeLock.unlock_shared();
                return Status(StatusCode::CANCELLED, no_lock_err.str());
            }
            writeLock.unlock_shared();
            shared_timed_mutex* file_write_lock = writelockMap.find(request->filename())->second.get();

            file_write_lock->lock();
            directory_lock.lock();

            struct stat file_stats;
            int filestatval = stat(file_path.c_str(), &file_stats);
            if(filestatval != 0) {
                EraseClientId(request->filename());
                directory_lock.unlock();
                file_write_lock->unlock();
                stringstream not_found_err;
                not_found_err << "File path: " << file_path << " was not found." << endl;
                dfs_log(LL_ERROR) << not_found_err.str();
                return Status(StatusCode::NOT_FOUND, not_found_err.str());
            }

            if(context->IsCancelled()) {
                EraseClientId(request->filename());
                directory_lock.unlock();
                file_write_lock->unlock();
                stringstream deadline_err; 
                deadline_err << "Past the deadline time." << endl;
                dfs_log(LL_ERROR) << deadline_err.str();
                return Status(StatusCode::DEADLINE_EXCEEDED, deadline_err.str());
            }

            if(remove(file_path.c_str()) != 0) {
                EraseClientId(request->filename());
                directory_lock.unlock();
                file_write_lock->unlock();
                stringstream delete_err;
                delete_err << "Error in deleting file: " << file_path << endl;
                dfs_log(LL_ERROR) << delete_err.str();
                return Status(StatusCode::CANCELLED, delete_err.str());
            }
            EraseClientId(request->filename());
            directory_lock.unlock();
            file_write_lock->unlock();
            return Status::OK;
        } else {
            stringstream client_id_err;
            client_id_err << "Couldn't get client id." << endl;
            dfs_log(LL_ERROR) << client_id_err.str();
            return Status(StatusCode::CANCELLED, client_id_err.str());
        }
        stringstream eof_err;
        eof_err << "Reached the end of the function without doing anything error." << endl;
        dfs_log(LL_ERROR) << eof_err.str();
        return Status(StatusCode::CANCELLED, eof_err.str());
    }

    Status ListFiles(ServerContext* context, const Empty* request, FilesList* response) override {
        DIR *directory;
        struct dirent *ent;
        directory_lock.lock_shared();

        directory = opendir(mount_path.c_str());
        if(directory != NULL) {
            while((ent = readdir(directory)) != NULL) {
                const string& file_path = WrapPath(ent->d_name);

                struct stat file_stats;
                int filestatval = stat(file_path.c_str(), &file_stats);
                if(filestatval != 0) {
                    stringstream not_found_err;
                    not_found_err << "File path: " << file_path << " was not found." << endl;
                    dfs_log(LL_ERROR) << not_found_err.str();
                    directory_lock.unlock();
                    return Status(StatusCode::NOT_FOUND, not_found_err.str());
                }

                if(!(file_stats.st_mode & S_IFREG)) {
                    dfs_log(LL_SYSINFO) << "not a file.";
                    continue;
                }

                FileStatus* fs = response->add_file();
                fs->set_filename(ent->d_name);
                Timestamp* modified = new Timestamp(TimeUtil::TimeTToTimestamp(file_stats.st_mtime));
                fs->set_allocated_modified(modified);

                Timestamp* created = new Timestamp(TimeUtil::TimeTToTimestamp(file_stats.st_ctime));
                fs->set_allocated_created(created);

                int file_size = file_stats.st_size;
                fs->set_size(file_size);
            }
            closedir(directory);
        } else {
            closedir(directory);
            directory_lock.unlock_shared();
        }

        directory_lock.unlock_shared();
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

    Status CallbackList(ServerContext* context, const FileInfo* file_req, FilesList* response) override {
        Empty request;
        return this->ListFiles(context, &request, response);
    }
};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly
// to add additional startup/shutdown routines inside, but be aware that
// the basic structure should stay the same as the testing environment
// will be expected this structure.
//
/**
 * The main server node constructor
 *
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        int num_async_threads,
        std::function<void()> callback) :
        server_address(server_address),
        mount_path(mount_path),
        num_async_threads(num_async_threads),
        grader_callback(callback) {}
/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
}

/**
 * Start the DFSServerNode server
 */
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path, this->server_address, this->num_async_threads);


    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    service.Run();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional definitions here
//
