#include "threadpool_atomic.h"

static inline int first_free_slot(uint64_t bits){
    uint64_t free_bits = ~bits;

    if(free_bits == 0){
        return -1; // all 64 slots occupied
    }

    return (int)__builtin_ctzll(free_bits);
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
