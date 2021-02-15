
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

#define BUFSIZE 630

#define USAGE                                                        \
  "usage:\n"                                                         \
  "  echoserver [options]\n"                                         \
  "options:\n"                                                       \
  "  -p                  Port (Default: 30605)\n"                    \
  "  -m                  Maximum pending connections (default: 1)\n" \
  "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port", required_argument, NULL, 'p'},
    {"maxnpending", required_argument, NULL, 'm'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv) {
  int option_char;
  int portno = 30605; /* port to listen on */
  int maxnpending = 1;

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 'p':  // listen-port
        portno = atoi(optarg);
        break;
      default:
        fprintf(stderr, "%s ", USAGE);
        exit(1);
      case 'm':  // server
        maxnpending = atoi(optarg);
        break;
      case 'h':  // help
        fprintf(stdout, "%s ", USAGE);
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
  if (maxnpending < 1) {
    fprintf(stderr, "%s @ %d: invalid pending count (%d)\n", __FILE__, __LINE__,
            maxnpending);
    exit(1);
  }

  /* Socket Code Here */
  int sid = socket(AF_INET6,SOCK_STREAM, 0);

  int turnOff = 0;
  setsockopt(sid, IPPROTO_IPV6, IPV6_V6ONLY, &turnOff, sizeof(turnOff));

  if(sid < 0) {
    fprintf(stderr, "\nError, couldn't create socket. \n");
    return 0;
  }

  struct sockaddr_in6 srvAddr, cliAddr;

  srvAddr.sin6_family = AF_INET6;
  srvAddr.sin6_port = htons(portno);
  srvAddr.sin6_addr = in6addr_any;

  if(bind(sid, (struct sockaddr*)&srvAddr, sizeof(srvAddr)) < 0) {
    fprintf(stderr, "\nError, couldn't bind.\n");
    return 0;
  }

  listen(sid, maxnpending);
  int newSocketfd = 1;

  while(newSocketfd != 0) {
    int cliLength = sizeof(cliAddr);
    newSocketfd = accept(sid, (struct sockaddr*)&cliAddr, (socklen_t *)&cliLength);
    if(newSocketfd < 0) {
      fprintf(stderr, "\nError, couldn't accept. \n");
      return 0;
    }

    char pBuf[BUFSIZE];
    int n = read(newSocketfd, pBuf, BUFSIZE);
    if(n < 0) {
      fprintf(stderr, "\nError, reading from socket.\n");
      return 0;
    }

    write(newSocketfd, pBuf, BUFSIZE);

    printf("%s\n", pBuf);
  }

  return 0;
}
