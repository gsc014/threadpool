#ifndef CONDITION_ATOMIC_H
#define CONDITION_ATOMIC_H

#include <stddef.h>
#include <stdatomic.h>

#include "task.h"

/*
 * A countdown that executes task exactly once when remaining reaches zero.
 *
 * update_atomic_condition() may be called concurrently. Initialization must
 * happen before the condition is published to those callers. Reset and
 * destroy require that no update is running concurrently; an update does not
 * carry a generation identifier and therefore cannot be assigned safely to a
 * condition that is being reset at the same time.
 */
typedef struct{
    atomic_size_t remaining;
    Task_t task;
} Condition_atomic_t;

/*
 * Returns 1 when this call executes the completion task, 0 after a normal
 * decrement, and -1 when the condition is invalid or already complete.
 */
int update_atomic_condition(Condition_atomic_t *condition);

void init_atomic_condition(
    Condition_atomic_t *condition,
    size_t initial_value,
    const Task_t *task
);

void reset_atomic_condition(Condition_atomic_t *condition, size_t new_value);
void destroy_atomic_condition(Condition_atomic_t *condition);

#endif
