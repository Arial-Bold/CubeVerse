/* netio.h - DNS, TCP connections with timeouts and an SNTP clock, for the
 * update check (the adapter is brought up by net.c). GameCube only. */
#pragma once
#include <gccore.h>
#include <stdbool.h>
#include <time.h>

bool netio_ready(void);                             /* the network is up */
int  netio_resolve(const char *host, u32 *ip_be);   /* 0 on success */
int  netio_connect(u32 ip_be, u16 port);            /* socket or <0 */
int  netio_wait(int fd, bool for_write, int timeout_ms);  /* 1 ready, 0 timeout, <0 error */
void netio_close(int fd);

bool   netio_sync_time(void);           /* SNTP */
bool   netio_time_synced(void);
time_t netio_now(void);                 /* UTC unix time */
