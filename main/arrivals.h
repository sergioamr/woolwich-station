#ifndef ARRIVALS_H
#define ARRIVALS_H

#include <stddef.h>

#define ARRIVALS_MAX 12

typedef struct {
	char destination[32];
	char line[16];
	char platform[12];
	int ttl_sec;
} arrival_t;

/* Fetch arrivals from API. Returns count (0 on error). Fills arr[].
 * If time_str non-NULL and len>=6, writes "HH:MM" from API "updated" (UTC). */
int arrivals_fetch(arrival_t *arr, size_t max_count, char *time_str, size_t time_str_len);

#endif
