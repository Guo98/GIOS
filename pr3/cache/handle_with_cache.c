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

	mqd_t qd_server; 

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
	cra->segsize = sds->segsize;
	strcpy(cra->shm_name, sds->name);
	strcpy(cra->request_path, path);

	int mqBytesSent = mq_send(qd_server, (const char *)cra, MAX_MESSAGE_SIZE, 0);
	if(mqBytesSent == -1) {
		fprintf(stderr, "Error, couldn't add request to message queue.\n");
		return -1;
	}


	int shm_fd = shm_open(sds->name, O_RDWR, 0777);
	if(shm_fd < 0) {
		fprintf(stderr, "Error, couldn't open the posix ipc segment.\n");
		return -1;
	}

	shm_struct_ptr *shm = mmap(NULL, sizeof(struct shm_struct_ptr) + cra->segsize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

	sem_wait(&shm->rsem);
	gfstatus_t status = shm->status;
	size_t file_size = shm->size;
	sem_post(&shm->wsem);

	if(status == GF_FILE_NOT_FOUND || status == GF_ERROR) {
		// printf("file was not found in cache %s for %s.\n", path, sds->name);
		bytes_sent = gfs_sendheader(ctx, status, 0);
	} else {
		size_t bytes_recv = 0;
		gfs_sendheader(ctx, status, file_size);

		int semValue, writeVal;
		sem_getvalue(&shm->rsem, &semValue);
		sem_getvalue(&shm->wsem, &writeVal);
		printf("Total number of bytes to send: %zu for: %s for: %s ::::::::: status:\n", file_size, path, sds->name);

		while(bytes_sent < file_size) {
			if(sem_wait(&shm->rsem) == -1) {
				fprintf(stderr, "Error, couldn't wait on semaphore mutex.\n");
				return -1;
			}

			size_t chunk_bytes = 0;
			while(chunk_bytes < shm->numofbytes) {
				// printf("in the while loop here passing numofbytes: %zu for: %s\n", shm->numofbytes, sds->name);
				bytes_recv = gfs_send(ctx, shm->data, shm->numofbytes);
				chunk_bytes += bytes_recv;
			}
			// bytes_recv = gfs_send(ctx, shm->data, shm->numofbytes);
			if(chunk_bytes < 0) {
				fprintf(stderr, "Error, couldn't send bytes.\n");
				return -1;
			}
			bytes_sent += chunk_bytes;
			//cprintf("bytes sent: %zu for ::::::::::::::%s ::::::::::::: %d\n", bytes_sent, sds->name, semValue);
			if(sem_post(&shm->wsem) == -1) {
				fprintf(stderr, "Error, couldn't post on semaphore mutex.\n");
				return -1;
			}

		}
		sem_post(&shm->wsem);

	}
	
	sem_destroy(&shm->wsem);
	sem_destroy(&shm->rsem);

	munmap(shm, sizeof(struct shm_struct_ptr) + cra->segsize);
	// shm_unlink(sds->name);
	pthread_mutex_lock(&mqa->mqueue_mutex);
	steque_enqueue(m_queue, sds);
	pthread_mutex_unlock(&mqa->mqueue_mutex);
	pthread_cond_signal(&mqa->mqueue_cond);

	free(cra);
	// free(cache_res);
	printf("reaches the return statement: %zu for: %s for: %s\n", bytes_sent, sds->name, path);
	return bytes_sent;
}

