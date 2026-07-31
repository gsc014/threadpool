#ifndef CONDITION_H
#define CONDITION_H

#include <stdatomic.h>

// what is the condition? how can it be met?
typedef enum{
    IS_ZERO,
} Condition_states_t;

// When a using multiple threads, maybe multiple threads will be waiting on a condition to be met. This is a condition, it should be used when waiting on something. When the condition is met, the thread that made the condition will continue to run it or will place it in a ready queue.
typedef struct{
    atomic_flag lock;
    Condition_states_t state;
    atomic_int current_value;
} Condition_t;

void condition_check(Condition_t *condition);

#endif
