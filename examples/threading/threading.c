#include "threading.h"
#include <unistd.h>
#include <stdlib.h>

void* threadfunc(void* thread_param) {
    struct thread_data* args = (struct thread_data *) thread_param;
    usleep(args->wait_to_obtain_ms * 1000);
    pthread_mutex_lock(args->mutex);
    usleep(args->wait_to_release_ms * 1000);
    pthread_mutex_unlock(args->mutex);
    args->thread_complete_success = true;
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms) {
    struct thread_data* data = malloc(sizeof(struct thread_data));
    if (!data) return false;
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;
    if (pthread_create(thread, NULL, threadfunc, data) != 0) {
        free(data);
        return false;
    }
    return true;
}
