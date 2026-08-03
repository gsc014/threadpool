#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

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
bool enlarge_queue(Queue_t *queue);
bool enqueue(Queue_t *queue, const void *element);
bool dequeue(Queue_t *queue, void *element);
bool move_queue(Queue_t *source, Queue_t *destination, size_t count);

void free_dual_queue(Dual_queue_t *dual_queue);
int init_dual_queue(Dual_queue_t *dual_queue, size_t element_size, size_t capacity);
void enqueue_dual(Dual_queue_t *dual_queue, const void *element);
void dequeue_dual(Dual_queue_t *dual_queue, void *element);
void move_queue_dual(Dual_queue_t *source, Dual_queue_t *destination, size_t count);
void switch_active_queue(Dual_queue_t *dual_queue);
void move_queue_dual_to_single(Dual_queue_t *source, Queue_t *destination, size_t count, bool from_active_queue);


#endif
