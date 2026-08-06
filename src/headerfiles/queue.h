#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

#include <assert.h>
#include "../platform_specific/api.h"

typedef struct{
    size_t capacity;
    size_t element_size;
    void *elements;

    size_t active_count;
    size_t tail;
    size_t head;

    Platform_mutex_t lock;
} Queue_t;

typedef struct{
    atomic_bool active_queue_index; // which queue is active, 0 or 1.
    atomic_size_t queued_task_count;

    Platform_mutex_t switch_lock;
    Queue_t queues[2];
} Dual_queue_t;

int init_queue(Queue_t *queue, size_t element_size, size_t capacity);
void free_queue(Queue_t *queue);

/* The caller must hold the relevant queue lock while using these operations. */
bool enlarge_queue(Queue_t *queue, size_t new_capacity);
bool enqueue(Queue_t *queue, const void *element);
bool dequeue(Queue_t *queue, void *element, bool from_back); // if from_back is true, will dequeue from the back of the queue, otherwise will dequeue from the front of the queue.
size_t move_queue(Queue_t *source, Queue_t *destination, size_t count); //returns how many elements were moved

void free_dual_queue(Dual_queue_t *dual_queue);
int init_dual_queue(Dual_queue_t *dual_queue, size_t element_size, size_t capacity);

Queue_t *mutex_lock_dual_queue(Dual_queue_t *dual, bool want_active);
Queue_t *mutex_try_lock_active_queue(Dual_queue_t *dual_queue);

#endif
