#ifndef TASK_H
#define TASK_H

#include "kernel.h"

#define MAX_TASKS 8
#define TASK_STACK_SIZE 1024
#define STACK_CANARY 0xDEADC0DE

typedef enum { TASK_FREE = 0, TASK_READY, TASK_RUNNING, TASK_DEAD } task_state_t;

typedef void (*task_fn_t)(void);

typedef struct {
    u32 saved_esp;
    u32 stack[TASK_STACK_SIZE];
    task_state_t state;
    int id;
    task_fn_t func;
    int quantum;
} task_t;

void task_init(void);
int task_create(task_fn_t func);
void task_yield(void);
void task_exit(void);
int task_count(void);
int current_task_id(void);
int task_is_initialized(void);
extern volatile int atomic_driver;

#endif
