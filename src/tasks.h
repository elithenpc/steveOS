#ifndef STEVEOS_TASKS_H
#define STEVEOS_TASKS_H

#include <efi.h>

#define STEVEOS_MAX_TASKS 32

typedef enum {
    STEVEOS_TASK_EMPTY = 0,
    STEVEOS_TASK_READY,
    STEVEOS_TASK_RUNNING,
    STEVEOS_TASK_SLEEPING
} STEVEOS_TASK_STATE;

typedef struct {
    UINT32 id;
    STEVEOS_TASK_STATE state;
    UINT64 stack_base;
    UINT64 stack_size;
    UINT64 instruction_pointer;
} STEVEOS_TASK;

EFI_STATUS steveos_tasks_init(void);
EFI_STATUS steveos_task_create(UINT64 entry_point);
UINT32 steveos_task_count(void);
EFI_STATUS steveos_task_get(UINT32 index, STEVEOS_TASK *task);

#endif
