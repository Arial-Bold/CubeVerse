/* net.h - bringing up the Broadband Adapter (or a WIZnet adapter such as
 * the ETH2GC, through libogc2), and the little web server a phone uses to
 * sign in (the QR code on the sign-in screen). Everything runs on its own
 * thread; the screens only read the state below. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { NET_OFF, NET_STARTING, NET_UP, NET_FAILED } NetState;

void net_start(void);            /* starts the network and the server (once) */
NetState net_state(void);
const char *net_ip(void);        /* "192.168.1.50" when up */
const char *net_gateway(void);
const char *net_message(void);   /* status or problem, for the screen */
const char *net_url(void);       /* the phone sign-in page, "http://192.168.1.50/" */
bool net_server_ready(void);
/* A sign-in sent from the phone (once). */
bool net_take_signin(char *user, size_t ucap, char *pass, size_t pcap);
