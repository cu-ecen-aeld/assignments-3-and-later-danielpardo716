#include <stdbool.h>
#include <pthread.h>
#include <sys/queue.h>

struct thread_node {
    pthread_t thread_id;
    bool completed;
    int client_fd;
    char* client_ip;
    SLIST_ENTRY(thread_node) entries;
};

void thread_spawn(int client_fd, char* client_ip);
void thread_remove_all();
void thread_cleanup_completed();