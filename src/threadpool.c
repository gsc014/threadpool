#include "headerfiles/threadpool.h"

#include <stdio.h>
#include <stdlib.h>

// frees threadpool and all structures within.
int free_threadpool(Threadpool_t *threadpool){
    if(threadpool == NULL){
        return 0;
    }

    if(threadpool->global_taskqueue.work_available.initialized){
        platform_condition_destroy(&threadpool->global_taskqueue.work_available);
    }
    if(threadpool->global_taskqueue.lock.initialized){
        platform_mutex_destroy(&threadpool->global_taskqueue.lock);
    }

    free(threadpool->global_taskqueue.array_info.elements);
    free(threadpool->waiting_array.array_info.elements);
    free(threadpool->workers);
    free(threadpool);

    return 1;
}

static void worker_entry(void *arg){
    Worker_t *worker = (Worker_t *)arg;
    scheduler(worker);
}

// this function is used by threads to find new tasks to execute.
void scheduler(Worker_t *worker){
    Threadpool_t *threadpool = worker->threadpool;
    // is there any work available?
    while(true){
        // queue not empty OR threadpool is shutting down
        platform_mutex_lock(&threadpool->global_taskqueue.lock);
        while(threadpool->global_taskqueue.task_count == 0 && atomic_load(&threadpool->shutdown) == false){
            // i am not sure if the condition variable should be inside the global queue? maybe it should be in the main threadpool struct?
            platform_condition_wait(&threadpool->global_taskqueue.work_available, &threadpool->global_taskqueue.lock);
        }

        // is the threadpool shutting down?
        if(atomic_load(&threadpool->shutdown)){
            platform_mutex_unlock(&threadpool->global_taskqueue.lock);
            break;
        }

        // find task in global task queue
        Task_t task;
        bool found = pop(&threadpool->global_taskqueue, &task);
        platform_mutex_unlock(&threadpool->global_taskqueue.lock);
        if(found){
            // execute the task
            task.task(task.args);
        }
    }
    // shutdown
    return;
}

// adds a task to threadpool
bool add_task(Threadpool_t *threadpool, void (*task)(void *), void *args){
    if(threadpool == NULL || task == NULL){
        return false;
    }

    Task_t new_task = {.task = task, .args = args};
    platform_mutex_lock(&threadpool->global_taskqueue.lock);

    if(atomic_load(&threadpool->shutdown)){
        platform_mutex_unlock(&threadpool->global_taskqueue.lock);
        return false;
    }

    bool added = enqueue(&threadpool->global_taskqueue, new_task);
    if(added){
        platform_condition_signal(&threadpool->global_taskqueue.work_available);
    }
    platform_mutex_unlock(&threadpool->global_taskqueue.lock);
    return added;
}

int join_all_threads(Threadpool_t *threadpool){
    Worker_t *workers = threadpool->workers;
    int result = 0;

    for(size_t i = 0; i < threadpool->worker_count; i++){
        if(platform_thread_join(&workers[i].thread) != 0){
            printf("Failed to join thread %zu.\n", i);
            result = -1;
        }
    }

    return result;
}

// must be called from main thread
void shutdown_threadpool(Threadpool_t *threadpool){
    if(threadpool == NULL){
        return;
    }
    // wake all sleeping threads so they can exit.
    platform_mutex_lock(&threadpool->global_taskqueue.lock);
    atomic_store(&threadpool->shutdown, true);
    platform_condition_broadcast(&threadpool->global_taskqueue.work_available);
    platform_mutex_unlock(&threadpool->global_taskqueue.lock);
    if(join_all_threads(threadpool) == 0){
        free_threadpool(threadpool);
    }
    return;
}

// inits the threadpool.
Threadpool_t *init_threadpool(void){
    Threadpool_t *threadpool = calloc(1, sizeof(Threadpool_t));
    if(threadpool == NULL){
        printf("Failed to allocate memory for threadpool.\n");
        return NULL;
    }

    atomic_init(&threadpool->shutdown, false);

    // init global task queue
    // TODO: make a general funciton to init queues and arrays
    Queue_sensitive_t *tq = &threadpool->global_taskqueue;
    tq->array_info.elements = calloc(TASKQUEUE_SIZE, sizeof(Task_t));
    tq->array_info.capacity = TASKQUEUE_SIZE;

    tq->head = 0;
    tq->tail = 0;

    if(platform_mutex_init(&threadpool->global_taskqueue.lock) != 0){
        printf("Failed to initialize mutex for threadpool.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    if(platform_condition_init(&threadpool->global_taskqueue.work_available) != 0){
        printf("Failed to initialize condition variable for threadpool.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    // init waiting ARRAY
    Sensitive_array_t *wa = &threadpool->waiting_array;
    wa->array_info.elements = calloc(WAITINGQUEUE_SIZE, sizeof(Task_t));
    wa->array_info.capacity = WAITINGQUEUE_SIZE;

    atomic_flag_clear(&wa->lock);

    // init threads array
    threadpool->workers = calloc(THREADCOUNT, sizeof(Worker_t));

    // check if all allocations were successful
    if(threadpool->global_taskqueue.array_info.elements == NULL ||
       threadpool->waiting_array.array_info.elements == NULL ||
       threadpool->workers == NULL){
        printf("Failed to allocate memory for threadpool.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    // init the threads
    Worker_t *workers = threadpool->workers;
    for(int i = 0; i < THREADCOUNT; i++){
        Worker_t *worker = &workers[i];
        worker->id = i;
        worker->threadpool = threadpool;
        if( platform_thread_create(&worker->thread, worker_entry, worker) != 0){
            printf("Failed to create thread when init threadpool %d.\n", i);
            shutdown_threadpool(threadpool);
            return NULL;
        }
        threadpool->worker_count++;
    }

    return threadpool;
}
