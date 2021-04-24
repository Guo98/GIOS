#include <regex>
#include <mutex>
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
#include <utime.h>
#include <google/protobuf/util/time_util.h>

#include "src/dfs-utils.h"
#include "src/dfslibx-clientnode-p2.h"
#include "dfslib-shared-p2.h"
#include "dfslib-clientnode-p2.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

using namespace std;

using dfs_service::FileInfo;
using dfs_service::Result;
using dfs_service::FileRequest;
using dfs_service::FileName;
using dfs_service::FileContent;
using dfs_service::Empty;
using dfs_service::FilesList;
using dfs_service::FileStat;
using dfs_service::FileStatus;

using google::protobuf::util::TimeUtil;
using google::protobuf::Timestamp;

extern dfs_log_level_e DFS_LOG_LEVEL;

int BUFSIZE = 1024;

//
// STUDENT INSTRUCTION:
//
// Change these "using" aliases to the specific
// message types you are using to indicate
// a file request and a listing of files from the server.
//
using FileRequestType = FileInfo;
using FileListResponseType = dfs_service::FilesList;

DFSClientNodeP2::DFSClientNodeP2() : DFSClientNode() {}
DFSClientNodeP2::~DFSClientNodeP2() {}

grpc::StatusCode DFSClientNodeP2::RequestWriteAccess(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to obtain a write lock here when trying to store a file.
    // This method should request a write lock for the given file at the server,
    // so that the current client becomes the sole creator/writer. If the server
    // responds with a RESOURCE_EXHAUSTED response, the client should cancel
    // the current file storage
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext ctx;
    ctx.AddMetadata("file_name", filename);
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);
    FileInfo file_req;
    Result res;

    ctx.AddMetadata("client_id", ClientId());

    file_req.set_name(filename);

    Status lock_status = service_stub->GetWriteLock(&ctx, file_req, &res);

    if(lock_status.ok()) {
        return StatusCode::OK;
    } else {
        return lock_status.error_code();
    }

    return StatusCode::CANCELLED;
}

grpc::StatusCode DFSClientNodeP2::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // stored is the same on the server (i.e. the ALREADY_EXISTS gRPC response).
    //
    // You will also need to add a request for a write lock before attempting to store.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::ALREADY_EXISTS - if the local cached file has not changed from the server version
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext ctx;
    FileName file_resp;
    ctx.AddMetadata("file_name", filename);
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    ctx.AddMetadata("client_id", ClientId());

    const string& filepath = WrapPath(filename);

    struct stat file_stats;
    int filestatval = stat(filepath.c_str(), &file_stats);
    if(filestatval != 0) {
        dfs_log(LL_ERROR) << "File path: " << filepath << " was not found.";
        return StatusCode::NOT_FOUND;
    }

    int file_size = file_stats.st_size;
    StatusCode writelock_status = this->RequestWriteAccess(filename);

    if(writelock_status == StatusCode::OK) {
        unique_ptr<ClientWriter<FileContent>> file_data_stream = service_stub->StoreFile(&ctx, &file_resp);

        // read file
        ifstream fd(filepath);
        int bytes_sent = 0;
        char buf[BUFSIZE];
        FileContent file_content;


        try {
            file_content.set_size(dfs_file_checksum(filepath, &this->crc_table));
            while(bytes_sent < file_size) {
                memset(buf, 0, sizeof buf);
                int bytes_to_send = 0;
                if(BUFSIZE < file_size - bytes_sent) {
                    bytes_to_send = BUFSIZE;
                } else {
                    bytes_to_send = file_size - bytes_sent;
                }

                fd.read(buf, bytes_to_send);
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
    } else {
        return writelock_status;
    }
    

    return StatusCode::CANCELLED;
}


grpc::StatusCode DFSClientNodeP2::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // fetched is the same on the client (i.e. the files do not differ
    // between the client and server and a fetch would be unnecessary.
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // DEADLINE_EXCEEDED - if the deadline timeout occurs
    // NOT_FOUND - if the file cannot be found on the server
    // ALREADY_EXISTS - if the local cached file has not changed from the server version
    // CANCELLED otherwise
    //
    // Hint: You may want to match the mtime on local files to the server's mtime
    //
    ClientContext ctx;
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    ctx.AddMetadata("client_id", ClientId());

    FileRequest file_req;
    file_req.set_filename(filename);

    const string& file_path = WrapPath(filename);

    file_req.set_checksum(dfs_file_checksum(file_path, &this->crc_table));

    dfs_log(LL_SYSINFO) << "client checksum: " << dfs_file_checksum(file_path, &this->crc_table);

    StatusCode writelock_status = this->RequestWriteAccess(filename);

    if(writelock_status == StatusCode::OK) {
        unique_ptr<ClientReader<FileContent>> file_resp = service_stub->FetchFile(&ctx, file_req);

        FileContent file_content;
        ofstream fd;

        try {
            dfs_log(LL_SYSINFO) << "fetch file: " << filename;
            // fd.open(file_path);
            while(file_resp->Read(&file_content)) {
                if(!fd.is_open()) {
                    fd.open(file_path);
                }
                fd << file_content.data();
                // dfs_log(LL_SYSINFO) << "fetch file: " << filename;
            }
            dfs_log(LL_SYSINFO) << "finished while loop";
            fd.close();
        } catch (exception& e) {
            dfs_log(LL_ERROR) << "Error in getting file: " << file_path;
            return StatusCode::CANCELLED;
        }

        Status fetch_status = file_resp->Finish();
        if(fetch_status.ok()) {
            return StatusCode::OK;
        } else {
            dfs_log(LL_SYSINFO) << "In the else error code";
            return fetch_status.error_code();
        }
    } else {
        return writelock_status;
    }

    
    return StatusCode::CANCELLED;
}

