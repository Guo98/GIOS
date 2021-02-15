
#include "gfclient-student.h"

#define BUFSIZE 1024

typedef struct gfcrequest_t {
  size_t bytesReceived;
  size_t fileLength;
  gfstatus_t reqStatus;
  unsigned short portno;
  const char *hostname;
  const char *filePath;
  void (*headerfunc)(void*, size_t, void *);
  void (*writefunc)(void*, size_t, void *);
  void *headerarg;
  void *writearg;
}gfcrequest_t;

// optional function for cleaup processing.
void gfc_cleanup(gfcrequest_t **gfr){
  free(*gfr);
  *gfr = NULL;
}

gfcrequest_t *gfc_create(){
    gfcrequest_t *req = malloc(sizeof(gfcrequest_t));
    return req;
}

size_t gfc_get_bytesreceived(gfcrequest_t **gfr){
    // not yet implemented
    return (*gfr)->bytesReceived;
}

size_t gfc_get_filelen(gfcrequest_t **gfr){
    // not yet implemented
    return (*gfr)->fileLength;
}

gfstatus_t gfc_get_status(gfcrequest_t **gfr){
    // not yet implemented
    return (*gfr)->reqStatus;
}

void gfc_global_init(){
}

void gfc_global_cleanup(){
}

int gfc_perform(gfcrequest_t **gfr){
    // currently not implemented.  You fill this part in.
    struct addrinfo srv, *res, *foundAddr;
    char portBuf[6];
    int sid;

    snprintf(portBuf, 6, "%d", (*gfr)->portno);

    memset(&srv, 0, sizeof(srv));

    srv.ai_family = AF_INET;
    srv.ai_socktype = SOCK_STREAM;
    srv.ai_protocol = IPPROTO_TCP;

    struct hostent *host = gethostbyname((*gfr)->hostname);

    if(host == NULL) {
      fprintf(stderr, "\nError, no such host.\n");
      return 0;
    }


    int addressInfo = getaddrinfo((*gfr)->hostname, portBuf, &srv, &res);
    if(addressInfo != 0) {
      fprintf(stderr, "Error, couldn't get address info.");
    }

    for(foundAddr = res; foundAddr != NULL; foundAddr = foundAddr->ai_next){
      // void *addr;
      sid = socket(foundAddr->ai_family, foundAddr->ai_socktype, foundAddr->ai_protocol);
      if(sid < 0) {
        continue;
      }

      int connectRes = connect(sid, foundAddr->ai_addr, foundAddr->ai_addrlen);
      if(connectRes < 0) {
        fprintf(stderr, "Error, couldn't connect to host.");
        close(sid);
        return -1;
      } else {
        break;
      }

      close(sid);
    }

    freeaddrinfo(res);


    char request[BUFSIZE];
    sprintf(request, "GETFILE GET %s \r\n\r\n", (*gfr)->filePath);

    int sent = send(sid, request, strlen(request), 0);
    if(sent == -1) {
      fprintf(stderr, "Error, couldn't send header.");
      return -1;
    }

    size_t readCount;
    char readBuf[BUFSIZE];
    memset(&readBuf, 0, BUFSIZE);
    readCount = recv(sid, readBuf, sizeof(readBuf), 0);
    if(readCount < 0) {
      fprintf(stderr, "Error, no response header was received.");
      return -1;
    }
    readBuf[readCount] = '\0';
    char *schemeToken = strtok(readBuf, " ");
    char *statusToken = strtok(NULL, " ");
    char *fileToken;

    if(strcmp(schemeToken, "GETFILE") != 0) {
      fprintf(stderr, "Error, wrong scheme sent by server.");
      (*gfr)->reqStatus = GF_INVALID;
      close(sid);
      return -1;
    }
    readCount -= strlen(schemeToken);
    if(strcmp(statusToken, "OK") == 0) {
      (*gfr)->reqStatus = GF_OK;
      fileToken = strtok(NULL, " ");

      if(fileToken == NULL) {
        fprintf(stderr, "Error, didn't have the correct file length format.");
        (*gfr)->reqStatus = GF_INVALID;
        close(sid);
        return -1;
      } else if(strstr(fileToken, "\r\n\r\n") == NULL){
        fprintf(stderr, "Error, couldn't find the correct end marker.");
        (*gfr)->reqStatus = GF_INVALID;
        close(sid);
        return -1;
      }

      int parseRes = sscanf(fileToken, "%zu", &(*gfr)->fileLength);

      if(parseRes != 1) {
        fprintf(stderr, "Error, couldn't get the file length.");
        (*gfr)->reqStatus = GF_INVALID;
        close(sid);
        return -1;
      }
    } else if(strcmp(statusToken, "INVALID\r\n\r\n") == 0) {
      (*gfr)->reqStatus = GF_INVALID;
      close(sid);
      return -1;
    } else if(strcmp(statusToken, "FILE_NOT_FOUND\r\n\r\n") == 0) {
      (*gfr)->reqStatus = GF_FILE_NOT_FOUND;
      close(sid);
      return 0;
    } else if(strcmp(statusToken, "ERROR\r\n\r\n") == 0) {
      (*gfr)->reqStatus = GF_ERROR;
      close(sid);
      return 0;
    } else {
      fprintf(stderr, "Error, didn't receive a valid response status.");
      (*gfr)->reqStatus = GF_INVALID;
      close(sid);
      return -1;
    }
    readCount -= strlen(statusToken);
    char dataBuf[BUFSIZE] = {0};
    (*gfr)->bytesReceived = 0;
    char *charFileLength = strtok(fileToken, "\r\n\r\n");
    readCount -= strlen(charFileLength);
    readCount -= 6;
    printf("whats the read count %zu\n", readCount);
    char *outlyingBytes = strtok(NULL, "\r\n\r\n");
    if(outlyingBytes != NULL) {
      // int outlyingBytesLen = strlen(outlyingBytes);
      (*gfr)->writefunc(outlyingBytes, readCount, (*gfr)->writearg);
      (*gfr)->bytesReceived += readCount;
      printf("what are the outlying bytes %zu +++\n", readCount);
    }

    // printf("what are the outlying bytes %s +++\n", outlyingBytes);

    memset(&dataBuf, 0, BUFSIZE);
    int recvBytes;
    while(1) {
      recvBytes = recv(sid, dataBuf, BUFSIZE, 0);
      // printf("Bytes: %zu. Recv: %d, Filelength: %zu", (*gfr)->bytesReceived, recvBytes, (*gfr)->fileLength);
      if(recvBytes == 0) {
        if((*gfr)->fileLength > (*gfr)->bytesReceived) {
          close(sid);
          return -1;
        } else {
          close(sid);
          return 0;
        }
      }

      if(recvBytes < 0) {
        close(sid);
        return -1;
      }

      (*gfr)->writefunc(dataBuf, recvBytes, (*gfr)->writearg);
      (*gfr)->bytesReceived += recvBytes;
      if((*gfr)->bytesReceived == (*gfr)->fileLength) {
        close(sid);
        return 0;
      }
      memset(&dataBuf, 0, BUFSIZE);
    }
    if((*gfr)->fileLength != (*gfr)->bytesReceived) {
      fprintf(stderr, "Error, incorrect length received: %zu. Correct file length was: %zu", (*gfr)->bytesReceived, (*gfr)->fileLength);
      close(sid);
      return -1;
    } else {
      printf("Correct number of bytes was received");
    }

    return 0;
}

