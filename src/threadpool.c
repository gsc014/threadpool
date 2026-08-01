#include "headerfiles/threadpool.h"

#include <stdio.h>
#include <stdlib.h>

// frees threadpool and all structures within.
int free_threadpool(Threadpool_t *threadpool){
    if(threadpool == NULL){
        return 0;
    }

    if(threadpool->workers != NULL){
        for(size_t i = 0; i < THREADCOUNT; i++){
            Queue_sensitive_t *local_queue =
                &threadpool->workers[i].local_taskqueue_info;

            if(local_queue->lock.initialized){
                platform_mutex_destroy(&local_queue->lock);
            }
            free(local_queue->array_info.elements);
        }
    }

    if(threadpool->work_available.initialized){
        platform_condition_destroy(&threadpool->work_available);
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

void scheduler(Worker_t *worker){
    Threadpool_t *threadpool = worker->threadpool;
    while(atomic_load(&threadpool->shutdown) == false){ // as long as we are not shutting down
        // check out local queue.
        /* will preload a task. previously did not. we will work on a more updated taskcounter. but this is
        actually just because this way i did not have to lock local queue and global queue unnecessarly
        at the same time. when i need to get global queue tasks i need to use a condition variable.
        this will make the thread sleep. previously i held the lock. this means that the local tasks would
        be impossible to reach. this does not actually mean anything tho. right now there is no functionality
        for any other than the thread itself to access its local queue, but in the future it would prob be
        smartest to do it this way.*/
        platform_mutex_lock(&worker->local_taskqueue_info.lock);
        Task_t task = {.task = NULL, .args = NULL};
        bool found_task = false;
        if(worker->local_taskqueue_info.task_count != 0){
            found_task = pop(&worker->local_taskqueue_info, &task); // find new task in LQ
        }

        /*threr is no work in the local queue. need to get sum sum ahaha. if there is no work in the global queue
        we will wait for more to be added. if there is work in GQ we will calculate how much we can take without
        being greedy. everybody gets a kinda fair share. we move the tasks from GQ to LQ
        also, we check if we have already gotten a task. we will only run this if we are completly out of options*/
        if(worker->local_taskqueue_info.task_count == 0 && found_task == false){ // is there not any work in the local queue?
            platform_mutex_unlock(&worker->local_taskqueue_info.lock);
            // we know its no tasks in LQ, is there any in GQ?
            platform_mutex_lock(&threadpool->global_taskqueue.lock);
            // as long as there is no work in GQ or no work was found earlier or TP is shutting down, we will wait for work to become available in GQ.
            while(threadpool->global_taskqueue.task_count == 0 && atomic_load(&threadpool->shutdown) == false){
                platform_condition_wait(&threadpool->work_available, &threadpool->global_taskqueue.lock);
            }
            if(atomic_load(&threadpool->shutdown)){
                platform_mutex_unlock(&threadpool->global_taskqueue.lock);
                // platform_mutex_unlock(&worker->local_taskqueue_info.lock);
                break;
            }
            
            platform_mutex_lock(&worker->local_taskqueue_info.lock);
            size_t count = 0;
            // if there are a LOT of tasks, just take as many as possible.
            if(THREADCOUNT * LOCAL_TASKQUEUE_SIZE < threadpool->global_taskqueue.task_count){
                count = worker->local_taskqueue_info.array_info.capacity - worker->local_taskqueue_info.task_count;
            }
            else{
                // dont want to be hoardning tasks, this function will try to distribute fairly.
                count = threadpool->global_taskqueue.task_count / THREADCOUNT + (threadpool->global_taskqueue.task_count % THREADCOUNT != 0);
            }
            // moves x tasks from GQ to LQ
            move_queue(&threadpool->global_taskqueue, &worker->local_taskqueue_info, count);
            // since we did not have a task before we will need to get one now.
            found_task = pop(&worker->local_taskqueue_info, &task);
            platform_mutex_unlock(&worker->local_taskqueue_info.lock);
            platform_mutex_unlock(&threadpool->global_taskqueue.lock);
            
        }
        /*there is work in LQ, thats good.. but there can always be mroe!!! at a certain threshold we will try to
        take some work. if we are <= threshold we will try to aquire the lock to the GQ, if GQ is available we
        will take some work. if it is not available, we dont care. normally a lock will wait until the lock is
        available, now we will just see if it is available, but we dont wait. we continue with our business and
        execute the task we found earlier*/
        else if(worker->local_taskqueue_info.task_count <= LOCAL_QUEUE_LOW_THRESHOLD){
            // LQ is not empty, but we are running low. If we are lucky, the GQ is available and we will fill LQ while we its available. If its not available, we wont wait for it, we will just continue with our tasks.
            if(platform_mutex_trylock(&threadpool->global_taskqueue.lock) == 0){
                size_t count = 0;
                // if there are a LOT of tasks, just take as many as possible.
                if(THREADCOUNT * LOCAL_TASKQUEUE_SIZE < threadpool->global_taskqueue.task_count){
                    count = worker->local_taskqueue_info.array_info.capacity - worker->local_taskqueue_info.task_count;
                }
                else{
                    // dont want to be hoardning tasks, this function will try to distribute fairly.
                    count = threadpool->global_taskqueue.task_count / THREADCOUNT + (threadpool->global_taskqueue.task_count % THREADCOUNT != 0);
                }
                // if we get lock
                move_queue(&threadpool->global_taskqueue, &worker->local_taskqueue_info, count);
                platform_mutex_unlock(&threadpool->global_taskqueue.lock);
            }
            platform_mutex_unlock(&worker->local_taskqueue_info.lock);
        }
        else{
            platform_mutex_unlock(&worker->local_taskqueue_info.lock);
        }
        // execute previously found task.
        if(found_task == true){
            task.task(task.args);
        }
    }
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
        platform_condition_signal(&threadpool->work_available);
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
    platform_condition_broadcast(&threadpool->work_available);
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

    if(platform_condition_init(&threadpool->work_available) != 0){
        printf("Failed to initialize condition variable for threadpool.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    // init waiting ARRAY
    // Sensitive_array_t *wa = &threadpool->waiting_array;
    // wa->array_info.elements = calloc(WAITINGQUEUE_SIZE, sizeof(Task_t));
    // wa->array_info.capacity = WAITINGQUEUE_SIZE;

    // atomic_flag_clear(&wa->lock);

    // init threads array
    threadpool->workers = calloc(THREADCOUNT, sizeof(Worker_t));

    // check if all allocations were successful
    if(threadpool->global_taskqueue.array_info.elements == NULL ||
    //    threadpool->waiting_array.array_info.elements == NULL ||
       threadpool->workers == NULL){
        printf("Failed to allocate memory for threadpool.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    // TODO: instead of callocing for every local task queue, we should allocate one big array.
    // TODO: make a function that easliy inits arrays and queues.
    // init the threads
    Worker_t *workers = threadpool->workers;
    for(int i = 0; i < THREADCOUNT; i++){
        Worker_t *worker = &workers[i];
        worker->id = i;
        worker->threadpool = threadpool;
        worker->local_taskqueue_info.task_count = 0;
        worker->local_taskqueue_info.head = 0;
        worker->local_taskqueue_info.tail = 0;
        if(platform_mutex_init(&worker->local_taskqueue_info.lock) != 0){
            printf("Failed to initialize mutex for local task queue %d.\n", i);
            free_threadpool(threadpool);
            return NULL;
        }
        worker->local_taskqueue_info.array_info.capacity = LOCAL_TASKQUEUE_SIZE;
        worker->local_taskqueue_info.array_info.elements = calloc(LOCAL_TASKQUEUE_SIZE, sizeof(Task_t));
        if(worker->local_taskqueue_info.array_info.elements == NULL){
            printf("Failed to allocate memory for local task queue %d.\n", i);
            free_threadpool(threadpool);
            return NULL;
        }

        if( platform_thread_create(&worker->thread, worker_entry, worker) != 0){
            printf("Failed to create thread when init threadpool %d.\n", i);
            shutdown_threadpool(threadpool);
            return NULL;
        }
        threadpool->worker_count++;
    }

    return threadpool;
}
