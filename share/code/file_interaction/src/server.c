#include "common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static int listener = -1;

static void stop_server(int signal_number)
{
    (void)signal_number;
    stopped = 1;
    if(listener >= 0) { close(listener); listener = -1; }
}

static void send_error(int socket_fd, enum fi_status status, const char *message)
{
    struct fi_response response = { status, strlen(message), 0 };
    if(fi_send_response(socket_fd, &response) == 0)
        fi_send_all(socket_fd, message, (size_t)response.size);
}

/* Resolve every component under an open root; never follow a symlink. */
static int open_shared_file(int root_fd, char *path)
{
    char *part, *next, *save;
    int directory, file = -1;
    if(strncmp(path, "SERVER-DIR/", 11) == 0) path += 11;
    if(!*path || *path == '/') { errno = EACCES; return -1; }
    directory = dup(root_fd);
    if(directory < 0) return -1;
    part = strtok_r(path, "/", &save);
    while(part) {
        next = strtok_r(NULL, "/", &save);
        if(strcmp(part, "..") == 0) { errno = EACCES; break; }
        if(strcmp(part, ".") == 0) {
            if(!next) { errno = EISDIR; break; }
            part = next; continue;
        }
        file = openat(directory, part, O_RDONLY | O_CLOEXEC | O_NOFOLLOW |
                      (next ? O_DIRECTORY : O_NONBLOCK));
        if(file < 0) break;
        if(!next) { close(directory); return file; }
        close(directory); directory = file; file = -1; part = next;
    }
    {
        int error = errno;
        close(directory); errno = error;
    }
    return -1;
}

static void handle_copy(int socket_fd, int root_fd)
{
    char path[FI_MAX_PATH + 1];
    unsigned char buffer[65536];
    struct stat info;
    struct fi_response response;
    uint64_t remaining;
    int file;
    if(fi_recv_request(socket_fd, path) < 0) {
        send_error(socket_fd, FI_BAD_REQUEST, "Invalid or incomplete request."); return;
    }
    file = open_shared_file(root_fd, path);
    if(file < 0) {
        if(errno == ENOENT) send_error(socket_fd, FI_NOT_FOUND, "File not found.");
        else if(errno == EACCES || errno == ELOOP || errno == ENOTDIR)
            send_error(socket_fd, FI_ACCESS, "Path is not accessible inside the shared directory.");
        else if(errno == EISDIR) send_error(socket_fd, FI_NOT_FILE, "A regular file is required.");
        else send_error(socket_fd, FI_IO, "Cannot open the source file.");
        return;
    }
    if(fstat(file, &info) < 0 || info.st_size < 0) {
        send_error(socket_fd, FI_IO, "Cannot read file metadata."); close(file); return;
    }
    if(!S_ISREG(info.st_mode)) {
        send_error(socket_fd, FI_NOT_FILE, "A regular file is required."); close(file); return;
    }
    response.status = FI_OK; response.size = (uint64_t)info.st_size;
    response.mode = (uint32_t)(info.st_mode & 0777);
    remaining = response.size;
    if(fi_send_response(socket_fd, &response) < 0) { close(file); return; }
    while(remaining) {
        size_t length = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        ssize_t count = read(file, buffer, length);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0 || fi_send_all(socket_fd, buffer, (size_t)count) < 0) break;
        remaining -= (uint64_t)count;
    }
    close(file);
}

static int listen_tcp(uint16_t port)
{
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_STREAM, 0), reuse = 1;
    if(fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
       bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(fd, 16) < 0) {
        int error = errno; close(fd); errno = error; return -1;
    }
    return fd;
}

static void accept_clients(int root_fd)
{
    while(!stopped) {
        int socket_fd = accept(listener, NULL, NULL);
        pid_t child;
        if(socket_fd < 0) {
            if(stopped) break;
            if(errno == EINTR) continue;
            perror("accept"); break;
        }
        if(fi_socket_timeout(socket_fd) < 0) { close(socket_fd); continue; }
        child = fork();
        if(child == 0) {
            close(listener);
            signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
            handle_copy(socket_fd, root_fd);
            close(socket_fd); close(root_fd); _exit(0);
        }
        if(child < 0) send_error(socket_fd, FI_IO, "Server cannot create a worker.");
        close(socket_fd);
    }
}

int main(int argc, char **argv)
{
    uint16_t port = FI_DEFAULT_PORT;
    struct sigaction action;
    int root_fd;
    if(argc < 2 || argc > 3 || (argc == 3 && fi_parse_port(argv[2], &port) < 0)) {
        fprintf(stderr, "Usage: %s DIRECTORY [PORT]\n", argv[0]); return 1;
    }
    root_fd = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(root_fd < 0) { perror("Shared directory"); return 1; }
    listener = listen_tcp(port);
    if(listener < 0) { perror("Listen"); close(root_fd); return 1; }
    memset(&action, 0, sizeof(action)); action.sa_handler = stop_server;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
    signal(SIGCHLD, SIG_IGN);
    printf("Serving %s on 0.0.0.0:%u\n", argv[1], (unsigned int)port); fflush(stdout);
    accept_clients(root_fd);
    if(listener >= 0) close(listener);
    close(root_fd);
    return stopped ? 0 : 1;
}
