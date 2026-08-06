#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "queue.h"
#include "task.h"
#include "condition.h"
#include "platform_specific/api.h"

// threadcount is now calculated at runtime based on the number of hardware threads available.
// #define THREADCOUNT 14
#define TASKQUEUE_SIZE 1024

#define LOCAL_TASKQUEUE_SIZE 16
// we dont want to fill tasks all the time, at what capacity should we start moving tasks from the global queue to the local queue?
#define LOCAL_QUEUE_LOW_THRESHOLD 4

typedef struct Threadpool Threadpool_t;

typedef struct{
    size_t id;
    Threadpool_t *threadpool;
    Platform_thread_t thread;
    Queue_t local_taskqueue_info;
} Worker_t;

struct Threadpool{
    atomic_bool shutdown; // atomic bool that indicates if the threadpool is shutting down. If it is, then all threads will finish their current task and then exit.
    Platform_mutex_t work_wait_lock; // mutex for the condition variable
    Platform_condition_t work_available; // condition variable for signaling when work is available
    // Queue_t global_taskqueue; // queue of tasks that are ready to be executed.
    Dual_queue_t global_taskqueue; // queue of tasks that are ready to be executed.
    Worker_t *workers;
    size_t worker_count;
    size_t worker_capacity;
};

void shutdown_threadpool(Threadpool_t *threadpool);
Threadpool_t *init_threadpool(size_t thread_count);

bool add_task(Threadpool_t *threadpool, void (*task)(void *), void *args);
bool add_task_bulk(Threadpool_t *threadpool, Queue_t *tasks);

#endif
