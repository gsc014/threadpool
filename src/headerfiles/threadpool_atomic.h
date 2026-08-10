#ifndef THREADPOOL_ATOMIC_H
#define THREADPOOL_ATOMIC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "queue.h"
#include "task.h"
#include "condition_atomic.h"
#include "platform_specific/api.h"

#define BITS_PER_BITMAP 64u
#define TOTAL_BITMAPS 16u
#define TOTAL_TASK_CAPACITY (TOTAL_BITMAPS * BITS_PER_BITMAP)

typedef struct AtomicThreadpool AtomicThreadpool_t;

typedef struct{
    size_t id;
    AtomicThreadpool_t *threadpool;
    Platform_thread_t thread;
} AtomicWorker_t;

typedef _Atomic(uint64_t) AtomicBitmap;

struct AtomicThreadpool{
    atomic_bool shutdown;
    AtomicBitmap bit_map_map; // 10010000100, the 1 means the bitmap at that index has available tasks, 0 means no available tasks
    atomic_uint bulk_input_index;
    AtomicBitmap bit_maps_occupied[TOTAL_BITMAPS];
    AtomicBitmap bit_maps[TOTAL_BITMAPS];
    Task_t tasks[TOTAL_TASK_CAPACITY];

    AtomicWorker_t *workers;
    size_t worker_count;
};

bool add_atomic_task(AtomicThreadpool_t *threadpool, Task_t *task);
bool add_atomic_task_bulk(
    AtomicThreadpool_t *threadpool,
    Task_t *tasks,
    size_t task_count
);
void shutdown_atomic_threadpool(AtomicThreadpool_t *threadpool);
AtomicThreadpool_t *init_atomic_threadpool(size_t thread_count);

#endif
