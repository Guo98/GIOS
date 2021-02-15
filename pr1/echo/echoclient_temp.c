#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* A buffer large enough to contain the longest allowed string */
#define BUFSIZE 630

#define USAGE                                                          \
  "usage:\n"                                                           \
  "  echoclient [options]\n"                                           \
  "options:\n"                                                         \
  "  -s                  Server (Default: localhost)\n"                \
  "  -p                  Port (Default: 30605)\n"                      \
  "  -m                  Message to send to server (Default: \"Hello " \
  "spring.\")\n"                                                       \
  "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"message", required_argument, NULL, 'm'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv) {
  int option_char = 0;
  char *hostname = "localhost";
  unsigned short portno = 30605;
  char *message = "Hello Spring!!";

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "s:p:m:hx", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 's':  // server
        hostname = optarg;
        break;
      case 'p':  // listen-port
        portno = atoi(optarg);
        break;
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
      case 'm':  // message
        message = optarg;
        break;
      case 'h':  // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;
    }
  }

  setbuf(stdout, NULL);  // disable buffering

  if ((portno < 1025) || (portno > 65535)) {
    fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__,
            portno);
    exit(1);
  }

  if (NULL == message) {
    fprintf(stderr, "%s @ %d: invalid message\n", __FILE__, __LINE__);
    exit(1);
  }

  if (NULL == hostname) {
    fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
    exit(1);
  }

  /* Socket Code Here */
  struct sockaddr_in srv;

  int sid = socket(AF_INET6, SOCK_STREAM, 0);

  if(sid < 0) {
    fprintf(stderr, "\nError, couldn't create a socket.\n");
    return 0;
  }

  // connect to server
  struct hostent *server = gethostbyname(hostname);

  if(server == NULL) {
    fprintf(stderr, "\nError, no such host.\n");
    return 0;
  }

  srv.sin_family = AF_INET6;
  srv.sin_port = htons(portno);

  memcpy(&srv.sin_addr, server->h_addr, server->h_length);

  if(connect(sid, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
    fprintf(stderr, "\nError, couldn't connect to host. \n");
    return 0;
  }


  write(sid, message, strlen(message) + 1);

  char pBuf[16];
  int readCount = read(sid, &pBuf, 16);
  if(readCount <= 0) {
    fprintf(stderr, "\nError, couldn't read from server. \n");
    return 0;
  }

  printf("%s", pBuf);
}
