#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sys/signal.h>
#include <printf.h>
#include <curl/curl.h>

#include "gfserver.h"
#include "cache-student.h"
#include "shm_channel.h"
#include "simplecache.h"

#if !defined(CACHE_FAILURE)
#define CACHE_FAILURE (-1)
#endif // CACHE_FAILURE

#define MAX_CACHE_REQUEST_LEN 5041

static void _sig_handler(int signo){
	if (signo == SIGTERM || signo == SIGINT){
		// you should do IPC cleanup here
		// pthread_cond_destroy(&cond);
		// pthread_mutex_destroy(&m);
		// steque_destroy(&req_queue);
		mq_unlink(MESSAGE_QUEUE_RESPONSE);
		mq_unlink(MESSAGE_QUEUE_REQUEST);
		exit(signo);
	}
}

unsigned long int cache_delay;

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work queue (Default is 7, Range is 1-31415)\n"      \
"  -d [delay]          Delay in simplecache_get (Default is 0, Range is 0-5000000 (microseconds)\n "	\
"  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"cachedir",           required_argument,      NULL,           'c'},
  {"nthreads",           required_argument,      NULL,           't'},
  {"help",               no_argument,            NULL,           'h'},
  {"hidden",			 no_argument,			 NULL,			 'i'}, /* server side */
  {"delay", 			 required_argument,		 NULL, 			 'd'}, // delay.
  {NULL,                 0,                      NULL,             0}
};

static steque_t *req_queue;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
sem_t *mutex_sem;

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

void *get_requests(void *arg) {
	//job_args *j_arg = (job_args *)arg;
	while(1) {
		pthread_mutex_lock(&m);
		while(steque_isempty(req_queue)) {
			pthread_cond_wait(&cond, &m);
		}
		cache_req_args *cra = steque_pop(req_queue);
		pthread_mutex_unlock(&m);

		cache_res_args *cache_res = malloc(sizeof(cache_res_args));

		int cache_fd = simplecache_get(cra->request_path);
		// printf("fd: %d for path: %s for shm: %s\n", cache_fd, cra->request_path, cra->shm_name);
		if(cache_fd < 0) {
			fprintf(stderr, "Error, couldn't find file: %s in cache for %s.\n", cra->request_path, cra->shm_name);
			cache_res->status = GF_FILE_NOT_FOUND;
			cache_res->size = 0;
		} else {
			cache_res->status = GF_OK;
			struct stat fileStat;
			int fileStatus = fstat(cache_fd, &fileStat);
			if(fileStatus < 0) {
				fprintf(stderr, "Error, couldn't get the file status for fd.\n");
				cache_res->status = GF_FILE_NOT_FOUND;
				cache_res->size = 0;
			} else {
				off_t fileSize = fileStat.st_size;
				if(fileSize < 0) {
					fprintf(stderr, "Error, invalid file length.\n");
					cache_res->status = GF_FILE_NOT_FOUND;
					cache_res->size = 0;
				} else {
					cache_res->size = fileSize;
				}
			}

		}

		int shm_fd = shm_open(cra->shm_name, O_RDWR, 0777);
		if(shm_fd < 0) {
			fprintf(stderr, "Error, couldn't open the posix ipc segment.\n");
		}

		ftruncate(shm_fd, sizeof(struct shm_struct_ptr) + cra->segsize);
		shm_struct_ptr *shm =  mmap(NULL, sizeof(struct shm_struct_ptr) + cra->segsize, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);

		sem_wait(&shm->wsem);
		shm->status = cache_res->status;
		shm->size = cache_res->size;
		sem_post(&shm->rsem);
		int readVal, writeVal;
		sem_getvalue(&shm->rsem, &readVal);
		sem_getvalue(&shm->wsem, &writeVal);

		printf("whats the semaphores for: %s with path: %s read: %d write: %d\n", cra->shm_name, cra->request_path, readVal, writeVal);
		if(cache_res->status == GF_OK && cache_res->size > 0) {
			size_t bytes_sent = 0, bytes_read = 0;
			size_t file_length = cache_res->size;

			int index = 0;
			// sem_post(&shm->rsem);
			while(bytes_sent < file_length) {
				if(sem_wait(&shm->wsem) == -1) {
					fprintf(stderr, "Error, couldn't wait on semaphore mutex.\n");
					exit(1);
				}
				size_t write_chunk = 0;
				if(file_length - bytes_sent > cra->segsize) {
					write_chunk = cra->segsize;
				} else {
					write_chunk = file_length - bytes_sent;
				}
				bytes_read = pread(cache_fd, shm->data, write_chunk, index);
				shm->numofbytes = bytes_read;

				index += bytes_read;
				bytes_sent += bytes_read;
				if(sem_post(&shm->rsem) == -1) {
					fprintf(stderr, "Error, couldn't unlock semaphore mutex.\n");
					break;
				}
			}
			sem_wait(&shm->wsem);
		}
		sem_destroy(&shm->wsem);
		sem_destroy(&shm->rsem);
		munmap(shm, sizeof(struct shm_struct_ptr) + cra->segsize);
		// shm_unlink(cra->shm_name);
		free(cache_res);
		free(cra);
	}
	return 0;
}

