#include "common.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

static void put32(unsigned char *out, uint32_t value)
{
    out[0] = value >> 24; out[1] = value >> 16;
    out[2] = value >> 8; out[3] = value;
}

static uint32_t get32(const unsigned char *in)
{
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) | in[3];
}

int fi_parse_port(const char *text, uint16_t *port)
{
    unsigned int value = 0;
    if(!*text) return -1;
    for(; *text; ++text) {
        if(*text < '0' || *text > '9') return -1;
        value = value * 10 + (unsigned int)(*text - '0');
        if(value > 65535) return -1;
    }
    if(!value) return -1;
    *port = (uint16_t)value;
    return 0;
}

int fi_socket_timeout(int fd)
{
    struct timeval timeout = { FI_TIMEOUT_SECONDS, 0 };
    if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        return -1;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

int fi_send_all(int fd, const void *data, size_t size)
{
    const unsigned char *position = data;
    while(size) {
        ssize_t count = send(fd, position, size, MSG_NOSIGNAL);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) { if(!count) errno = EPIPE; return -1; }
        position += count; size -= (size_t)count;
    }
    return 0;
}

int fi_recv_all(int fd, void *data, size_t size)
{
    unsigned char *position = data;
    while(size) {
        ssize_t count = recv(fd, position, size, 0);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) { if(!count) errno = ECONNRESET; return -1; }
        position += count; size -= (size_t)count;
    }
    return 0;
}

int fi_send_request(int fd, const char *path)
{
    unsigned char header[16];
    size_t length = strlen(path);
    if(!length || length > FI_MAX_PATH) { errno = EINVAL; return -1; }
    memcpy(header, "FICP0001", 8);
    put32(header + 8, FI_COMMAND_CP); put32(header + 12, (uint32_t)length);
    if(fi_send_all(fd, header, sizeof(header)) < 0) return -1;
    return fi_send_all(fd, path, length);
}

int fi_recv_request(int fd, char path[FI_MAX_PATH + 1])
{
    unsigned char header[16];
    uint32_t length;
    if(fi_recv_all(fd, header, sizeof(header)) < 0) return -1;
    length = get32(header + 12);
    if(memcmp(header, "FICP0001", 8) || get32(header + 8) != FI_COMMAND_CP ||
       !length || length > FI_MAX_PATH) { errno = EPROTO; return -1; }
    if(fi_recv_all(fd, path, length) < 0) return -1;
    if(memchr(path, '\0', length)) { errno = EPROTO; return -1; }
    path[length] = '\0';
    return 0;
}

int fi_send_response(int fd, const struct fi_response *response)
{
    unsigned char header[24];
    memcpy(header, "FIRS0001", 8);
    put32(header + 8, response->status);
    put32(header + 12, (uint32_t)(response->size >> 32));
    put32(header + 16, (uint32_t)response->size);
    put32(header + 20, response->mode);
    return fi_send_all(fd, header, sizeof(header));
}

int fi_recv_response(int fd, struct fi_response *response)
{
    unsigned char header[24];
    if(fi_recv_all(fd, header, sizeof(header)) < 0) return -1;
    if(memcmp(header, "FIRS0001", 8)) { errno = EPROTO; return -1; }
    response->status = get32(header + 8);
    response->size = ((uint64_t)get32(header + 12) << 32) | get32(header + 16);
    response->mode = get32(header + 20);
    if(response->status > FI_IO || response->mode > 0777 ||
       (response->status != FI_OK &&
        (response->size > FI_MAX_ERROR || response->mode != 0))) {
        errno = EPROTO; return -1;
    }
    return 0;
}
