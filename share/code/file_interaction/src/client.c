#include "common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

struct options { const char *ip, *source, *destination; uint16_t port; };
static volatile sig_atomic_t interrupted;
static volatile sig_atomic_t committed;
static volatile sig_atomic_t connection = -1;

static void cancel_copy(int signal_number)
{
    int saved_errno = errno;
    (void)signal_number;
    if(committed) return;
    interrupted = 1;
    if(connection >= 0) { close(connection); connection = -1; }
    errno = saved_errno;
}

static int parse_options(int argc, char **argv, struct options *options)
{
    int command;
    struct in_addr address;
    if(argc != 5 && argc != 6) return -1;
    options->port = FI_DEFAULT_PORT;
    command = argc == 6 ? 3 : 2;
    if(argc == 6 && fi_parse_port(argv[2], &options->port) < 0) return -1;
    if(strcmp(argv[command], "cp") || inet_pton(AF_INET, argv[1], &address) != 1)
        return -1;
    options->ip = argv[1]; options->source = argv[command + 1];
    options->destination = argv[command + 2];
    if(!*options->destination || !*options->source || strlen(options->source) > FI_MAX_PATH)
        return -1;
    return 0;
}

static int connect_server(const struct options *options)
{
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = htons(options->port);
    inet_pton(AF_INET, options->ip, &address.sin_addr);
    if(fi_socket_timeout(fd) < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        int error = errno; close(fd); errno = error; return -1;
    }
    return fd;
}

static int choose_destination(const struct options *options, char *target, size_t capacity,
                              mode_t source_mode, mode_t *mode)
{
    struct stat info;
    const char *filename = strrchr(options->source, '/');
    int length;
    mode_t mask = umask(0);
    umask(mask);
    *mode = source_mode & ~mask;
    filename = filename ? filename + 1 : options->source;
    if(!*filename || !strcmp(filename, ".") || !strcmp(filename, "..")) {
        errno = EINVAL; return -1;
    }
    if(stat(options->destination, &info) == 0 && S_ISDIR(info.st_mode))
        length = snprintf(target, capacity, "%s/%s", options->destination, filename);
    else
        length = snprintf(target, capacity, "%s", options->destination);
    if(length < 0 || (size_t)length >= capacity) { errno = ENAMETOOLONG; return -1; }
    if(lstat(target, &info) == 0) {
        if(!S_ISREG(info.st_mode)) { errno = EINVAL; return -1; }
        *mode = info.st_mode & 0777;
    }
    else if(errno != ENOENT) return -1;
    return 0;
}

static int write_all(int fd, const unsigned char *data, size_t size)
{
    while(size) {
        if(interrupted) { errno = EINTR; return -1; }
        ssize_t count = write(fd, data, size);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) { if(!count) errno = EIO; return -1; }
        data += count; size -= (size_t)count;
    }
    return 0;
}

static int commit_file(const char *temporary, const char *target)
{
    sigset_t blocked, previous;
    int result, error;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT); sigaddset(&blocked, SIGTERM);
    if(sigprocmask(SIG_BLOCK, &blocked, &previous) < 0) return -1;
    if(interrupted) { result = -1; error = EINTR; }
    else {
        result = rename(temporary, target);
        error = errno;
        if(result == 0) committed = 1;
    }
    /* A pending cancellation after a successful commit cannot undo the copy. */
    sigprocmask(SIG_SETMASK, &previous, NULL);
    errno = error;
    return result;
}

static int save_file(int socket_fd, const struct options *options, const struct fi_response *response)
{
    char target[PATH_MAX], temporary[PATH_MAX];
    unsigned char buffer[65536];
    uint64_t remaining = response->size;
    mode_t mode;
    const char *slash;
    int file, error, result = -1, length;
    if(response->size > INT64_MAX) { errno = EFBIG; return -1; }
    if(choose_destination(options, target, sizeof(target), response->mode, &mode) < 0)
        return -1;
    slash = strrchr(target, '/');
    length = snprintf(temporary, sizeof(temporary), "%.*s.file-client-XXXXXX",
                      slash ? (int)(slash - target + 1) : 0, target);
    if(length < 0 || (size_t)length >= sizeof(temporary)) { errno = ENAMETOOLONG; return -1; }
    file = mkstemp(temporary);
    if(file < 0) return -1;
    while(remaining) {
        size_t size = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        if(interrupted) { errno = EINTR; goto done; }
        if(fi_recv_all(socket_fd, buffer, size) < 0 || write_all(file, buffer, size) < 0)
            goto done;
        remaining -= size;
    }
    if(interrupted) { errno = EINTR; goto done; }
    if(fchmod(file, mode) < 0 || fsync(file) < 0) goto done;
    if(close(file) < 0) { file = -1; goto done; }
    file = -1;
    if(interrupted) { errno = EINTR; goto done; }
    if(commit_file(temporary, target) < 0) goto done;
    printf("Copied %s to %s (%" PRIu64 " bytes)\n", options->source, target, response->size);
    result = 0;
done:
    error = errno;
    if(file >= 0) close(file);
    if(result < 0) unlink(temporary);
    errno = error;
    return result;
}

static int copy_remote(int socket_fd, const struct options *options)
{
    struct fi_response response;
    if(fi_send_request(socket_fd, options->source) < 0 || fi_recv_response(socket_fd, &response) < 0) {
        perror("Transfer protocol"); return -1;
    }
    if(response.status != FI_OK) {
        char message[FI_MAX_ERROR + 1];
        if(fi_recv_all(socket_fd, message, (size_t)response.size) < 0) {
            perror("Server response"); return -1;
        }
        message[response.size] = '\0';
        fprintf(stderr, "Server error: %s\n", message); return -1;
    }
    if(save_file(socket_fd, options, &response) < 0) { perror("Copy failed"); return -1; }
    return 0;
}

int main(int argc, char **argv)
{
    struct options options;
    struct sigaction action;
    int socket_fd, result;
    if(parse_options(argc, argv, &options) < 0) {
        fprintf(stderr, "Usage: %s IP [PORT] cp REMOTE_PATH DESTINATION\n", argv[0]); return 1;
    }
    socket_fd = connect_server(&options);
    if(socket_fd < 0) { perror("Connect"); return 1; }
    connection = socket_fd;
    memset(&action, 0, sizeof(action)); action.sa_handler = cancel_copy;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
    result = copy_remote(socket_fd, &options);
    if(connection >= 0) close(connection);
    connection = -1;
    if(interrupted) return 130;
    return result < 0 ? 1 : 0;
}