void gfc_set_headerarg(gfcrequest_t **gfr, void *headerarg){
  (*gfr)->headerarg = headerarg;
}

void gfc_set_headerfunc(gfcrequest_t **gfr, void (*headerfunc)(void*, size_t, void *)){
  (*gfr)->headerfunc = headerfunc;
}

void gfc_set_path(gfcrequest_t **gfr, const char* path){
  (*gfr)->filePath = path;
}

void gfc_set_port(gfcrequest_t **gfr, unsigned short port){
  (*gfr)->portno = port;
}

void gfc_set_server(gfcrequest_t **gfr, const char* server){
  (*gfr)->hostname = server;
}

void gfc_set_writearg(gfcrequest_t **gfr, void *writearg){
  (*gfr)->writearg = writearg;
}

void gfc_set_writefunc(gfcrequest_t **gfr, void (*writefunc)(void*, size_t, void *)){
  (*gfr)->writefunc = writefunc;
}

const char* gfc_strstatus(gfstatus_t status){
    const char *strstatus = NULL;

    switch (status)
    {
        default: {
            strstatus = "UNKNOWN";
        }
        break;

        case GF_INVALID: {
            strstatus = "INVALID";
        }
        break;

        case GF_FILE_NOT_FOUND: {
            strstatus = "FILE_NOT_FOUND";
        }
        break;

        case GF_ERROR: {
            strstatus = "ERROR";
        }
        break;

        case GF_OK: {
            strstatus = "OK";
        }
        break;

    }

    return strstatus;
}
