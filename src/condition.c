#include "headerfiles/condition.h"

/* maybe multiple workers are working in parrallel, and all are working until a condition is met.
example: you want to calculate 16 parts of a screen before you send it to be viewed by a user.
use a condition to make sure that what needs to be done gets done when the condition is met*/

// will decrement counter, once counter reaches 0, will execute task.
// will return 1 if task was executed, 0 if the condition has not been met
// yet and -1 if the condition has already been met and executed.

int update_condition(Condition_t *condition){
    if(condition == NULL || condition->task.task == NULL){
        return -1;
    }

    // bool should_execute_task = false;
    platform_mutex_lock(&condition->lock);
    if(condition->remaining == 1){ // if this is the last one, we will execute the task.
        // should_execute_task = true;
        Task_t task = condition->task;
        condition->remaining = 0;
        platform_mutex_unlock(&condition->lock);
        task.task(task.args);
        return 1;
    }
    else if(condition->remaining == 0){ // if the condition has already been met, we will not execute the task again.
        platform_mutex_unlock(&condition->lock);
        return -1;
    }

    condition->remaining--;
    platform_mutex_unlock(&condition->lock);
    return 0;
}

void init_condition(Condition_t *condition, size_t initial_value, Task_t *task){
    if(condition == NULL || task == NULL || task->task == NULL || initial_value == 0){
        return;
    }
    if(platform_mutex_init(&condition->lock) != 0){
        return;
    }
    condition->remaining = initial_value;
    condition->task = *task;
    return;
}

void reset_condition(Condition_t *condition, size_t new_value){
    if(condition == NULL){
        return;
    }

    platform_mutex_lock(&condition->lock);
    condition->remaining = new_value;
    platform_mutex_unlock(&condition->lock);
    return;
}

void destroy_condition(Condition_t *condition){
    if(condition == NULL){
        return;
    }

    platform_mutex_destroy(&condition->lock);
    condition->remaining = 0;
    condition->task.task = NULL;
    condition->task.args = NULL;
    return;
}