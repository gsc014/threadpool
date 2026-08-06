#include "headerfiles/threadpool.h"

bool add_task(Threadpool_t *threadpool, void (*task)(void *), void *args){
    if(threadpool == NULL || task == NULL){
        return false;
    }

    Task_t new_task = {.task = task, .args = args};
    Queue_t *buffer_queue = mutex_lock_dual_queue(&threadpool->global_taskqueue, false); // lock buffer queue

    platform_mutex_lock(&threadpool->work_wait_lock);
    if(atomic_load(&threadpool->shutdown)){
        platform_mutex_unlock(&threadpool->work_wait_lock);
        platform_mutex_unlock(&buffer_queue->lock);
        return false;
    }

    bool added = enqueue(buffer_queue, &new_task);
    
    if(added){
        atomic_fetch_add(&threadpool->global_taskqueue.queued_task_count, 1);
        platform_condition_signal(&threadpool->work_available);
    }
    platform_mutex_unlock(&threadpool->work_wait_lock);
    platform_mutex_unlock(&buffer_queue->lock);
    return added;
}

// bulk adds tasks to threadpool. holds lock, so is more efficient for multiple tasks. input needs to be a queue of tasks.
// will prbably be good to use with conditions in condition.c
bool add_task_bulk(Threadpool_t *threadpool, Queue_t *tasks){
    if(threadpool == NULL || tasks == NULL ||
       tasks == &threadpool->global_taskqueue.queues[0] ||
       tasks == &threadpool->global_taskqueue.queues[1]){
        return false;
    }

    Queue_t *buffer_queue = mutex_lock_dual_queue(&threadpool->global_taskqueue, false); // lock buffer queue
    platform_mutex_lock(&threadpool->work_wait_lock);
    if(atomic_load(&threadpool->shutdown)){
        platform_mutex_unlock(&threadpool->work_wait_lock);
        platform_mutex_unlock(&buffer_queue->lock);
        return false;
    }

    platform_mutex_lock(&tasks->lock);
    size_t task_count = tasks->active_count;
    size_t moved = move_queue(tasks, buffer_queue, task_count);
    platform_mutex_unlock(&tasks->lock);
    
    if(moved > 0){
        // atomic add to queued_task_count       
        atomic_fetch_add(&threadpool->global_taskqueue.queued_task_count, moved);
        platform_condition_broadcast(&threadpool->work_available);
    }
    platform_mutex_unlock(&threadpool->work_wait_lock);
    platform_mutex_unlock(&buffer_queue->lock);
    return moved > 0;
}