grpc::StatusCode DFSClientNodeP2::Delete(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You will also need to add a request for a write lock before attempting to delete.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext ctx;
    chrono::time_point<chrono::system_clock> deadline = chrono::system_clock::now() + chrono::milliseconds(deadline_timeout);
    ctx.set_deadline(deadline);

    ctx.AddMetadata("client_id", ClientId());

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

grpc::StatusCode DFSClientNodeP2::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list files here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // listing details that would be useful to your solution to the list response.
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
        for(const FileStatus& fstat : list_resp.file()) {
            file_map->insert(pair<string, int>(fstat.filename(), TimeUtil::TimestampToSeconds(fstat.modified())));
            dfs_log(LL_SYSINFO) << "File: " << fstat.filename();
        }
        return StatusCode::OK;
    } else {
        return list_status.error_code();
    }
}

grpc::StatusCode DFSClientNodeP2::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // status details that would be useful to your solution.
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

void DFSClientNodeP2::InotifyWatcherCallback(std::function<void()> callback) {

    //
    // STUDENT INSTRUCTION:
    //
    // This method gets called each time inotify signals a change
    // to a file on the file system. That is every time a file is
    // modified or created.
    //
    // You may want to consider how this section will affect
    // concurrent actions between the inotify watcher and the
    // asynchronous callbacks associated with the server.
    //
    // The callback method shown must be called here, but you may surround it with
    // whatever structures you feel are necessary to ensure proper coordination
    // between the async and watcher threads.
    //
    // Hint: how can you prevent race conditions between this thread and
    // the async thread when a file event has been signaled?
    //

    callback_lock.lock();
    callback();
    callback_lock.unlock();
}

