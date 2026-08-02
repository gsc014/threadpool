#ifndef CONDITION_H
#define CONDITION_H

#include <stddef.h>
#include <stdatomic.h>

#include "platform_specific/api.h"
#include "task.h"

// condition, will execute task when counter reaches 0
// TODO, make this lock free at some point.
typedef struct{
  Platform_mutex_t lock;
  size_t remaining;
  Task_t task;
} Condition_t;

int update_condition(Condition_t *condition);
void init_condition(Condition_t *condition, size_t initial_value, Task_t *task);
void reset_condition(Condition_t *condition, size_t new_value);
void destroy_condition(Condition_t *condition);

#endif
