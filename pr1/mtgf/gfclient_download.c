#include "gfclient-student.h"

#define MAX_THREADS (2105)

#define USAGE                                                   \
  "usage:\n"                                                    \
  "  webclient [options]\n"                                     \
  "options:\n"                                                  \
  "  -h                  Show this help message\n"              \
  "  -r [num_requests]   Request download total (Default: 5)\n" \
  "  -p [server_port]    Server port (Default: 30605)\n"        \
  "  -s [server_addr]    Server address (Default: 127.0.0.1)\n" \
  "  -t [nthreads]       Number of threads (Default 8)\n"       \
  "  -w [workload_path]  Path to workload file (Default: workload.txt)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"nthreads", required_argument, NULL, 't'},
    {"nrequests", required_argument, NULL, 'r'},
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"workload-path", required_argument, NULL, 'w'},
    {NULL, 0, NULL, 0}};

static void Usage() { fprintf(stderr, "%s", USAGE); }

static void localPath(char *req_path, char *local_path) {
  static int counter = 0;

  sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

static FILE *openFile(char *path) {
  char *cur, *prev;
  FILE *ans;

  /* Make the directory if it isn't there */
  prev = path;
  while (NULL != (cur = strchr(prev + 1, '/'))) {
    *cur = '\0';

    if (0 > mkdir(&path[0], S_IRWXU)) {
      if (errno != EEXIST) {
        perror("Unable to create directory");
        exit(EXIT_FAILURE);
      }
    }

    *cur = '/';
    prev = cur;
  }

  if (NULL == (ans = fopen(&path[0], "w"))) {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  return ans;
}

/* Callbacks ========================================================= */
static void writecb(void *data, size_t data_len, void *arg) {
  FILE *file = (FILE *)arg;

  fwrite(data, 1, data_len, file);
}

// pthread mutex and condition setup
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t bossM = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bossCond = PTHREAD_COND_INITIALIZER;
static steque_t *downloadQueue;

typedef struct download_args {
  char *server;
  unsigned short portno;
  int numrequests;
  int returncode;
} download_args;

/* Main ========================================================= */
int main(int argc, char **argv) {
  /* COMMAND LINE OPTIONS ============================================= */
  char *server = "localhost";
  unsigned short port = 30605;
  char *workload_path = "workload.txt";
  int option_char = 0;
  int nrequests = 5;
  int nthreads = 8;
  int returncode = 0;
  // gfcrequest_t *gfr = NULL;
  // FILE *file = NULL;
  char *req_path = NULL;
  // char local_path[1066];
  int i = 0;

  setbuf(stdout, NULL);  // disable caching

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:n:hs:w:r:t:", gLongOptions,
                                    NULL)) != -1) {
    switch (option_char) {
      case 'h':  // help
        Usage();
        exit(0);
        break;
      case 'r':
        nrequests = atoi(optarg);
        break;
      case 'n':  // nrequests
        break;
      case 'p':  // port
        port = atoi(optarg);
        break;
      default:
        Usage();
        exit(1);
      case 's':  // server
        server = optarg;
        break;
      case 't':  // nthreads
        nthreads = atoi(optarg);
        break;
      case 'w':  // workload-path
        workload_path = optarg;
        break;
    }
  }

  if (EXIT_SUCCESS != workload_init(workload_path)) {
    fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
    exit(EXIT_FAILURE);
  }

  if (nthreads < 1) {
    nthreads = 1;
  }
  if (nthreads > MAX_THREADS) {
    nthreads = MAX_THREADS;
  }

  gfc_global_init();

  /* add your threadpool creation here */
  downloadQueue = malloc(sizeof(steque_t));
  pthread_t *tid = malloc(nthreads * sizeof(pthread_t));
  download_args *dargs = malloc(sizeof(download_args));

  dargs->server = server;
  dargs->portno = port;
  dargs->numrequests = nrequests;
  dargs->returncode = returncode;

  steque_init(downloadQueue);

  for(i = 0; i < nthreads; i++) {
    if(pthread_create(&tid[i], NULL, download, dargs) != 0) {
      fprintf(stderr, "Error, couldn't create worker thread %d\n", i);
      exit(1);
    }
  }

  for(int j = 0; j < nrequests; j++) {
    req_path = workload_get_path();
    pthread_mutex_lock(&m);
    steque_enqueue(downloadQueue, req_path);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&m);
  }

  pthread_mutex_lock(&bossM);
  while(dargs->numrequests > 0) {
    printf("how many requests are left %d\n", dargs->numrequests);
    pthread_cond_wait(&bossCond, &bossM);
  }
  pthread_mutex_unlock(&bossM);

  pthread_cond_broadcast(&cond);
  for(int i = 0; i < nthreads; i++) {
    if(pthread_join(tid[i], NULL) < 0) {
      fprintf(stderr, "Error, couldn't join worker thread %d\n", i);
      exit(1);
    }
  }
  printf("finished joining the threads\n");
  gfc_global_cleanup();  // use for any global cleanup for AFTER your thread
                         // pool has terminated.
  free(tid);
  free(dargs);
  return 0;
}

void *download(void *args) {
  printf("makes it in here\n");
  download_args *dargs = (download_args *)args;
  gfcrequest_t *gfr = NULL;
  FILE *file = NULL;
  char *req_path = NULL;
  char local_path[1066];
  /* Build your queue of requests here */
  while(1) {
    /* Note that when you have a worker thread pool, you will need to move this
     * logic into the worker threads */
    pthread_mutex_lock(&m);

    while(steque_isempty(downloadQueue)) {
      if(dargs->numrequests == 0) {
        pthread_mutex_unlock(&m);
        return 0;
      }
      pthread_cond_wait(&cond, &m);
    }
    req_path = steque_pop(downloadQueue);
    pthread_mutex_unlock(&m);

    if (strlen(req_path) > 1024) {
      fprintf(stderr, "Request path exceeded maximum of 1024 characters\n.");
      exit(EXIT_FAILURE);
    }

    localPath(req_path, local_path);

    file = openFile(local_path);

    gfr = gfc_create();
    gfc_set_server(&gfr, dargs->server);
    gfc_set_path(&gfr, req_path);
    gfc_set_port(&gfr, dargs->portno);
    gfc_set_writefunc(&gfr, writecb);
    gfc_set_writearg(&gfr, file);

    fprintf(stdout, "Requesting %s%s\n", dargs->server, req_path);

    if (0 > (dargs->returncode = gfc_perform(&gfr))) {
      fprintf(stdout, "gfc_perform returned an error %d\n", dargs->returncode);
      fclose(file);
      if (0 > unlink(local_path))
        fprintf(stderr, "warning: unlink failed on %s\n", local_path);
    } else {
      fclose(file);
    }

    if (gfc_get_status(&gfr) != GF_OK) {
      if (0 > unlink(local_path))
        fprintf(stderr, "warning: unlink failed on %s\n", local_path);
    }

    fprintf(stdout, "Status: %s\n", gfc_strstatus(gfc_get_status(&gfr)));
    fprintf(stdout, "Received %zu of %zu bytes\n", gfc_get_bytesreceived(&gfr), gfc_get_filelen(&gfr));
    gfc_cleanup(&gfr);

    /*
     * note that when you move the above logic into your worker thread, you will
     * need to coordinate with the boss thread here to effect a clean shutdown.
     */
     pthread_mutex_lock(&bossM);
     dargs->numrequests--;
     pthread_cond_signal(&bossCond);
     pthread_mutex_unlock(&bossM);
    }
    return 0;
}
