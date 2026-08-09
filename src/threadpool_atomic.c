#include "threadpool_atomic.h"

#include <stdlib.h>

static inline int first_available(uint64_t bits){
    if(bits == 0){
        return -1;
    }

    return (int)__builtin_ctzll(bits);
}

static inline int first_free_slot(uint64_t bits){
    uint64_t free_bits = ~bits;

    if(free_bits == 0){
        return -1; // all 64 slots occupied
    }

    return (int)__builtin_ctzll(free_bits);
}

static inline uint64_t reverse_bits64(uint64_t x)
{
    x = ((x >> 1)  & 0x5555555555555555ULL) |
        ((x & 0x5555555555555555ULL) << 1);

    x = ((x >> 2)  & 0x3333333333333333ULL) |
        ((x & 0x3333333333333333ULL) << 2);

    x = ((x >> 4)  & 0x0F0F0F0F0F0F0F0FULL) |
        ((x & 0x0F0F0F0F0F0F0F0FULL) << 4);

    x = ((x >> 8)  & 0x00FF00FF00FF00FFULL) |
        ((x & 0x00FF00FF00FF00FFULL) << 8);

    x = ((x >> 16) & 0x0000FFFF0000FFFFULL) |
        ((x & 0x0000FFFF0000FFFFULL) << 16);

    x = (x >> 32) | (x << 32);

    return x;
}

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

bool add_atomic_task(AtomicThreadpool_t *threadpool, Task_t *task){
    if(threadpool == NULL || task == NULL){
        return false;
    }

    if(atomic_load(&threadpool->shutdown)){
        return false;
    }

    for(size_t i = 0; i < TOTAL_BITMAPS; i++){
        while(true){
            uint64_t occupied = atomic_load(&threadpool->bit_maps_occupied[i]);
            int free_slot_index = first_free_slot(occupied);
            if(free_slot_index == -1){
                break;
            }

            uint64_t mask = UINT64_C(1) << free_slot_index;
            uint64_t old_value = atomic_fetch_or(&threadpool->bit_maps_occupied[i], mask);
            if(old_value & mask){
                // someone else got this slot first
                continue;
            }

            size_t real_task_index = i * BITS_PER_BITMAP + (size_t)free_slot_index;
            threadpool->tasks[real_task_index] = *task;

            // mark available.
            atomic_fetch_or(&threadpool->bit_maps[i], mask);
            atomic_fetch_or(&threadpool->bit_map_map, UINT64_C(1) << i);
            return true;
        }
    }

    return false;
}

bool add_atomic_task_bulk(AtomicThreadpool_t *threadpool, Task_t *tasks, size_t task_count){
    if(threadpool == NULL || tasks == NULL || task_count == 0){
        return false;
    }

    if(atomic_load(&threadpool->shutdown)){
        return false;
    }

    size_t tasks_added = 0;
    while(tasks_added < task_count){
        if(atomic_load(&threadpool->shutdown)){
            return false;
        }

        size_t start_index = atomic_fetch_add(&threadpool->bulk_input_index, 1) % TOTAL_BITMAPS;
        size_t bit_map_index = TOTAL_BITMAPS;
        size_t partial_bit_map_index = TOTAL_BITMAPS;

        // Prefer a completely empty bitmap. If there is not one, remember
        // the first partially free bitmap, starting at a different place
        // for every bulk add.
        for(size_t i = 0; i < TOTAL_BITMAPS; i++){
            size_t index = (start_index + i) % TOTAL_BITMAPS;
            uint64_t occupied = atomic_load(&threadpool->bit_maps_occupied[index]);

            if(occupied == 0){
                bit_map_index = index;
                break;
            }

            if(occupied != UINT64_MAX && partial_bit_map_index == TOTAL_BITMAPS){
                partial_bit_map_index = index;
            }
        }

        if(bit_map_index == TOTAL_BITMAPS){
            bit_map_index = partial_bit_map_index;
        }
        if(bit_map_index == TOTAL_BITMAPS){
            // Every bitmap was full. Workers need to release some slots.
            platform_thread_yield();
            continue;
        }

        // Claim every currently free slot in this bitmap. The old value
        // tells us which bits already belonged to someone else.
        uint64_t old_value = atomic_fetch_or(
            &threadpool->bit_maps_occupied[bit_map_index],
            UINT64_MAX
        );
        uint64_t claimed = ~old_value;
        if(claimed == 0){
            continue;
        }

        uint64_t added_mask = 0;
        while(claimed != 0 && tasks_added < task_count){
            int task_index = first_available(claimed);
            uint64_t mask = UINT64_C(1) << task_index;
            size_t real_task_index = bit_map_index * BITS_PER_BITMAP + (size_t)task_index;

            threadpool->tasks[real_task_index] = tasks[tasks_added];
            tasks_added++;
            added_mask |= mask;
            claimed &= ~mask;
        }

        // Only publish slots that received a task.
        atomic_fetch_or(&threadpool->bit_maps[bit_map_index], added_mask);
        atomic_fetch_or(&threadpool->bit_map_map, UINT64_C(1) << bit_map_index);

        // claimed now contains slots we reserved but did not need.
        if(claimed != 0){
            atomic_fetch_and(&threadpool->bit_maps_occupied[bit_map_index], ~claimed);
        }
    }

    return true;
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
