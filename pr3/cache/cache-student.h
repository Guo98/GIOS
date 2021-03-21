/*
*  This file is for use by students to define anything they wish.  It is used by the proxy cache implementation
*/
#ifndef __CACHE_STUDENT_H__
#define __CACHE_STUDENT_H__

#include "steque.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <mqueue.h>

#define MESSAGE_QUEUE_REQUEST "/request_queue"
#define MESSAGE_QUEUE_RESPONSE "/posix/response"
#define BUFSIZE (630)

typedef struct shm_data_struct{
    size_t *segsize;
    char *name;
} shm_data_struct;

typedef struct message_queue_args {
    pthread_mutex_t mqueue_mutex;
    pthread_cond_t mqueue_cond;
    char *server;
} message_queue_args;

typedef struct job_args {
    steque_t *job_queue;
    pthread_mutex_t job_m;
    pthread_cond_t job_c;
} job_args;

typedef struct cache_req_args {
    char *shm_name;
    char *request_path;
    size_t *segsize;
} cache_req_args;

extern steque_t *m_queue;


#endif // __CACHE_STUDENT_H__