int main(int argc, char **argv) {
	int nthreads = 7;
	char *cachedir = "locals.txt";
	char option_char;

	/* disable buffering to stdout */
	setbuf(stdout, NULL);

	while ((option_char = getopt_long(argc, argv, "id:c:hlxt:", gLongOptions, NULL)) != -1) {
		switch (option_char) {
			default:
				Usage();
				exit(1);
			case 'c': //cache directory
				cachedir = optarg;
				break;
			case 'h': // help
				Usage();
				exit(0);
				break;    
			case 't': // thread-count
				nthreads = atoi(optarg);
				break;
			case 'd':
				cache_delay = (unsigned long int) atoi(optarg);
				break;
			case 'i': // server side usage
			case 'l': // experimental
			case 'x': // experimental
				break;
		}
	}

	if (cache_delay > 5000000) {
		fprintf(stderr, "Cache delay must be less than 5000000 (us)\n");
		exit(__LINE__);
	}

	if ((nthreads>31415) || (nthreads < 1)) {
		fprintf(stderr, "Invalid number of threads\n");
		exit(__LINE__);
	}

	if (SIG_ERR == signal(SIGINT, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
		exit(CACHE_FAILURE);
	}

	if (SIG_ERR == signal(SIGTERM, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
		exit(CACHE_FAILURE);
	}

	// Initialize cache
	simplecache_init(cachedir);

	// cache code gos here
	req_queue = malloc(sizeof(steque_t));
	steque_init(req_queue);
	mqd_t qd_server;

	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = 2;
	attr.mq_msgsize = MAX_MESSAGE_SIZE;
	attr.mq_curmsgs = 0;

	pthread_t *tid = malloc(nthreads * sizeof(pthread_t));

	for(int i = 0; i < nthreads; i++) {
		if(pthread_create(&tid[i], NULL, get_requests, NULL) != 0) {
			fprintf(stderr, "Error, couldn't create worker thread %d\n", i);
			exit(1);
		}
	}

	qd_server = mq_open(MESSAGE_QUEUE_REQUEST, O_RDONLY | O_CREAT, 0644, &attr);
	if(qd_server == -1) {
		fprintf(stderr, "Error, couldn't open message queue.\n");
		return -1;
	}
	while(1) {
		cache_req_args *cra = malloc(sizeof(cache_req_args));
		int mqBytesRecv = mq_receive(qd_server, (char *)cra, MAX_MESSAGE_SIZE, NULL);
		if(mqBytesRecv == -1) {
			fprintf(stderr, "Error, didn't get message from message queue.\n");
		}

		pthread_mutex_lock(&m);
		steque_enqueue(req_queue, (cache_req_args *)cra);
		pthread_mutex_unlock(&m);
		pthread_cond_signal(&cond);
	}

	for(int i = 0; i < nthreads; i++) {
		if(pthread_join(tid[i], NULL) < 0) {
			fprintf(stderr, "Error, couldn't join worker thread %d\n", i);
			exit(1);
		}
	}
	simplecache_destroy();
	steque_destroy(req_queue);
	free(req_queue);
	// Won't execute
	return 0;
}
