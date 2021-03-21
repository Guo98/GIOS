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

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

void *get_requests(void *arg) {
	//job_args *j_arg = (job_args *)arg;

	pthread_mutex_lock(&m);
	while(steque_isempty(req_queue)) {
		pthread_cond_wait(&cond, &m);
	}
	cache_req_args *cra = (cache_req_args *)steque_pop(req_queue);
	pthread_mutex_unlock(&m);

	printf("makes it in here-------------- %s\n", cra->request_path);
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
	attr.mq_msgsize = BUFSIZE;
	attr.mq_curmsgs = 0;

	pthread_t *tid = malloc(nthreads * sizeof(pthread_t));

	for(int i = 0; i < nthreads; i++) {
		if(pthread_create(&tid[i], NULL, get_requests, NULL) != 0) {
			fprintf(stderr, "Error, couldn't create worker thread %d\n", i);
			exit(1);
		}
	}

	qd_server = mq_open(MESSAGE_QUEUE_REQUEST, O_RDONLY, 0644, &attr);
	if(qd_server == -1) {
		fprintf(stderr, "Error, couldn't open message queue.\n");
		return -1;
	}
	while(1) {
		char buf[BUFSIZE];
		cache_req_args *cra = malloc(sizeof(cache_req_args));
		int mqBytesRecv = mq_receive(qd_server, buf, BUFSIZE, NULL);
		if(mqBytesRecv == -1) {
			fprintf(stderr, "Error, didn't get message from message queue.\n");
			return -1;
		}

		cra = (cache_req_args *)buf;

		pthread_mutex_lock(&m);
		steque_enqueue(req_queue, cra);
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&m);
	}

	// Won't execute
	return 0;
}
