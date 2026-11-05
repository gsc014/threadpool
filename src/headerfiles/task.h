#ifndef TASK_H
#define TASK_H

typedef struct{
    void (*task)(void *);
    void *args;
} Task_t;

#endif
