#define _POSIX_C_SOURCE 200112L

#include "lab.h"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

// --- Layer 3 Implementation ---

long socket_read_cb(void *ctx, char *buf, size_t len)
{
    socket_transport_t *transport = (socket_transport_t *)ctx;
    return (!transport || transport->sockfd < 0) ? -1 : (long)recv(transport->sockfd, buf, len, 0);
}

long socket_write_cb(void *ctx, const char *buf, size_t len)
{
    socket_transport_t *transport = (socket_transport_t *)ctx;
    return (!transport || transport->sockfd < 0) ? -1 : (long)send(transport->sockfd, buf, len, 0);
}

int socket_connect(const char *host, const char *port_str)
{
    struct addrinfo hints = {0}, *res = NULL, *p;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;

    int sockfd = -1;
    for (p = res; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

void socket_close(int sockfd)
{
    if (sockfd >= 0) close(sockfd);
}