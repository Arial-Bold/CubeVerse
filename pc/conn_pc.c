/* conn_pc.c - the PC test build's stand-in for conn.c: plain TCP to a local
 * test server (CV_TEST_SERVER, default 127.0.0.1:8099) whatever the host;
 * no TLS. Lets the HTTP client and Juxt code run against saved pages. */
#include "conn.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool tls_global_init(char *err, size_t errlen) { (void)err; (void)errlen; return true; }
bool tls_ca_loaded(void) { return true; }
int tls_ca_count(void) { return 1; }

bool conn_open(Conn *c, const char *host, unsigned short port, bool tls, volatile int *cancel,
               void (*stage)(const char *), char *err, size_t errlen) {
    (void)port; (void)tls; (void)stage;
    memset(c, 0, sizeof(*c));
    c->cancel = cancel;
    const char *srv = getenv("CV_TEST_SERVER");
    int tport = 8099;
    if (srv && strchr(srv, ':')) tport = atoi(strchr(srv, ':') + 1);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)tport);
    a.sin_addr.s_addr = htonl(0x7f000001);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        snprintf(err, errlen, "Couldn't connect to %s (test server).", host);
        return false;
    }
    c->fd = fd;
    c->tls = tls;
    return true;
}
bool conn_write_all(Conn *c, const void *data, size_t len) {
    const char *p = data;
    while (len) { ssize_t k = write(c->fd, p, len); if (k <= 0) return false; p += k; len -= (size_t)k; }
    return true;
}
int conn_read(Conn *c, void *buf, size_t len) { return (int)read(c->fd, buf, len); }
void conn_close(Conn *c) { if (c->fd >= 0) close(c->fd); c->fd = -1; }
