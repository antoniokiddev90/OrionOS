#include "ipc.h"
#include "../sched/task.h"
#define IPC_QUEUE_SLOTS 32
struct ipc_message {
    uint64_t sender;
    uint64_t receiver;
    uint32_t size;
    uint32_t used;
    uint8_t data[IPC_MAX_MESSAGE];
};
static struct ipc_message messages[IPC_QUEUE_SLOTS];
void ipc_init(void) {
    for (size_t i = 0; i < IPC_QUEUE_SLOTS; ++i) messages[i].used = 0;
}
static int process_exists(uint64_t pid) {
    struct task *current = task_current();
    if (!current || !pid) return 0;
    if (current->pid == pid && current->state != TASK_DEAD) return 1;
    struct task *start = current;
    struct task *task = current->next;
    while (task && task != start) {
        if (task->pid == pid && task->state != TASK_DEAD) return 1;
        task = task->next;
    }
    return 0;
}
int64_t ipc_send(uint64_t sender, uint64_t receiver, const void *message, size_t size) {
    if (!sender || !receiver || !message || !size || size > IPC_MAX_MESSAGE || !process_exists(receiver)) return -1;
    for (size_t i = 0; i < IPC_QUEUE_SLOTS; ++i) {
        if (messages[i].used) continue;
        messages[i].sender = sender;
        messages[i].receiver = receiver;
        messages[i].size = (uint32_t)size;
        const uint8_t *source = (const uint8_t *)message;
        for (size_t j = 0; j < size; ++j) messages[i].data[j] = source[j];
        messages[i].used = 1;
        return (int64_t)size;
    }
    return -1;
}
static int receive_one(uint64_t receiver, uint64_t sender_filter, void *message, size_t capacity, uint64_t *sender_out) {
    for (size_t i = 0; i < IPC_QUEUE_SLOTS; ++i) {
        struct ipc_message *item = &messages[i];
        if (!item->used || item->receiver != receiver || (sender_filter && item->sender != sender_filter)) continue;
        if (item->size > capacity) return -1;
        uint8_t *destination = (uint8_t *)message;
        for (size_t j = 0; j < item->size; ++j) destination[j] = item->data[j];
        if (sender_out) *sender_out = item->sender;
        int64_t size = item->size;
        item->used = 0;
        return (int)size;
    }
    return 0;
}
int64_t ipc_receive(uint64_t receiver, uint64_t sender_filter, void *message, size_t capacity, uint64_t *sender_out) {
    if (!receiver || !message || !capacity) return -1;
    for (;;) {
        int result = receive_one(receiver, sender_filter, message, capacity, sender_out);
        if (result != 0) return result;
        __asm__ volatile("sti; hlt; cli" : : : "memory");
    }
}
void ipc_process_exit(uint64_t pid) {
    for (size_t i = 0; i < IPC_QUEUE_SLOTS; ++i) {
        if (messages[i].used && messages[i].receiver == pid) messages[i].used = 0;
    }
}