//
// STUDENT INSTRUCTION:
//
// This method handles the gRPC asynchronous callbacks from the server.
// We've provided the base structure for you, but you should review
// the hints provided in the STUDENT INSTRUCTION sections below
// in order to complete this method.
//
void DFSClientNodeP2::HandleCallbackList() {

    void* tag;

    bool ok = false;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your file list synchronization code here.
    //
    // When the server responds to an asynchronous request for the CallbackList,
    // this method is called. You should then synchronize the
    // files between the server and the client based on the goals
    // described in the readme.
    //
    // In addition to synchronizing the files, you'll also need to ensure
    // that the async thread and the file watcher thread are cooperating. These
    // two threads could easily get into a race condition where both are trying
    // to write or fetch over top of each other. So, you'll need to determine
    // what type of locking/guarding is necessary to ensure the threads are
    // properly coordinated.
    //

    // Block until the next result is available in the completion queue.
    while (completion_queue.Next(&tag, &ok)) {
        {
            //
            // STUDENT INSTRUCTION:
            //
            // Consider adding a critical section or RAII style lock here
            //
            // The tag is the memory location of the call_data object
            AsyncClientData<FileListResponseType> *call_data = static_cast<AsyncClientData<FileListResponseType> *>(tag);

            dfs_log(LL_DEBUG2) << "Received completion queue callback";

            // Verify that the request was completed successfully. Note that "ok"
            // corresponds solely to the request for updates introduced by Finish().
            // GPR_ASSERT(ok);
            if (!ok) {
                dfs_log(LL_ERROR) << "Completion queue callback not ok.";
            }

            if (ok && call_data->status.ok()) {

                dfs_log(LL_DEBUG3) << "Handling async callback ";

                //
                // STUDENT INSTRUCTION:
                //
                // Add your handling of the asynchronous event calls here.
                // For example, based on the file listing returned from the server,
                // how should the client respond to this updated information?
                // Should it retrieve an updated version of the file?
                // Send an update to the server?
                // Do nothing?
                //

                callback_lock.lock();
                for(const FileStatus& server_file : call_data->reply.file()) {
                    FileStatus client_file;
                    struct stat file_stat;
                    const string& file_path = WrapPath(server_file.filename());
                    int filestatval = stat(file_path.c_str(), &file_stat);
                    if(filestatval != 0) {
                        dfs_log(LL_SYSINFO) << "Couldn't get file stats for file: " << file_path;
                        StatusCode fetch_status = this->Fetch(server_file.filename());
                        if(fetch_status != StatusCode::OK) {
                            //dfs_log(LL_ERROR) << "Couldn't fetch file: " << server_file.filename();
                        } else {
                            dfs_log(LL_SYSINFO) << "file doesn't exist, fetching worked";;
                        }
                    } else {
                        Timestamp* modified = new Timestamp(TimeUtil::TimeTToTimestamp(file_stat.st_mtime));
                        client_file.set_allocated_modified(modified);;
                        if(server_file.modified() > client_file.modified()) {
                            StatusCode update_status = this->Fetch(server_file.filename());
                            if(update_status != StatusCode::OK) {
                               //  dfs_log(LL_ERROR) << "Couldn't fetch to update file: " << server_file.filename();
                            } else {
                                dfs_log(LL_SYSINFO) << "was successful in fetching";
                            }
                        } else if(server_file.modified() < client_file.modified()) {
                            StatusCode store_status = this->Store(server_file.filename());
                            if(store_status != StatusCode::OK) {
                               //  dfs_log(LL_ERROR) << "Couldn't store file on server, file: " << server_file.filename();
                            } else {
                                dfs_log(LL_SYSINFO) << "was successful in storing";
                            }
                        }
                    }
                }

                callback_lock.unlock();

            } else {
                dfs_log(LL_ERROR) << "Status was not ok. Will try again in " << DFS_RESET_TIMEOUT << " milliseconds.";
                dfs_log(LL_ERROR) << call_data->status.error_message();
                std::this_thread::sleep_for(std::chrono::milliseconds(DFS_RESET_TIMEOUT));
            }

            // Once we're complete, deallocate the call_data object.
            delete call_data;

            //
            // STUDENT INSTRUCTION:
            //
            // Add any additional syncing/locking mechanisms you may need here

        }


        // Start the process over and wait for the next callback response
        dfs_log(LL_DEBUG3) << "Calling InitCallbackList";
        InitCallbackList();

    }
}

/**
 * This method will start the callback request to the server, requesting
 * an update whenever the server sees that files have been modified.
 *
 * We're making use of a template function here, so that we can keep some
 * of the more intricate workings of the async process out of the way, and
 * give you a chance to focus more on the project's requirements.
 */
void DFSClientNodeP2::InitCallbackList() {
    CallbackList<FileRequestType, FileListResponseType>();
}

//
// STUDENT INSTRUCTION:
//
// Add any additional code you need to here
//


