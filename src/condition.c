#include "headerfiles/condition.h"

/* maybe multiple workers are working in parrallel, and all are working until a condition is met.
example: you want to calculate 16 parts of a screen before you send it to be viewed by a user.
use a condition to make sure that what needs to be done gets done when the condition is met*/

// will decrement counter, once counter reaches 0, will execute task.
// will return 1 if task was executed, 0 if the condition has not been met
// yet and -1 if the condition has already been met and executed.
int update_condition(Condition_t *condition){
    if(condition == NULL || condition->task.task == NULL) {
        return -1;
    }

    size_t old_value = atomic_load(&condition->remaining);

    for(;;){
        if(old_value == 0){
            return -1;
        }

        if(atomic_compare_exchange_weak(
            &condition->remaining,
            &old_value,
            old_value - 1
        )){
            break;
        }
    }

    if (old_value == 1) {
        condition->task.task(condition->task.args);
        return 1;
    }

    return 0;
}
void init_condition(Condition_t *condition, size_t initial_value, Task_t task){
    if(condition == NULL || task.task == NULL){
        return;
    }

    atomic_init(&condition->remaining, initial_value);
    condition->task = task;
    return;
}

void reset_condition(Condition_t *condition, size_t new_value){
    if(condition == NULL){
        return;
    }

    atomic_store(&condition->remaining, new_value);
    return;
}
