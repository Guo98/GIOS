#include "gfserver.h"
#include "cache-student.h"

ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void* arg){
	message_queue_args *mqa = (message_queue_args *)arg;
	size_t bytes_sent = 0;

	pthread_mutex_lock(&mqa->mqueue_mutex);
	while(steque_isempty(m_queue)) {
		pthread_cond_wait(&mqa->mqueue_cond, &mqa->mqueue_mutex);
	}

	shm_data_struct *sds = steque_pop(m_queue);
	pthread_mutex_unlock(&mqa->mqueue_mutex);

	mqd_t qd_server, qd_client;

	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = MAX_MESSAGE_SIZE;
	attr.mq_curmsgs = 0;

	qd_server = mq_open(MESSAGE_QUEUE_REQUEST, O_WRONLY | O_CREAT, 0644, &attr);
	if(qd_server == -1) {
		fprintf(stderr, "Error, couldn't open message queue.\n");
		return -1;
	}

	cache_req_args *cra = malloc(sizeof(cache_req_args));
	cra->segsize = (size_t)sds->segsize;
	strcpy(cra->shm_name, sds->name);
	strcpy(cra->request_path, path);

	int mqBytesSent = mq_send(qd_server, (const char *)cra, MAX_MESSAGE_SIZE, 0);
	if(mqBytesSent == -1) {
		fprintf(stderr, "Error, couldn't add request to message queue.\n");
		return -1;
	}

	cache_res_args *cache_res = malloc(sizeof(cache_res_args));

	qd_client = mq_open(MESSAGE_QUEUE_RESPONSE, O_RDONLY | O_CREAT, 0644, &attr);
	if(qd_client == -1) {
		fprintf(stderr, "Error, couldn't open repsonse message queue (handle_with_cache).\n");
		return -1;
	}

	int mqBytesRecv = mq_receive(qd_client, (char *)cache_res, MAX_MESSAGE_SIZE, 0);
	if(mqBytesRecv == -1) {
		fprintf(stderr, "Error, couldn't receive request to response message queue.\n");
		return -1;
	}

	if(cache_res->status == GF_FILE_NOT_FOUND || cache_res->status == GF_ERROR) {
		return gfs_sendheader(ctx, cache_res->status, cache_res->size);
	} else {
		gfs_sendheader(ctx, cache_res->status, cache_res->size);
		size_t bytes_recv = 0;
		int shm_fd = shm_open(sds->name, O_RDWR, 0666);
		if(shm_fd < 0) {
			fprintf(stderr, "Error, couldn't open the posix ipc segment.\n");
			return -1;
		}
		void *ptr;
		while(bytes_sent < cache_res->size) {
			if(sem_wait(&cache_res->mutex_read) == -1) {
				fprintf(stderr, "Error, couldn't wait on semaphore mutex.\n");
				exit(1);
			}
			size_t ptr_bytes;
			if(cache_res->size - bytes_sent > cra->segsize) {
				ptr_bytes = cra->segsize;
			} else {
				ptr_bytes = cache_res->size - bytes_sent;
			}
			// start reading and writing
			ptr = mmap(0, ptr_bytes, PROT_READ, MAP_SHARED, shm_fd, 0);
			
			bytes_recv = gfs_send(ctx, ptr, ptr_bytes);
			if(bytes_recv < 0) {
				fprintf(stderr, "Error, couldn't send bytes.\n");
				return -1;
			}
			bytes_sent += bytes_recv;
			if(sem_post(&cache_res->mutex_write) == -1) {
				fprintf(stderr, "Error, couldn't unlock semaphore mutex.\n");
				exit(1);
			}
		}
		// shm_unlink(sds->name);
	}

	return bytes_sent;

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

