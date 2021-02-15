
#include "gfserver-student.h"

/*
 * Modify this file to implement the interface specified in
 * gfserver.h.
 */

typedef struct gfserver_t {
  unsigned short portno;
  int maxnpending;
  gfh_error_t (*handler)(gfcontext_t **, const char *, void*);
  void* handlerarg;
} gfserver_t;

typedef struct gfcontext_t {
  int clientSid;
  size_t filelen;
  gfstatus_t status;
} gfcontext_t;

void gfs_abort(gfcontext_t **ctx){
  close((*ctx)->clientSid);
  // free(*ctx);
}

gfserver_t* gfserver_create(){
    gfserver_t *server = malloc(sizeof(gfserver_t));
    return server;
}

ssize_t gfs_send(gfcontext_t **ctx, const void *data, size_t len){
  size_t bytesLeft = len;
  size_t index = 0;
  while(bytesLeft > 0) {
    size_t sentBytes = send((*ctx)->clientSid, data + index, bytesLeft, 0);
    bytesLeft -= sentBytes;
    index += sentBytes;
  }
  (*ctx)->filelen -= len;
  if((*ctx)->filelen == 0) {
    gfs_abort(ctx);
  }
  return len;
}

ssize_t gfs_sendheader(gfcontext_t **ctx, gfstatus_t status, size_t file_len){
    (*ctx)->filelen = file_len;
    (*ctx)->status = status;
    char headerBuf[BUFSIZE];
    char endMarker[] = "\r\n\r\n";

    switch(status) {
      default: {
        sprintf(headerBuf, "GETFILE UNKNOWN%s", endMarker);
        // gfs_abort(ctx);
      }
      break;

      case GF_INVALID: {
        sprintf(headerBuf, "GETFILE INVALID%s", endMarker);
        // gfs_abort(ctx);
      }
      break;

      case GF_FILE_NOT_FOUND: {
        sprintf(headerBuf, "GETFILE FILE_NOT_FOUND%s", endMarker);
        // gfs_abort(ctx);
      }
      break;

      case GF_ERROR: {
        sprintf(headerBuf, "GETFILE ERROR%s", endMarker);
        // gfs_abort(ctx);
      }
      break;

      case GF_OK: {
        sprintf(headerBuf, "GETFILE OK %zu%s", file_len, endMarker);
      }
      break;
    }

    size_t sent = send((*ctx)->clientSid, headerBuf, strlen(headerBuf), 0);
    if(sent == -1) {
      fprintf(stderr, "Error, couldn't send response header.");
      return -1;
    }

    return 0;
}

void gfserver_serve(gfserver_t **gfs){
  struct addrinfo srv, *res, *foundAddr;
  struct gfcontext_t *ctx = (gfcontext_t *)malloc(sizeof(gfcontext_t));
  char portBuf[6];
  int sid;

  snprintf(portBuf, 6, "%d", (*gfs)->portno);

  memset(&srv, 0, sizeof(srv));

  srv.ai_family = AF_INET;
  srv.ai_socktype = SOCK_STREAM;
  srv.ai_protocol = 0;

  int addressInfo = getaddrinfo(NULL, portBuf, &srv, &res);
  if(addressInfo != 0) {
    fprintf(stderr, "Error, couldn't get address info.");
  }

  for(foundAddr = res; foundAddr != NULL; foundAddr = foundAddr->ai_next){
    sid = socket(foundAddr->ai_family, foundAddr->ai_socktype, foundAddr->ai_protocol);
    if(sid < 0) {
      continue;
    }

    int yes = 1;
    setsockopt(sid, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    int bindResult = bind(sid, foundAddr->ai_addr, foundAddr->ai_addrlen);
    if(bindResult == 0) {
      break;
    }
    close(sid);
  }

  freeaddrinfo(res);

  listen(sid, (*gfs)->maxnpending);

  int clientSocket = 1;

  while(1) {
    struct sockaddr_storage cliAddr;
    socklen_t cliAddrLen = sizeof(cliAddr);

    clientSocket = accept(sid, (struct sockaddr*)&cliAddr, &cliAddrLen);

    ctx->clientSid = clientSocket;
    if(clientSocket < 0) {
      fprintf(stderr, "Error, couldn't accept client socket.");
      break;
    }

    char reqBuf[BUFSIZE];
    memset(&reqBuf, 0, BUFSIZE);
    int totalLength = 0;
    int recvVar = recv(clientSocket, reqBuf, BUFSIZE, 0);
    if(recvVar < 0) {
      fprintf(stderr, "nothing received");
    }
    totalLength += recvVar;
    printf("first recv buf %s\n", reqBuf);
    while((strstr(reqBuf, "\r\n\r\n") == NULL) && (recvVar > 0)) {
      char recvBuf[BUFSIZE];
      memset(&recvBuf, 0, BUFSIZE);
      recvVar = recv(clientSocket, recvBuf, BUFSIZE, 0);
      strncat(reqBuf, recvBuf, recvVar);
      printf("What is the recvBuf: %s, and whats the reqBuf: %s\n", recvBuf, reqBuf);
      totalLength += recvVar;
    }
    // int reqLength = strlen(reqBuf);
    reqBuf[totalLength] = '\0';

    if(strstr(reqBuf, "\r\n\r\n") == NULL) {
      fprintf(stderr, "Error, didn't receive the full path.");
    }

    printf("whats the request header %s", reqBuf);
    char *schemeToken = strtok(reqBuf, " ");;
    char *methodToken = strtok(NULL, " ");
    char *pathToken = strtok(NULL, " ");
    //  || strncmp(pathToken, "/", 1) != 0
    if(strcmp(schemeToken, "GETFILE") != 0 || strcmp(methodToken, "GET") != 0) {
      gfs_sendheader(&ctx, GF_INVALID, 0);
    } else {
      if(pathToken != NULL) {
        if(strncmp(pathToken, "/", 1) != 0) {
          gfs_sendheader(&ctx, GF_INVALID, 0);
        } else {
          printf("should be a valid path %s\n", pathToken);
          (*gfs)->handler(&ctx, pathToken, (*gfs)->handlerarg);
        }
      }
    }
  }
  free(gfs);
  close(sid);
}

void gfserver_set_handlerarg(gfserver_t **gfs, void* arg){
  (*gfs)->handlerarg = arg;
}

void gfserver_set_handler(gfserver_t **gfs, gfh_error_t (*handler)(gfcontext_t **, const char *, void*)){
  (*gfs)->handler = handler;
}

void gfserver_set_maxpending(gfserver_t **gfs, int max_npending){
  (*gfs)->maxnpending = max_npending;
}

void gfserver_set_port(gfserver_t **gfs, unsigned short port){
  (*gfs)->portno = port;
}
