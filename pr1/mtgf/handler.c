#include "gfserver-student.h"
#include "gfserver.h"
#include "content.h"

#include "workload.h"

//
//  The purpose of this function is to handle a get request
//
//  The ctx is a pointer to the "context" operation and it contains connection state
//  The path is the path being retrieved
//  The arg allows the registration of context that is passed into this routine.
//  Note: you don't need to use arg. The test code uses it in some cases, but
//        not in others.
//
#define BUFSIZE 1024

gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void* arg){
	// printf("whats the path received %s\n", path);
	req_args_t *reqargs = malloc(sizeof(req_args_t));
	reqargs->ctx = *ctx;
	reqargs->path = path;

	pthread_mutex_lock(&server_m);
	steque_enqueue(reqQueue, reqargs);
	pthread_cond_broadcast(&server_cond);
	// printf("did it broadcast\n");
	pthread_mutex_unlock(&server_m);

	*ctx = NULL;

	return gfh_success;
}
