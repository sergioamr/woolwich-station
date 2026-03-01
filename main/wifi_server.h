#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include <stddef.h>

void wifi_server_init(const char *ssid, const char *password);
void wifi_server_wait_connected(void);
int wifi_server_wait_connected_timeout(uint32_t ms);  /* returns 1=ok, 0=timeout */
void wifi_server_reconnect(void);  /* trigger reconnect attempt */
void wifi_server_start_http(void);
void wifi_server_init_sntp(void);
void wifi_server_print_ip(void);
int wifi_server_get_ip(char *buf, size_t len);

#endif
