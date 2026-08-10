#include "headerfiles/threadpool.h"

#include <stdio.h>
#include <stdlib.h>

// frees threadpool and all structures within.
static int free_threadpool(Threadpool_t *threadpool){
    if(threadpool == NULL){
        return 0;
    }

    if(threadpool->workers != NULL){
        for(size_t i = 0; i < threadpool->worker_capacity; i++){
            free_queue(&threadpool->workers[i].local_taskqueue_info);
        }
    }

    if(threadpool->work_available.initialized){
        platform_condition_destroy(&threadpool->work_available);
    }

    free_dual_queue(&threadpool->global_taskqueue);
    platform_mutex_destroy(&threadpool->work_wait_lock);
    free(threadpool->workers);
    free(threadpool);

    return 1;
}

static int join_all_threads(Threadpool_t *threadpool){
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
    platform_mutex_lock(&threadpool->work_wait_lock);
    atomic_store(&threadpool->shutdown, true);
    platform_condition_broadcast(&threadpool->work_available);
    platform_mutex_unlock(&threadpool->work_wait_lock);
    if(join_all_threads(threadpool) == 0){
        free_threadpool(threadpool);
    }
    return;
}

// will steal half the tasks from another workers LQ, will put first task in provided task pointer. it will naturally not be put into the new LQ.
// kinda janky, TODO: clean this up.
static bool try_steal_half(Worker_t *worker, Task_t *task){
    Threadpool_t *threadpool = worker->threadpool;
    for(size_t i = 1; i < threadpool->worker_capacity; i++){
        size_t index = (worker->id + i) % threadpool->worker_capacity;
        if(platform_mutex_trylock(&threadpool->workers[index].local_taskqueue_info.lock) == 0){
            size_t steal_count = (threadpool->workers[index].local_taskqueue_info.active_count + 1) / 2; // steal half the tasks.
            if(steal_count == 0){ // pretty sure this will never happen.
                platform_mutex_unlock(&threadpool->workers[index].local_taskqueue_info.lock);
                continue;
            }
            if(platform_mutex_trylock(&worker->local_taskqueue_info.lock) == 0){
                dequeue(&threadpool->workers[index].local_taskqueue_info, task, 1); // steal one task for ourselves.
                size_t remaining_to_move = steal_count - 1;
                if(0 < remaining_to_move){
                    if(move_queue(&threadpool->workers[index].local_taskqueue_info, &worker->local_taskqueue_info, remaining_to_move)){
                        platform_mutex_unlock(&worker->local_taskqueue_info.lock);
                        platform_mutex_unlock(&threadpool->workers[index].local_taskqueue_info.lock);
                        return true;
                    }
                }
                // move queue failed, should not happen.
                platform_mutex_unlock(&worker->local_taskqueue_info.lock);
                platform_mutex_unlock(&threadpool->workers[index].local_taskqueue_info.lock);
                return true; // we did actually steal a task, we just did not steal as many as we wanted.
            }
            else{
                platform_mutex_unlock(&threadpool->workers[index].local_taskqueue_info.lock);
            }
        }
    }
    return false;
}

// calculate how many tasks should be taken from the global queue to the local queue.
// assumes that the global queue is locked and the local queue is locked. will return 0 if no tasks should be taken.
static size_t calculate_count(Worker_t *worker, Queue_t *victim_queue){
    assert(platform_mutex_is_owned_by_current_thread(&worker->local_taskqueue_info.lock));
    assert(platform_mutex_is_owned_by_current_thread(&victim_queue->lock));

    Threadpool_t *threadpool = worker->threadpool;
    size_t free_slots = worker->local_taskqueue_info.capacity - worker->local_taskqueue_info.active_count;
    size_t fair_count = victim_queue->active_count / threadpool->worker_capacity + (victim_queue->active_count % threadpool->worker_capacity != 0);
    return fair_count < free_slots ? fair_count : free_slots;
}

