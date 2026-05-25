#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ae.h"

#define PORT 8888
#define BACKLOG 128
#define BUF_SIZE 1024

// Stats tracking structure
typedef struct {
    int active_connections;
    int total_connections;
    long uptime_seconds;
} ServerStats;

// Client state structure
typedef struct {
    int fd;
    char ip[INET_ADDRSTRLEN];
    int port;
    ServerStats *stats;
} ClientState;

int setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 5-second periodic timer callback
long long statsTimerHandler(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    (void)eventLoop;
    (void)id;
    ServerStats *stats = (ServerStats *)clientData;
    stats->uptime_seconds += 5;
    printf("[STATS] Uptime: %lds | Active Connections: %d | Total Connections: %d\n",
           stats->uptime_seconds, stats->active_connections, stats->total_connections);
    fflush(stdout);
    return 5000; // reschedule in 5000ms (5 seconds)
}

// Client read event handler
void readFromClientHandler(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask) {
    (void)mask;
    ClientState *client = (ClientState *)clientData;
    char buffer[BUF_SIZE];
    ssize_t nread;

    nread = read(fd, buffer, sizeof(buffer) - 1);
    if (nread <= 0) {
        if (nread == 0) {
            printf("[INFO] Client %s:%d disconnected\n", client->ip, client->port);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return; // Nothing to read right now
            }
            fprintf(stderr, "[ERROR] Read error from client %s:%d: %s\n",
                    client->ip, client->port, strerror(errno));
        }

        // Clean up client connection
        aeDeleteFileEvent(eventLoop, fd, AE_READABLE);
        close(fd);
        client->stats->active_connections--;
        free(client);
        return;
    }

    buffer[nread] = '\0';
    // Remove newline for printing
    if (nread > 0 && buffer[nread - 1] == '\n') {
        buffer[nread - 1] = '\0';
    }

    printf("[ECHO] Received from %s:%d: \"%s\"\n", client->ip, client->port, buffer);
    fflush(stdout);

    // Echo the message back to client (with original newline if it was stripped)
    char echo_buf[BUF_SIZE + 2];
    int len = snprintf(echo_buf, sizeof(echo_buf), "%s\n", buffer);

    // In a fully robust production server, we would use AE_WRITABLE to handle write blocks.
    // For this classic example, a direct write is standard and keeps the logic clear.
    if (write(fd, echo_buf, len) == -1) {
        fprintf(stderr, "[WARNING] Write failed to client %s:%d: %s\n",
                client->ip, client->port, strerror(errno));
    }
}

// TCP listener accept event handler
void acceptConnectionHandler(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask) {
    (void)mask;
    ServerStats *stats = (ServerStats *)clientData;
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // No pending connections
        }
        fprintf(stderr, "[ERROR] Accept failed: %s\n", strerror(errno));
        return;
    }

    if (setNonBlocking(client_fd) == -1) {
        fprintf(stderr, "[ERROR] Failed to set client socket non-blocking: %s\n", strerror(errno));
        close(client_fd);
        return;
    }

    ClientState *client = malloc(sizeof(ClientState));
    if (client == NULL) {
        fprintf(stderr, "[ERROR] Out of memory allocating client state\n");
        close(client_fd);
        return;
    }

    client->fd = client_fd;
    client->stats = stats;
    inet_ntop(AF_INET, &client_addr.sin_addr, client->ip, sizeof(client->ip));
    client->port = ntohs(client_addr.sin_port);

    printf("[INFO] Accepted connection from %s:%d (fd: %d)\n", client->ip, client->port, client_fd);
    fflush(stdout);

    if (aeCreateFileEvent(eventLoop, client_fd, AE_READABLE, readFromClientHandler, client) == AE_ERR) {
        fprintf(stderr, "[ERROR] Failed to create client file event\n");
        close(client_fd);
        free(client);
        return;
    }

    stats->active_connections++;
    stats->total_connections++;
}

int main(int argc, const char *argv[]) {
    int server_fd;
    struct sockaddr_in server_addr;
    ServerStats stats = {0};
    int port = PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    // Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        fprintf(stderr, "[FATAL] Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    // Set SO_REUSEADDR to avoid "Address already in use" errors
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        fprintf(stderr, "[WARNING] Failed to set SO_REUSEADDR: %s\n", strerror(errno));
    }

    // Bind address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        fprintf(stderr, "[FATAL] Failed to bind to port %d: %s\n", port, strerror(errno));
        close(server_fd);
        return 1;
    }

    // Start listening
    if (listen(server_fd, BACKLOG) == -1) {
        fprintf(stderr, "[FATAL] Failed to listen on port %d: %s\n", port, strerror(errno));
        close(server_fd);
        return 1;
    }

    if (setNonBlocking(server_fd) == -1) {
        fprintf(stderr, "[FATAL] Failed to set server socket non-blocking: %s\n", strerror(errno));
        close(server_fd);
        return 1;
    }

    printf("===================================================\n");
    printf("  fsae Event Loop TCP Echo Server running on port %d\n", port);
    printf("===================================================\n");
    fflush(stdout);

    // Initialize Event Loop
    aeEventLoop *loop = aeCreateEventLoop(1024);
    if (loop == NULL) {
        fprintf(stderr, "[FATAL] Failed to create event loop\n");
        close(server_fd);
        return 1;
    }

    // Register Listener File Event
    if (aeCreateFileEvent(loop, server_fd, AE_READABLE, acceptConnectionHandler, &stats) == AE_ERR) {
        fprintf(stderr, "[FATAL] Failed to register listener file event\n");
        aeDeleteEventLoop(loop);
        close(server_fd);
        return 1;
    }

    // Register Stats Time Event (runs every 5000ms / 5 seconds)
    if (aeCreateTimeEvent(loop, 5000, statsTimerHandler, &stats, NULL) == AE_ERR) {
        fprintf(stderr, "[FATAL] Failed to register stats timer event\n");
        aeDeleteEventLoop(loop);
        close(server_fd);
        return 1;
    }

    // Start Event Loop
    aeMain(loop);

    // Clean up
    aeDeleteEventLoop(loop);
    close(server_fd);
    return 0;
}