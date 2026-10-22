#ifndef FILE_INTERACTION_COMMON_H
#define FILE_INTERACTION_COMMON_H

#include <stddef.h>
#include <stdint.h>

#define FI_DEFAULT_PORT 9000
#define FI_MAX_PATH 4096
#define FI_MAX_ERROR 1024
#define FI_TIMEOUT_SECONDS 30
#define FI_COMMAND_CP 1

enum fi_status { FI_OK, FI_BAD_REQUEST, FI_NOT_FOUND, FI_ACCESS, FI_NOT_FILE, FI_IO };
struct fi_response { uint32_t status; uint64_t size; uint32_t mode; };

int fi_parse_port(const char *text, uint16_t *port);
int fi_socket_timeout(int fd);
int fi_send_all(int fd, const void *data, size_t size);
int fi_recv_all(int fd, void *data, size_t size);
int fi_send_request(int fd, const char *path);
int fi_recv_request(int fd, char path[FI_MAX_PATH + 1]);
int fi_send_response(int fd, const struct fi_response *response);
int fi_recv_response(int fd, struct fi_response *response);

#endif
