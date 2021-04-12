#include <regex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>
#include <exception>
#include <google/protobuf/util/time_util.h>


#include "dfslib-shared-p1.h"
#include "dfslib-clientnode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

using namespace std;
using dfs_service::FileRequest;
using dfs_service::FileName;
using dfs_service::FileContent;
using dfs_service::HelloRequest;
using dfs_service::HelloReply;
using dfs_service::Empty;
using dfs_service::FilesList;
using dfs_service::FileStat;
using dfs_service::FileStatus;

using google::protobuf::util::TimeUtil;

int BUFSIZE = 1024;

//
// STUDENT INSTRUCTION:
//
// You may want to add aliases to your namespaced service methods here.
// All of the methods will be under the `dfs_service` namespace.
//
// For example, if you have a method named MyMethod, add
// the following:
//
//      using dfs_service::MyMethod
//


DFSClientNodeP1::DFSClientNodeP1() : DFSClientNode() {}

DFSClientNodeP1::~DFSClientNodeP1() noexcept {}

StatusCode DFSClientNodeP1::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept and store a file.
    //
    // When working with files in gRPC you'll need to stream
    // the file contents, so consider the use of gRPC's ClientWriter.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the client
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext ctx;
    FileName file_resp;
    ctx.AddMetadata("file_name", filename);
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    const string& filepath = WrapPath(filename);

    struct stat file_stats;
    int filestatval = stat(filepath.c_str(), &file_stats);
    if(filestatval != 0) {
        dfs_log(LL_ERROR) << "File path: " << filepath << " was not found.";
        return StatusCode::NOT_FOUND;
    }

    int file_size = file_stats.st_size;

    unique_ptr<ClientWriter<FileContent>> file_data_stream = service_stub->StoreFile(&ctx, &file_resp);

    // read file
    ifstream fd(filepath);
    int bytes_sent = 0;
    char buf[BUFSIZE];
    FileContent file_content;


    try {
        while(bytes_sent < file_size) {
            memset(buf, 0, sizeof buf);
            int bytes_to_send = 0;
            if(BUFSIZE < file_size - bytes_sent) {
                bytes_to_send = BUFSIZE;
            } else {
                bytes_to_send = file_size - bytes_sent;
            }

            fd.read(buf, bytes_to_send);
            file_content.set_size(bytes_to_send);
            file_content.set_data(buf, bytes_to_send);
            file_data_stream->Write(file_content);

            bytes_sent += bytes_to_send;
        }

        fd.close();
    } catch (exception& e) {
        dfs_log(LL_ERROR) << "Error in storing file (client)): " << e.what();
        return StatusCode::CANCELLED;
    }

    file_data_stream->WritesDone();
    Status store_status = file_data_stream->Finish();

    if(store_status.ok()) {
        return StatusCode::OK;
    } else {
        return store_status.error_code();
    }

    return StatusCode::CANCELLED;
}


StatusCode DFSClientNodeP1::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext ctx;
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    FileRequest file_req;
    file_req.set_filename(filename);

    const string& file_path = WrapPath(filename);

    unique_ptr<ClientReader<FileContent>> file_resp = service_stub->FetchFile(&ctx, file_req);

    FileContent file_content;
    ofstream fd;
    fd.open(file_path);

    try {
        while(file_resp->Read(&file_content)) {
            fd << file_content.data();
            // dfs_log(LL_SYSINFO) << "file content size: " << file_buf.length();
        }

        fd.close();
    } catch (exception& e) {
        dfs_log(LL_ERROR) << "Error in getting file: " << file_path;
        return StatusCode::CANCELLED;
    }

    Status fetch_status = file_resp->Finish();
    if(fetch_status.ok()) {
        return StatusCode::OK;
    } else {
        return fetch_status.error_code();
    }
    return StatusCode::CANCELLED;
}

StatusCode DFSClientNodeP1::Delete(const std::string& filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext ctx;
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    FileRequest file_req;
    file_req.set_filename(filename);

    Empty file_resp;

    Status delete_status = service_stub->DeleteFile(&ctx, file_req, &file_resp);

    if(delete_status.ok()) {
        return StatusCode::OK;
    } else {
        return delete_status.error_code();
    }

    return StatusCode::CANCELLED;
}

StatusCode DFSClientNodeP1::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list all files here. This method
    // should connect to your service's list method and return
    // a list of files using the message type you created.
    //
    // The file_map parameter is a simple map of files. You should fill
    // the file_map with the list of files you receive with keys as the
    // file name and values as the modified time (mtime) of the file
    // received from the server.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext ctx;
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    Empty file_req;
    FilesList list_resp;

    Status list_status = service_stub->ListFiles(&ctx, file_req, &list_resp);

    if(list_status.ok()) {
        for(const FileStat& fstat : list_resp.file()) {
            file_map->insert(pair<string, int>(fstat.filename(), TimeUtil::TimestampToSeconds(fstat.modified())));
            dfs_log(LL_SYSINFO) << "File: " << fstat.filename();
        }
        return StatusCode::OK;
    } else {
        return list_status.error_code();
    }
}

StatusCode DFSClientNodeP1::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. This method should
    // retrieve the status of a file on the server. Note that you won't be
    // tested on this method, but you will most likely find that you need
    // a way to get the status of a file in order to synchronize later.
    //
    // The status might include data such as name, size, mtime, crc, etc.
    //
    // The file_status is left as a void* so that you can use it to pass
    // a message type that you defined. For instance, may want to use that message
    // type after calling Stat from another method.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext ctx;
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    FileRequest request;
    request.set_filename(filename);

    FileStatus response;

    Status stat_status = service_stub->GetFileStatus(&ctx, request, &response);

    if(stat_status.ok()) {
        dfs_log(LL_SYSINFO) << "File: " << response.filename() << " created: " << response.created();
        return StatusCode::OK;
    } else {
        return stat_status.error_code();
    }

    return stat_status.error_code();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//

