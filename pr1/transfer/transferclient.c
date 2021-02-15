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

#define USAGE                                                  \
    "usage:\n"                                                 \
    "  transferclient [options]\n"                             \
    "options:\n"                                               \
    "  -s                  Server (Default: localhost)\n"      \
    "  -p                  Port (Default: 30605)\n"            \
    "  -o                  Output file (Default cs6200.txt)\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"output", required_argument, NULL, 'o'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    char *hostname = "localhost";
    unsigned short portno = 30605;
    char *filename = "cs6200.txt";

    setbuf(stdout, NULL);

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:xp:o:h", gLongOptions, NULL)) != -1)
    {
        switch (option_char)
        {
        case 's': // server
            hostname = optarg;
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 'o': // filename
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        }
    }

    if (NULL == hostname)
    {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    if (NULL == filename)
    {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535))
    {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    /* Socket Code Here */
    struct sockaddr_in srv;

    int sid = socket(AF_INET, SOCK_STREAM, 0);

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

    srv.sin_family = AF_INET;
    srv.sin_port = htons(portno);

    memcpy(&srv.sin_addr, server->h_addr, server->h_length);

    if(connect(sid, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
      fprintf(stderr, "\nError, couldn't connect to host. \n");
      return 0;
    }


    int readCount;
    char readBuf[BUFSIZE];

    int fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);

    readCount = recv(sid, readBuf, strlen(readBuf), 0);

    while(readCount > 0) {
      write(fd, readBuf, readCount);
      printf("%s", readBuf);
      readCount = recv(sid, readBuf, strlen(readBuf), 0);
    }

    close(fd);

    return 0;
}
