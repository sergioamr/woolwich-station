/*
 * Fetch train arrivals from https://engineer.blue/api/arrivals
 */
#include "arrivals.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <stdio.h>
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#define ARRIVALS_URL "https://engineer.blue/api/arrivals"
#define BUF_SIZE 4096

static const char *TAG = "arrivals";

typedef struct { char *buf; int len; int max; } fetch_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
	if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data) return ESP_OK;
	fetch_ctx_t *ctx = (fetch_ctx_t *)evt->user_data;
	if (ctx->len + evt->data_len <= ctx->max) {
		memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
		ctx->len += evt->data_len;
	}
	return ESP_OK;
}

int arrivals_fetch(arrival_t *arr, size_t max_count, char *time_str, size_t time_str_len)
{
	if (!arr || max_count == 0) return 0;

	char *buf = malloc(BUF_SIZE);
	if (!buf) return 0;
	memset(buf, 0, BUF_SIZE);

	fetch_ctx_t ctx = { .buf = buf, .len = 0, .max = BUF_SIZE - 1 };
	esp_http_client_config_t cfg = {
		.url = ARRIVALS_URL,
		.event_handler = http_event_handler,
		.user_data = &ctx,
		.timeout_ms = 10000,
		.skip_cert_common_name_check = true,
	};

	esp_http_client_handle_t client = esp_http_client_init(&cfg);
	if (!client) {
		free(buf);
		return 0;
	}

	esp_err_t err = esp_http_client_perform(client);
	int count = 0;

	if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
		buf[ctx.len] = '\0';
		cJSON *root = cJSON_Parse(buf);
		if (root) {
			/* Parse "updated" (e.g. "2026-03-01T18:43:51Z") -> "HH:MM" UK time */
			if (time_str && time_str_len >= 6) {
				cJSON *updated = cJSON_GetObjectItem(root, "updated");
				if (updated && updated->valuestring) {
					const char *s = updated->valuestring;
					if (strlen(s) >= 16) {
						int uh = (s[11]-'0')*10 + (s[12]-'0');
						int um = (s[14]-'0')*10 + (s[15]-'0');
						int mo = (s[5]-'0')*10 + (s[6]-'0');
						int dy = (s[8]-'0')*10 + (s[9]-'0');
						/* BST: last Sun Mar 01:00 UTC - last Sun Oct 01:00 UTC */
						int bst = 0;
						if (mo > 3 && mo < 10) bst = 1;
						else if (mo == 3 && (dy > 29 || (dy == 29 && uh >= 1))) bst = 1;
						else if (mo == 10 && (dy < 25 || (dy == 25 && uh < 1))) bst = 1;
						if (bst) { uh += 1; if (uh >= 24) uh -= 24; }
						snprintf(time_str, time_str_len, "%02d:%02d", uh, um);
					} else {
						time_str[0] = '\0';
					}
				} else {
					time_str[0] = '\0';
				}
			}
			cJSON *arrivals = cJSON_GetObjectItem(root, "arrivals");
			if (cJSON_IsArray(arrivals)) {
				int n = cJSON_GetArraySize(arrivals);
				for (int i = 0; i < n && (size_t)count < max_count; i++) {
					cJSON *item = cJSON_GetArrayItem(arrivals, i);
					if (!item) continue;

					cJSON *dest = cJSON_GetObjectItem(item, "destination");
					cJSON *line = cJSON_GetObjectItem(item, "line");
					cJSON *plat = cJSON_GetObjectItem(item, "platform");
					cJSON *ttl = cJSON_GetObjectItem(item, "ttl");

					if (dest && dest->valuestring)
						strncpy(arr[count].destination, dest->valuestring, sizeof(arr[count].destination) - 1);
					else
						arr[count].destination[0] = '\0';

					if (line && line->valuestring)
						strncpy(arr[count].line, line->valuestring, sizeof(arr[count].line) - 1);
					else
						arr[count].line[0] = '\0';

					if (plat && plat->valuestring)
						strncpy(arr[count].platform, plat->valuestring, sizeof(arr[count].platform) - 1);
					else
						arr[count].platform[0] = '\0';

					arr[count].ttl_sec = (ttl && cJSON_IsNumber(ttl)) ? (int)ttl->valuedouble : 0;
					count++;
				}
			}
			cJSON_Delete(root);
		} else {
			ESP_LOGE(TAG, "JSON parse failed");
		}
	} else {
		ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
	}

	esp_http_client_cleanup(client);
	free(buf);
	return count;
}
