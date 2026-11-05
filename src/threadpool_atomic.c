#include "threadpool_atomic.h"

#include <stdlib.h>

static void execute_task(Task_t *task){
    if(task != NULL && task->task != NULL){
        task->task(task->args);
    }
}

static void scheduler(AtomicWorker_t *worker){
    AtomicThreadpool_t *threadpool = worker->threadpool;
    while(atomic_load(&threadpool->shutdown) == false){
        // is there any work at all?
        int bit_map_index = first_available(atomic_load(&threadpool->bit_map_map));
        if(bit_map_index == -1){
            // no work available, wait for work to be added to the threadpool
            platform_thread_yield();
            continue;
        }
        // get the bitmap for this index
        uint64_t bits = atomic_load(&threadpool->bit_maps[bit_map_index]);
        int task_index = first_available(bits); // free task
        if(task_index == -1){
            // The summary can briefly say there is work after another worker
            // takes the last task. Clear it, then restore it if a producer
            // added another task at the same time.
            uint64_t bit_map_mask = UINT64_C(1) << bit_map_index;
            atomic_fetch_and(&threadpool->bit_map_map, ~bit_map_mask);
            if(atomic_load(&threadpool->bit_maps[bit_map_index]) != 0){
                atomic_fetch_or(&threadpool->bit_map_map, bit_map_mask);
            }
            continue;
        }
        uint64_t mask = UINT64_C(1) << task_index; // make mask, this is 000...001 then shift it left by index count, so 000...100 if index is 2
        // mark task as taken, maybe it is already taken but that is fine.
        // old_value is the value before we marked it as taken. if the old bit is 0, it means noone has been here before us.
        // if the old bit is 1, it means someone has been here before us, and we should not execute the task.
        uint64_t old_value = atomic_fetch_and(&threadpool->bit_maps[bit_map_index], ~mask);
        if(!(old_value & mask)){
            // Another worker got this task first.
            continue;
        }
        size_t real_task_index = (size_t)bit_map_index * BITS_PER_BITMAP + (size_t)task_index;
        Task_t task = threadpool->tasks[real_task_index];
        
        atomic_fetch_and(&threadpool->bit_maps_occupied[bit_map_index], ~mask); // flip the occupied bit to 0
        
        // is this the last task in the bitmap? if so, update the summary bitmap.
        // i do not really understand this.
        if(atomic_load(&threadpool->bit_maps[bit_map_index]) == 0){
            uint64_t bit_map_mask = UINT64_C(1) << bit_map_index;
            atomic_fetch_and(&threadpool->bit_map_map, ~bit_map_mask);
            if(atomic_load(&threadpool->bit_maps[bit_map_index]) != 0){
                atomic_fetch_or(&threadpool->bit_map_map, bit_map_mask);
            }
        }

        execute_task(&task);
        
    }
}

static void worker_entry(void *argument){
    AtomicWorker_t *worker = argument;
    scheduler(worker);
}

void shutdown_atomic_threadpool(AtomicThreadpool_t *threadpool){
    if(threadpool == NULL){
        return;
    }

    atomic_store(&threadpool->shutdown, true);

    for(size_t i = 0; i < threadpool->worker_count; i++){
        platform_thread_join(&threadpool->workers[i].thread);
    }

    free(threadpool->workers);
    free(threadpool);
}

AtomicThreadpool_t *init_atomic_threadpool(size_t thread_count){
    if(thread_count == 0){
        return NULL;
    }

    AtomicThreadpool_t *threadpool = calloc(1, sizeof(AtomicThreadpool_t));
    if(threadpool == NULL){
        return NULL;
    }

    atomic_init(&threadpool->shutdown, false);
    atomic_init(&threadpool->bit_map_map, 0);
    atomic_init(&threadpool->bulk_input_index, 0);
    for(size_t i = 0; i < TOTAL_BITMAPS; i++){
        atomic_init(&threadpool->bit_maps_occupied[i], 0);
        atomic_init(&threadpool->bit_maps[i], 0);
    }

    threadpool->workers = calloc(thread_count, sizeof(AtomicWorker_t));
    if(threadpool->workers == NULL){
        free(threadpool);
        return NULL;
    }

    for(size_t i = 0; i < thread_count; i++){
        AtomicWorker_t *worker = &threadpool->workers[i];
        worker->id = i;
        worker->threadpool = threadpool;

        if(platform_thread_create(&worker->thread, worker_entry, worker) != 0){
            atomic_store(&threadpool->shutdown, true);
            for(size_t j = 0; j < threadpool->worker_count; j++){
                platform_thread_join(&threadpool->workers[j].thread);
            }
            free(threadpool->workers);
            free(threadpool);
            return NULL;
        }
        threadpool->worker_count++;
    }

    return threadpool;
}
