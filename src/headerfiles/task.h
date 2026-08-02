#ifndef TASK_H
#define TASK_H

typedef struct{
    int id;
    void (*task)(void *);
    void *args;
} Task_t;

#endif
