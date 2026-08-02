#include "headerfiles/threadpool.h"

// adds a single task to threadpool
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

    bool added = enqueue(&threadpool->global_taskqueue, &new_task);
    if(added){
        platform_condition_signal(&threadpool->work_available);
    }
    platform_mutex_unlock(&threadpool->global_taskqueue.lock);
    return added;
}

// bulk adds tasks to threadpool. holds lock, so is more efficient for multiple tasks. input needs to be a queue of tasks.
// will prbably be good to use with conditions in condition.c
bool add_task_bulk(Threadpool_t *threadpool, Queue_t *tasks){
    if(threadpool == NULL || tasks == NULL ||
       tasks == &threadpool->global_taskqueue){
        return false;
    }

    platform_mutex_lock(&threadpool->global_taskqueue.lock);

    if(atomic_load(&threadpool->shutdown)){
        platform_mutex_unlock(&threadpool->global_taskqueue.lock);
        return false;
    }

    platform_mutex_lock(&tasks->lock);
    size_t task_count = tasks->active_count;
    while(threadpool->global_taskqueue.capacity -
          threadpool->global_taskqueue.active_count < task_count){
        if(!enlarge_queue(&threadpool->global_taskqueue)){
            platform_mutex_unlock(&tasks->lock);
            platform_mutex_unlock(&threadpool->global_taskqueue.lock);
            return false;
        }
    }

    move_queue(tasks, &threadpool->global_taskqueue, task_count);
    platform_mutex_unlock(&tasks->lock);

    if(task_count > 0){
        platform_condition_broadcast(&threadpool->work_available);
    }
    platform_mutex_unlock(&threadpool->global_taskqueue.lock);
    return true;
}