static void scheduler(Worker_t *worker){
    Threadpool_t *threadpool = worker->threadpool;
    while(atomic_load(&threadpool->shutdown) == false){
        Task_t task = {.task = NULL, .args = NULL};
        // fast track: check local queue
        platform_mutex_lock(&worker->local_taskqueue_info.lock);
        bool found_task = dequeue(&worker->local_taskqueue_info, &task, 0);
        if(found_task){
            // dequeue(&worker->local_taskqueue_info, &task);
            atomic_fetch_sub(&worker->threadpool->global_taskqueue.queued_task_count, 1);
            if(worker->local_taskqueue_info.active_count <= LOCAL_QUEUE_LOW_THRESHOLD){
                Queue_t *active_queue = mutex_try_lock_active_queue(&threadpool->global_taskqueue);
                if(active_queue != NULL){
                    size_t count = calculate_count(worker, active_queue);
                    move_queue(active_queue, &worker->local_taskqueue_info, count);
                    // move_queue_dual_to_single(&threadpool->global_taskqueue, &worker->local_taskqueue_info, count, true);
                    platform_mutex_unlock(&active_queue->lock);
                }
            }
            platform_mutex_unlock(&worker->local_taskqueue_info.lock);
            task.task(task.args);
            continue;
        }
        platform_mutex_unlock(&worker->local_taskqueue_info.lock);

        // slow track: no task in local queue.
        // bool found_task = false;

        Queue_t *active_queue = mutex_lock_dual_queue(&threadpool->global_taskqueue, true); // lock active queue
        // Queue_t *active_queue = &threadpool->global_taskqueue.queues[atomic_load(&threadpool->global_taskqueue.active_queue_index)];
        bool active_index = atomic_load(&threadpool->global_taskqueue.active_queue_index);
        if(active_queue->active_count != 0){
            // if there is work in active queue.
            platform_mutex_lock(&worker->local_taskqueue_info.lock);
            size_t count = calculate_count(worker, active_queue);
            move_queue(active_queue, &worker->local_taskqueue_info, count);
            // move_queue_dual_to_single(&threadpool->global_taskqueue, &worker->local_taskqueue_info, count, true);
            found_task = dequeue(&worker->local_taskqueue_info, &task, 0);
            platform_mutex_unlock(&worker->local_taskqueue_info.lock);
            platform_mutex_unlock(&active_queue->lock);
        }
        else if(active_queue->active_count == 0){
            // if there is no work in active queue
            Queue_t *buffer_queue = mutex_lock_dual_queue(&threadpool->global_taskqueue, false); // lock buffer queue
            // Queue_t *buffer_queue = &threadpool->global_taskqueue.queues[!active_index];
            if(buffer_queue->active_count == 0){
                // steal from other workers.
                platform_mutex_unlock(&buffer_queue->lock);
                platform_mutex_unlock(&active_queue->lock);
                found_task = try_steal_half(worker, &task);
            }
            else if(buffer_queue->active_count != 0){
                // swap queues
                // found_task = dequeue_dual_select_index(&threadpool->global_taskqueue, &task, !active_index);
                found_task = dequeue(buffer_queue, &task, 0);
                platform_mutex_lock(&threadpool->global_taskqueue.switch_lock);
                atomic_store(&threadpool->global_taskqueue.active_queue_index, !active_index);
                platform_mutex_unlock(&threadpool->global_taskqueue.switch_lock);
                platform_mutex_unlock(&buffer_queue->lock);
                platform_mutex_unlock(&active_queue->lock);
            }
        }
        if(found_task == true){
            atomic_fetch_sub(&worker->threadpool->global_taskqueue.queued_task_count, 1);
            task.task(task.args);
        }
        else{
            platform_mutex_lock(&threadpool->work_wait_lock);
            while(atomic_load(&threadpool->global_taskqueue.queued_task_count) == 0 && !atomic_load(&threadpool->shutdown)){
                platform_condition_wait(&threadpool->work_available, &threadpool->work_wait_lock);
            }
            platform_mutex_unlock(&threadpool->work_wait_lock);
        }
    }
}

