/*
 *  This file is for use by students to define anything they wish.  It is used by the gf server implementation
 */
#ifndef __GF_SERVER_STUDENT_H__
#define __GF_SERVER_STUDENT_H__

#include "gf-student.h"
#include "gfserver.h"
#include "content.h"

void *request(void *arg);
void init_threads(size_t numthreads);
void cleanup_threads();

typedef struct req_args_t {
	gfcontext_t *ctx;
	const char *path;
} req_args_t;

extern pthread_mutex_t server_m;
extern pthread_cond_t server_cond;
extern steque_t *reqQueue;

#endif // __GF_SERVER_STUDENT_H__
