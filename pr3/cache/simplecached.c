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

	pthread_mutex_lock(&m);
	while(steque_isempty(req_queue)) {
		pthread_cond_wait(&cond, &m);
	}
	cache_req_args *cra = steque_pop(req_queue);
	pthread_mutex_unlock(&m);

	cache_res_args *cache_res = malloc(sizeof(cache_res_args));

	int cache_fd = simplecache_get(cra->request_path);
	if(cache_fd < 0) {
		fprintf(stderr, "Error, couldn't find file in cache.\n");
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

	mqd_t qd_client;
	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = MAX_MESSAGE_SIZE;
	attr.mq_curmsgs = 0;
	qd_client = mq_open(MESSAGE_QUEUE_RESPONSE, O_WRONLY | O_CREAT, 0644, &attr);
	if(qd_client == -1) {
		fprintf(stderr, "Error, couldn't open response message queue.\n");
	}

	int mqBytesSent = mq_send(qd_client, (const char *)cache_res, MAX_MESSAGE_SIZE, 0);
	if(mqBytesSent == -1) {
		fprintf(stderr, "Error, couldn't add response to message queue.\n");
	}

	if(cache_res->status == GF_OK && cache_res->size > 0) {
		printf("will need to use posix ipc\n");
		size_t bytes_sent = 0;
		size_t file_length = cache_res->size;
		mutex_sem = sem_open(SEM_MUTEX_NAME, O_CREAT, 0660, 1);
		if(mutex_sem == SEM_FAILED) {
			fprintf(stderr, "Error, couldn't create semaphores.\n");
		}

		int shm_fd = shm_open(cra->shm_name, O_RDWR, 0666);
		if(shm_fd < 0) {
			fprintf(stderr, "Error, couldn't open the posix ipc segment.\n");
		}
		void *ptr = mmap(0, cra->segsize, PROT_WRITE, MAP_SHARED, shm_fd, 0);
		while(bytes_sent < file_length) {
			if(sem_wait(mutex_sem) == -1) {
				fprintf(stderr, "Error, couldn't wait on semaphore mutex.\n");
				exit(1);
			}

			

			if(sem_post(mutex_sem) == -1) {
				fprintf(stderr, "Error, couldn't unlock semaphore mutex.\n");
				exit(1);
			}
		}
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
			return -1;
		}

		pthread_mutex_lock(&m);
		steque_enqueue(req_queue, (cache_req_args *)cra);
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&m);
	}

	// Won't execute
	return 0;
}
