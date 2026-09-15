#include <efi.h>
#include <efilib.h>
#include "tasks.h"

static STEVEOS_TASK tasks[STEVEOS_MAX_TASKS];
static UINT32 task_count_value;

EFI_STATUS steveos_tasks_init(void) {
    SetMem(tasks, sizeof(tasks), 0);
    task_count_value = 0;
    return EFI_SUCCESS;
}

EFI_STATUS steveos_task_create(UINT64 entry_point) {
    if (task_count_value >= STEVEOS_MAX_TASKS || entry_point == 0)
        return EFI_OUT_OF_RESOURCES;

    STEVEOS_TASK *task = &tasks[task_count_value];
    task->id = task_count_value + 1;
    task->state = STEVEOS_TASK_READY;
    task->instruction_pointer = entry_point;
    task->stack_base = 0;
    task->stack_size = 0;
    task_count_value++;
    return EFI_SUCCESS;
}

UINT32 steveos_task_count(void) {
    return task_count_value;
}
