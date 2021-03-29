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

#include "gfserver.h"
#include "cache-student.h"

/* note that the -n and -z parameters are NOT used for Part 1 */
/* they are only used for Part 2 */                         
#define USAGE                                                                         \
"usage:\n"                                                                            \
"  webproxy [options]\n"                                                              \
"options:\n"                                                                          \
"  -n [segment_count]  Number of segments to use (Default: 6)\n"                      \
"  -p [listen_port]    Listen port (Default: 30605)\n"                                 \
"  -t [thread_count]   Num worker threads (Default: 12, Range: 1-520)\n"              \
"  -s [server]         The server to connect to (Default: GitHub test data)\n"     \
"  -z [segment_size]   The segment size (in bytes, Default: 6200).\n"                  \
"  -h                  Show this help message\n"


/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"segment-count", required_argument,      NULL,           'n'},
  {"port",          required_argument,      NULL,           'p'},
  {"thread-count",  required_argument,      NULL,           't'},
  {"server",        required_argument,      NULL,           's'},
  {"segment-size",  required_argument,      NULL,           'z'},         
  {"help",          no_argument,            NULL,           'h'},
  {"hidden",        no_argument,            NULL,           'i'}, /* server side */
  {NULL,            0,                      NULL,            0}
};

extern ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg);

static gfserver_t gfs;
steque_t *m_queue;
pthread_mutex_t m_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t m_queue_cond = PTHREAD_COND_INITIALIZER;

static void _sig_handler(int signo){
  if (signo == SIGTERM || signo == SIGINT){
    printf("makes it inside the signal handler\n");
    while(!steque_isempty(m_queue)) {
      shm_data_struct *sds = steque_pop(m_queue);
      printf("unlinking this shared memory %s\n", sds->name);
      shm_unlink(sds->name);
    }
    pthread_mutex_destroy(&m_queue_mutex);
    pthread_cond_destroy(&m_queue_cond);
    steque_destroy(m_queue);
    exit(signo);
  }
}



/* Main ========================================================= */
int main(int argc, char **argv) {
  int i;
  int option_char = 0;
  unsigned short port = 30605;
  unsigned short nworkerthreads = 12;
  unsigned int nsegments = 6;
  size_t segsize = 6200;
  char *server = "https://raw.githubusercontent.com/gt-cs6200/image_data";

  /* disable buffering on stdout so it prints immediately */
  setbuf(stdout, NULL);

  if (signal(SIGINT, _sig_handler) == SIG_ERR) {
    fprintf(stderr,"Can't catch SIGINT...exiting.\n");
    exit(SERVER_FAILURE);
  }

  if (signal(SIGTERM, _sig_handler) == SIG_ERR) {
    fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
    exit(SERVER_FAILURE);
  }

  /* Parse and set command line arguments */
  while ((option_char = getopt_long(argc, argv, "s:qt:hn:xp:z:l", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      default:
        fprintf(stderr, "%s", USAGE);
        exit(__LINE__);
      case 'p': // listen-port
        port = atoi(optarg);
        break;
      case 'n': // segment count
        nsegments = atoi(optarg);
        break;   
      case 's': // file-path
        server = optarg;
        break;                                          
      case 'z': // segment size
        segsize = atoi(optarg);
        break;
      case 't': // thread-count
        nworkerthreads = atoi(optarg);
        break;
      case 'i':
      case 'x':
      case 'l':
        break;
      case 'h': // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;
    }
  }

  if (segsize < 128) {
    fprintf(stderr, "Invalid segment size\n");
    exit(__LINE__);
  }

  if (!server) {
    fprintf(stderr, "Invalid (null) server name\n");
    exit(__LINE__);
  }

  if (port < 1024) {
    fprintf(stderr, "Invalid port number\n");
    exit(__LINE__);
  }

  if (nsegments < 1) {
    fprintf(stderr, "Must have a positive number of segments\n");
    exit(__LINE__);
  }

  if ((nworkerthreads < 1) || (nworkerthreads > 520)) {
    fprintf(stderr, "Invalid number of worker threads\n");
    exit(__LINE__);
  }

  // Initialize shared memory set-up here
  message_queue_args *mqa = malloc(sizeof(message_queue_args));
  mqa->mqueue_mutex = m_queue_mutex;
  mqa->mqueue_cond = m_queue_cond;
  mqa->server = server;

  m_queue = malloc(sizeof(steque_t));
  steque_init(m_queue);
  shm_data_struct shm_segment[nsegments];


  for(int i = 0; i < nsegments; i++) {
    sprintf(shm_segment[i].name, "/%d", i);
    int fd = shm_open(shm_segment[i].name , O_CREAT | O_RDWR | O_TRUNC, 0777);
    if(fd < 0) {
      fprintf(stderr, "Error, couldn't open file.\n");
      return -1;
    }
    ftruncate(fd, sizeof(struct shm_struct_ptr) + segsize);
    shm_struct_ptr *shm =  mmap(NULL, sizeof(struct shm_struct_ptr) + segsize, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    int read_sem = sem_init(&shm->rsem, 1, 0);
    if(read_sem < 0) {
      fprintf(stderr, "Error, couldn't create read semaphore.\n");
    }
    int write_sem = sem_init(&shm->wsem, 1, 1);
    if(write_sem < 0) {
      fprintf(stderr, "Error, couldn't create write semaphore.\n");
    }


    munmap(shm, sizeof(struct shm_struct_ptr) + segsize);
    shm_segment[i].segsize = segsize;
    steque_enqueue(m_queue, &shm_segment[i]);
  }

  printf("starting server.....\n");
  // Initialize server structure here
  gfserver_init(&gfs, nworkerthreads);

  // Set server options here
  gfserver_setopt(&gfs, GFS_MAXNPENDING, 121);
  gfserver_setopt(&gfs, GFS_WORKER_FUNC, handle_with_cache);
  gfserver_setopt(&gfs, GFS_PORT, port);

  // Set up arguments for worker here
  for(i = 0; i < nworkerthreads; i++) {
    gfserver_setopt(&gfs, GFS_WORKER_ARG, i, mqa);
  }
  
  // Invoke the framework - this is an infinite loop and shouldn't return
  gfserver_serve(&gfs);
  free(mqa);
  steque_destroy(m_queue);

  // not reached
  return -1;

}
