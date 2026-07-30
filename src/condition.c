#include <stdbool.h>

#include "headerfiles/condition.h"

// condition check, count down, is condition 0? how can we do that? should a condition have a state? then we can check based on the state, maybe the state is is_zero, or something like that. or should we call the function ourself? what is a good plan?
void condition_check(Condition_t *condition){
    // check if condition is met.
    lock condition;

    bool condition_met = false;
    switch(condition.state){
        case IS_ZERO:
            if(condition.current_value == 0){
                condition_met = true;
            }
            break;
        default:
            // this should never happen
    }
    // condition is met, move from waiting queue to ready queue. Or whatever we want to do. Do we want to try to switch to it?
}
