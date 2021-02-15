#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <fcntl.h>

#define BUFSIZE 630

#define USAGE                                                \
    "usage:\n"                                               \
    "  transferserver [options]\n"                           \
    "options:\n"                                             \
    "  -f                  Filename (Default: 6200.txt)\n"   \
    "  -h                  Show this help message\n"         \
    "  -p                  Port (Default: 30605)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"filename", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv)
{
    int option_char;
    int portno = 30605;             /* port to listen on */
    char *filename = "6200.txt";   /* file to transfer */

    setbuf(stdout, NULL); // disable buffering

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "xp:hf:", gLongOptions, NULL)) != -1)
    {
        switch (option_char)
        {
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        case 'f': // file to transfer
            filename = optarg;
            break;
        }
    }


    if ((portno < 1025) || (portno > 65535))
    {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    if (NULL == filename)
    {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    /* Socket Code Here */
    struct addrinfo srv, *res, *foundAddr;
    char portBuf[6];
    int sid;

    snprintf(portBuf, 6, "%d", portno);

    memset(&srv, 0, sizeof(srv));

    srv.ai_family = AF_INET; //AF_UNSPEC
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

    listen(sid, 1);

    int clientSocket = 1;

    while(1) {
      struct sockaddr_storage cliAddr;
      socklen_t cliAddrLen = sizeof(cliAddr);
      int fd = open(filename, O_RDONLY, S_IRUSR | S_IWUSR);

      if(fd == -1) {
        fprintf(stderr, "Error, couldn't read file");
        return 1;
      }

      clientSocket = accept(sid, (struct sockaddr*)&cliAddr, &cliAddrLen);
      if(clientSocket < 0) {
        fprintf(stderr, "Error, couldn't accept client socket.");
        return 0;
      }

      int readCount, writeCount;
      char fileBuf[BUFSIZE];
      char* sendFileBuf;
      readCount = read(fd, fileBuf, BUFSIZE);
      while(readCount > 0) {
        writeCount = 0;
        sendFileBuf = fileBuf;
        while(writeCount < readCount) {
          readCount -= writeCount;
          sendFileBuf += writeCount;
          writeCount = send(clientSocket, sendFileBuf, readCount, 0);
          if(writeCount == -1) {
            fprintf(stderr, "Error, nothing sent.");
            return -1;
          }
        }
        readCount = read(fd, fileBuf, BUFSIZE);
      }
      close(fd);
      close(clientSocket);
    }

    return 0;
}
