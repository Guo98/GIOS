
#include "gfserver-student.h"

#define USAGE                                                               \
  "usage:\n"                                                                \
  "  gfserver_main [options]\n"                                             \
  "options:\n"                                                              \
  "  -t [nthreads]       Number of threads (Default: 8)\n"                  \
  "  -p [listen_port]    Listen port (Default: 30605)\n"                    \
  "  -m [content_file]   Content file mapping keys to content files\n"      \
  "  -d [delay]          Delay in content_get, default 0, range 0-5000000 " \
  "(microseconds)\n "                                                       \
  "  -h                  Show this help message.\n"

#define BUFSIZE 1024
/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"content", required_argument, NULL, 'm'},
    {"delay", required_argument, NULL, 'd'},
    {"nthreads", required_argument, NULL, 't'},
    {"port", required_argument, NULL, 'p'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

extern unsigned long int content_delay;

extern gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void *arg);

static void _sig_handler(int signo) {
  if ((SIGINT == signo) || (SIGTERM == signo)) {
    exit(signo);
  }
}

pthread_mutex_t server_m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t server_cond = PTHREAD_COND_INITIALIZER;
steque_t *reqQueue;
/* Main ========================================================= */
int main(int argc, char **argv) {
  int option_char = 0;
  unsigned short port = 30605;
  char *content_map = "content.txt";
  gfserver_t *gfs = NULL;
  int nthreads = 8;

  setbuf(stdout, NULL);

  if (SIG_ERR == signal(SIGINT, _sig_handler)) {
    fprintf(stderr, "Can't catch SIGINT...exiting.\n");
    exit(EXIT_FAILURE);
  }

  if (SIG_ERR == signal(SIGTERM, _sig_handler)) {
    fprintf(stderr, "Can't catch SIGTERM...exiting.\n");
    exit(EXIT_FAILURE);
  }

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:t:rhm:d:", gLongOptions,
                                    NULL)) != -1) {
    switch (option_char) {
      case 't':  // nthreads
        nthreads = atoi(optarg);
        break;
      case 'h':  // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;
      case 'p':  // listen-port
        port = atoi(optarg);
        break;
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
      case 'm':  // file-path
        content_map = optarg;
        break;
      case 'd':  // delay
        content_delay = (unsigned long int)atoi(optarg);
        break;
    }
  }

  /* not useful, but it ensures the initial code builds without warnings */
  if (nthreads < 1) {
    nthreads = 1;
  }

  if (content_delay > 5000000) {
    fprintf(stderr, "Content delay must be less than 5000000 (microseconds)\n");
    exit(__LINE__);
  }

  content_init(content_map);

  /* Initialize thread management */
  reqQueue = malloc(sizeof(steque_t));
  steque_init(reqQueue);

  pthread_t *tid = malloc(nthreads * sizeof(pthread_t));

  for(int i = 0; i < nthreads; i++) {
    if(pthread_create(&tid[i], NULL, request, NULL) != 0) {
      fprintf(stderr, "Error, couldn't create threads.\n");
      exit(1);
    }
  }

  /*Initializing server*/
  gfs = gfserver_create();

  /*Setting options*/
  gfserver_set_port(&gfs, port);
  gfserver_set_maxpending(&gfs, 21);
  gfserver_set_handler(&gfs, gfs_handler);
  gfserver_set_handlerarg(&gfs, NULL);  // doesn't have to be NULL!

  /*Loops forever*/
  gfserver_serve(&gfs);

  for(int i = 0; i < nthreads; i++) {
    if(pthread_join(tid[i], NULL) < 0) {
      fprintf(stderr, "Error, couldn't join worker thread %d\n", i);
      exit(1);
    }
  }

  steque_destroy(reqQueue);
  free(reqQueue);
  free(gfs);
  content_destroy();
}

void *request(void *arg) {
  while(1) {
    pthread_mutex_lock(&server_m);

    while(steque_isempty(reqQueue)) {
      pthread_cond_wait(&server_cond, &server_m);
    }

    req_args_t *req = steque_pop(reqQueue);
    printf("whats the file path in here %s\n", req->path);
    pthread_mutex_unlock(&server_m);
    gfcontext_t **ctx = &req->ctx;
    int fd = content_get(req->path);
    if(fd < 0) {
    	fprintf(stderr, "Error, couldn't get file.");
    	gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
    	// exit(1);
    }

    struct stat fileStat;
    int fileStatus = fstat(fd, &fileStat);

    if(fileStatus < 0) {
      fprintf(stderr, "Error, couldn't get the file status for fd.\n");
      gfs_sendheader(ctx, GF_ERROR, 0);
      // exit(1);
    }

    off_t fileSize = fileStat.st_size;
    if(fileSize < 0) {
      fprintf(stderr, "Error, invalid file length.\n");
      gfs_sendheader(ctx, GF_ERROR, 0);
    }

    printf("whats the file size %ld\n", fileSize);

    gfs_sendheader(ctx, GF_OK, fileSize);
    printf("makes it pass the send header\n");
    size_t bytesReadHolder = 0;
    size_t bytesRead = 0;
    char fileBuf[BUFSIZE];
    memset(&fileBuf, 0, BUFSIZE);
    // lseek(fd, 0, SEEK_SET);
    int index = 0;
    while(bytesReadHolder < fileSize) {
    	bytesRead = pread(fd, fileBuf, BUFSIZE, index);
      index += bytesRead;
    	size_t bytesSent = gfs_send(ctx, fileBuf, bytesRead);
    	// while(bytesSent < bytesRead) {
    	// 	size_t currentSent = gfs_send(ctx, fileBuf, bytesRead);
    	// 	if(currentSent < 0) {
    	// 		fprintf(stderr, "Error, couldn't send data properly.");
    	// 		exit(1);
    	// 	}
      //   bytesSent += currentSent;
    	// }
      if(bytesSent != bytesRead) {
        fprintf(stderr, "Error, didn't sent the correct amount of bytes.\n");
        // exit(1);
      }
    	bytesReadHolder += bytesRead;
    	memset(&fileBuf, 0, BUFSIZE);
    }
    // close(fd);
    free(req);
  }
  return 0;
}
