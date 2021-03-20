#include "gfserver.h"
#include "cache-student.h"

#define BUFSIZE (630)

ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void* arg){
	message_queue_args *mqa = (message_queue_args *)arg;
	char reqUrl[BUFSIZE];
	snprintf(reqUrl, BUFSIZE, "%s%s",(char *) arg, path);

	pthread_mutex_lock(mqa->mqueue_mutex);
	while(steque_isempty(mqa->message_queue)) {
		pthread_cond_wait(mqa->mqueue_cond, mqa->mqueue_mutex);
	}

	shm_data_struct *sds = (shm_data_struct *)steque_pop(mqa->message_queue);
	pthread_mutex_unlock(mqa->mqueue_mutex);

	int fd = shm_open(sds->name, O_RDWR, 0666);
	if(fd < 0) {
		fprintf(stderr, "Error, couldn't open IPC for requesting.\n");
		return -1;
	}

	char *reqBuf = mmap(NULL, sds->segsize, PROT_WRITE, MAP_SHARED, fd, 0);
	if(reqBuf < 0) {
		fprintf(stderr, "Error, couldn't map correctly.\n");
		return -1;
	}

	int messageSizeSent = snprintf(reqBuf, sds->segsize, reqUrl);
	if(messageSizeSent < 0) {
		fprintf(stderr, "Error, couldn't send request url correctly. \n");
		return -1;
	}
	// int fildes;
	// size_t file_len, bytes_transferred;
	// ssize_t read_len, write_len;
	// char buffer[BUFSIZE];
	// char *data_dir = arg;
	// struct stat statbuf;

	// strncpy(buffer,data_dir, BUFSIZE);
	// strncat(buffer,path, BUFSIZE);

	// if( 0 > (fildes = open(buffer, O_RDONLY))){
	// 	if (errno == ENOENT)
	// 		/* If the file just wasn't found, then send FILE_NOT_FOUND code*/ 
	// 		return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
	// 	else
	// 		/* Otherwise, it must have been a server error. gfserver library will handle*/ 
	// 		return SERVER_FAILURE;
	// }

	// /* Calculating the file size */
	// if (0 > fstat(fildes, &statbuf)) {
	// 	return SERVER_FAILURE;
	// }

	// file_len = (size_t) statbuf.st_size;

	// gfs_sendheader(ctx, GF_OK, file_len);

	// /* Sending the file contents chunk by chunk. */
	// bytes_transferred = 0;
	// while(bytes_transferred < file_len){
	// 	read_len = read(fildes, buffer, BUFSIZE);
	// 	if (read_len <= 0){
	// 		fprintf(stderr, "handle_with_file read error, %zd, %zu, %zu", read_len, bytes_transferred, file_len );
	// 		return SERVER_FAILURE;
	// 	}
	// 	write_len = gfs_send(ctx, buffer, read_len);
	// 	if (write_len != read_len){
	// 		fprintf(stderr, "handle_with_file write error");
	// 		return SERVER_FAILURE;
	// 	}
	// 	bytes_transferred += write_len;
	// }

	// return bytes_transferred;
}
