#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"

int server_fd = -1;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t keep_running = 1;

struct thread_node {
    pthread_t thread_id;
    int client_fd;
    char client_ip[INET_ADDRSTRLEN];
    volatile int complete;
    SLIST_ENTRY(thread_node) entries;
};

SLIST_HEAD(thread_list, thread_node) head = SLIST_HEAD_INITIALIZER(head);

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        keep_running = 0;
        if (server_fd != -1) {
            close(server_fd);
        }
    }
}

void* handle_client(void* arg) {
    struct thread_node* node = (struct thread_node*)arg;
    char buffer[1024];
    ssize_t bytes_recv;
    
    FILE *fp = NULL;
    
    while ((bytes_recv = recv(node->client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_recv] = '\0';
        
        pthread_mutex_lock(&file_mutex);
        fp = fopen(DATA_FILE, "a+");
        if (fp) {
            fputs(buffer, fp);
            fclose(fp);
        }
        pthread_mutex_unlock(&file_mutex);
        
        if (strchr(buffer, '\n')) {
            break;
        }
    }
    
    pthread_mutex_lock(&file_mutex);
    fp = fopen(DATA_FILE, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            send(node->client_fd, buffer, strlen(buffer), 0);
        }
        fclose(fp);
    }
    pthread_mutex_unlock(&file_mutex);
    
    close(node->client_fd);
    syslog(LOG_INFO, "Closed connection from %s", node->client_ip);
    node->complete = 1;
    return NULL;
}

void* timer_thread(void* arg) {
    while (keep_running) {
        sleep(10);
        time_t rawtime;
        struct tm *info;
        char buffer[80];
        
        time(&rawtime);
        info = localtime(&rawtime);
        strftime(buffer, sizeof(buffer), "timestamp:%a, %d %b %Y %T %z\n", info);
        
        pthread_mutex_lock(&file_mutex);
        FILE *fp = fopen(DATA_FILE, "a");
        if (fp) {
            fputs(buffer, fp);
            fclose(fp);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);
    
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Error creating socket");
        return -1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        syslog(LOG_ERR, "Error binding socket");
        close(server_fd);
        return -1;
    }
    
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        if (fork() > 0) {
            return 0;
        }
        setsid();
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Error listening");
        close(server_fd);
        return -1;
    }
    
    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, timer_thread, NULL);
    
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            continue;
        }
        
        struct thread_node* node = malloc(sizeof(struct thread_node));
        node->client_fd = client_fd;
        node->complete = 0;
        inet_ntop(AF_INET, &client_addr.sin_addr, node->client_ip, sizeof(node->client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", node->client_ip);
        
        pthread_create(&node->thread_id, NULL, handle_client, node);
        SLIST_INSERT_HEAD(&head, node, entries);
        
        // Clean up complete threads
        struct thread_node* temp;
        struct thread_node* prev = NULL;
        struct thread_node* curr = SLIST_FIRST(&head);
        while (curr != NULL) {
            if (curr->complete) {
                pthread_join(curr->thread_id, NULL);
                temp = curr;
                if (prev == NULL) {
                    head.slh_first = SLIST_NEXT(curr, entries);
                } else {
                    SLIST_NEXT(prev, entries) = SLIST_NEXT(curr, entries);
                }
                curr = SLIST_NEXT(curr, entries);
                free(temp);
            } else {
                prev = curr;
                curr = SLIST_NEXT(curr, entries);
            }
        }
    }
    
    // Cleanup remaining threads
    while (!SLIST_EMPTY(&head)) {
        struct thread_node* node = SLIST_FIRST(&head);
        SLIST_REMOVE_HEAD(&head, entries);
        pthread_join(node->thread_id, NULL);
        free(node);
    }
    
    pthread_join(timer_tid, NULL);
    remove(DATA_FILE);
    closelog();
    return 0;
}
