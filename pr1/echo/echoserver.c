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
  struct addrinfo srv, *res, *foundAddr;
  char portBuf[6];
  int sid;

  snprintf(portBuf, 6, "%d", portno);

  memset(&srv, 0, sizeof(srv));

  srv.ai_family = AF_UNSPEC;
  srv.ai_socktype = SOCK_STREAM;
  srv.ai_protocol = 0;

  int addressInfo = getaddrinfo(NULL, portBuf, &srv, &res);

  if(addressInfo != 0) {
    fprintf(stderr, "Error, couldn't get address info");
    return 0;
  }

  for(foundAddr = res; foundAddr != NULL; foundAddr = foundAddr->ai_next) {
    sid = socket(foundAddr->ai_family, foundAddr->ai_socktype, foundAddr->ai_protocol);
    if(sid < 0) {
      continue;
    }

    int yes = 1;
    setsockopt(sid, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    int no = 0;
    setsockopt(sid, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));

    int bindResult = bind(sid, foundAddr->ai_addr, foundAddr->ai_addrlen);
    if(bindResult == 0) {
      break;
    }
    close(sid);
  }

  freeaddrinfo(res);

  listen(sid, maxnpending);

  int clientSocket = 1;

  while(1) {
    struct sockaddr_storage cliAddr;
    socklen_t cliAddrLen = sizeof(cliAddr);

    clientSocket = accept(sid, (struct sockaddr*)&cliAddr, &cliAddrLen);
    if(clientSocket < 0) {
      fprintf(stderr, "Error, couldn't accept client socket.");
      return 0;
    }

    char pBuf[16];

    int recvVar = recv(clientSocket, pBuf, sizeof(pBuf), 0);
    if(recvVar < 0) {
      fprintf(stderr, "Error, couldn't receive message from client");
      return 0;
    }

    write(clientSocket, pBuf, strlen(pBuf)+1);

    printf("%s", pBuf);
  }

  return 0;
}
