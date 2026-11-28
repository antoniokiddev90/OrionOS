#ifndef IPC_IPC_H
#define IPC_IPC_H
#include <stddef.h>
#include <stdint.h>
#define IPC_MAX_MESSAGE 512
void ipc_init(void);
int64_t ipc_send(uint64_t sender, uint64_t receiver, const void *message, size_t size);
int64_t ipc_receive(uint64_t receiver, uint64_t sender_filter, void *message, size_t capacity, uint64_t *sender_out);
void ipc_process_exit(uint64_t pid);
#endif