#if 0
// old single queue scheduler, will be removed in the future.
static void old_scheduler(Worker_t *worker){
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
        if(worker->local_taskqueue_info.active_count != 0){
            found_task = dequeue(&worker->local_taskqueue_info, &task); // find new task in LQ
        }

        /*threr is no work in the local queue. need to get sum sum ahaha. if there is no work in the global queue
        we will wait for more to be added. if there is work in GQ we will calculate how much we can take without
        being greedy. everybody gets a kinda fair share. we move the tasks from GQ to LQ
        also, we check if we have already gotten a task. we will only run this if we are completly out of options*/
        if(worker->local_taskqueue_info.active_count == 0 && found_task == false){ // is there not any work in the local queue?
            platform_mutex_unlock(&worker->local_taskqueue_info.lock);
            // we know its no tasks in LQ, is there any in GQ?
            platform_mutex_lock(&threadpool->global_taskqueue.lock);

            if(threadpool->global_taskqueue.active_count == 0){
                platform_mutex_unlock(&threadpool->global_taskqueue.lock);
                if(try_steal_half(worker, &task)){
                    found_task = true;
                }
                else{
                    platform_mutex_lock(&threadpool->global_taskqueue.lock); // this will basically never happen
                }
            }
            if(found_task == false){
                // as long as there is no work in GQ or no work was found earlier or TP is shutting down, we will wait for work to become available in GQ.
                while(threadpool->global_taskqueue.active_count == 0 && atomic_load(&threadpool->shutdown) == false){
                    platform_condition_wait(&threadpool->work_available, &threadpool->global_taskqueue.lock);
                }
                if(atomic_load(&threadpool->shutdown)){
                    platform_mutex_unlock(&threadpool->global_taskqueue.lock);
                    // platform_mutex_unlock(&worker->local_taskqueue_info.lock);
                    break;
                }
                // fetch from GQ to LQ
                platform_mutex_lock(&worker->local_taskqueue_info.lock);
    
                size_t count = how_many_tasks_should_i_take(worker);
                // moves x tasks from GQ to LQ
                move_queue(&threadpool->global_taskqueue, &worker->local_taskqueue_info, count);
                // since we did not have a task before we will need to get one now.
                found_task = dequeue(&worker->local_taskqueue_info, &task);
                platform_mutex_unlock(&worker->local_taskqueue_info.lock);
                platform_mutex_unlock(&threadpool->global_taskqueue.lock);
            }
        }

        /*there is work in LQ, thats good.. but there can always be mroe!!! at a certain threshold we will try to
        take some work. if we are <= threshold we will try to aquire the lock to the GQ, if GQ is available we
        will take some work. if it is not available, we dont care. normally a lock will wait until the lock is
        available, now we will just see if it is available, but we dont wait. we continue with our business and
        execute the task we found earlier*/
        else if(worker->local_taskqueue_info.active_count <= LOCAL_QUEUE_LOW_THRESHOLD){
            // LQ is not empty, but we are running low. If we are lucky, the GQ is available and we will fill LQ while we its available. If its not available, we wont wait for it, we will just continue with our tasks.
            if(platform_mutex_trylock(&threadpool->global_taskqueue.lock) == 0){
                // if we get lock
                size_t count = how_many_tasks_should_i_take(worker);
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
#endif

static void worker_entry(void *arg){
    Worker_t *worker = (Worker_t *)arg;
    scheduler(worker);
}

// inits the threadpool.
Threadpool_t *init_threadpool(size_t thread_count){
    if(thread_count == 0){
        return NULL;
    }

    Threadpool_t *threadpool = calloc(1, sizeof(Threadpool_t));
    if(threadpool == NULL){
        printf("Failed to allocate memory for threadpool.\n");
        return NULL;
    }

    atomic_init(&threadpool->shutdown, false);

    // init global task queue
    // TODO: make a general funciton to init queues and arrays
    // if(init_queue(&threadpool->global_taskqueue, sizeof(Task_t), TASKQUEUE_SIZE) != 0){
    //     printf("Failed to initialize global task queue.\n");
    //     free_threadpool(threadpool);
    //     return NULL;
    // }

    if(init_dual_queue(&threadpool->global_taskqueue, sizeof(Task_t), TASKQUEUE_SIZE) != 0){
        printf("Failed to initialize global dual task queue.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    // atomic_init(&threadpool->global_taskqueue.queued_task_count, 0);
    // platform_mutex_init(&threadpool->global_taskqueue.switch_lock);
    if(platform_mutex_init(&threadpool->work_wait_lock) != 0){
        printf("Failed to initialize mutex for work wait lock.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    // init threads array
    threadpool->workers = calloc(thread_count, sizeof(Worker_t));
    if(threadpool->workers == NULL){
        printf("Failed to allocate memory for workers.\n");
        free_threadpool(threadpool);
        return NULL;
    }
    threadpool->worker_capacity = thread_count;

    if (platform_condition_init(&threadpool->work_available) != 0) {
        printf("Failed to initialize condition variable.\n");
        free_threadpool(threadpool);
        return NULL;
    }

    // Initialize every worker resource before starting any threads.
    Worker_t *workers = threadpool->workers;
    for(size_t i = 0; i < thread_count; i++){
        Worker_t *worker = &workers[i];
        worker->id = i;
        worker->threadpool = threadpool;
        if(init_queue(&worker->local_taskqueue_info, sizeof(Task_t), LOCAL_TASKQUEUE_SIZE) != 0){
            printf("Failed to initialize local task queue %zu.\n", i);
            free_threadpool(threadpool);
            return NULL;
        }
    }

    for(size_t i = 0; i < thread_count; i++){
        Worker_t *worker = &workers[i];
        if( platform_thread_create(&worker->thread, worker_entry, worker) != 0){
            printf("Failed to create thread when init threadpool %zu.\n", i);
            shutdown_threadpool(threadpool);
            return NULL;
        }
        threadpool->worker_count++;
    }

    return threadpool;
}

/* next steps:
try to make the main thread do something if its waiting on something
benchmark where time is spent. how much time is wasted on locks etc.?
make a debug mode where you can see statistics, but only in debug mode.
optimize types in structs to reduce memory.
make sure that using conditions can be used by main thread functions.

should a task have a priority?
should a task have a size? so stealing etc will be more fair? a small task is currently equal to a large task.
can this be done at startup? we can calculate the approximate time/value for every task?

make a function that will make it easier to add tasks to the threadpool. thinking mostly of functions and arguments

add a add_task function that will put into a queue, only when a function flush_queue is called, all tasks will be added.
*/
