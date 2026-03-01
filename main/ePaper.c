/* ePaper demo
 *
 *  Author: LoBo (loboris@gmail.com, loboris.github)
*/

#include <time.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "spiffs_vfs.h"
#include "esp_log.h"

#include "spi_master_lobo.h"
#include "img1.h"
#include "img2.h"
#include "img3.h"
#include "img_hacking.c"
#include "EPD.h"
#ifdef CONFIG_EPD_MODULE_B
#include "EPD_2in9b.h"
#include "font_8x8.h"
#include "arrivals.h"
#endif

#ifdef CONFIG_EXAMPLE_USE_WIFI
#include "wifi_server.h"
#include "nvs_flash.h"
#include <sys/time.h>
#include <unistd.h>
#endif

#define DELAYTIME 1500



static struct tm* tm_info;
static char tmp_buff[128];
static time_t time_now, time_last = 0;
#if defined(CONFIG_EPD_MODULE_B) && defined(CONFIG_EXAMPLE_USE_WIFI)
static int wifi_retry_count = 0;
#endif
static const char *file_fonts[3] = {"/spiffs/fonts/DotMatrix_M.fon", "/spiffs/fonts/Ubuntu.fon", "/spiffs/fonts/Grotesk24x48.fon"};
static const char tag[] = "[Eink Demo]";

//==================================================================================
#ifdef CONFIG_EXAMPLE_USE_WIFI

static int obtain_time(void)
{
    int res = 1;
    wifi_server_init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    wifi_server_wait_connected();
    wifi_server_print_ip();
    wifi_server_init_sntp();

    int retry = 0;
    const int retry_count = 20;
    time(&time_now);
    tm_info = localtime(&time_now);

    while (tm_info->tm_year < (2016 - 1900) && ++retry < retry_count) {
        vTaskDelay(500 / portTICK_RATE_MS);
        time(&time_now);
        tm_info = localtime(&time_now);
    }
    if (tm_info->tm_year < (2016 - 1900)) {
        ESP_LOGI(tag, "System time NOT set.");
        res = 0;
    } else {
        ESP_LOGI(tag, "System time is set.");
    }
    return res;
}

#endif  //CONFIG_EXAMPLE_USE_WIFI
//==================================================================================


//=============
void app_main()
{

    // ========  PREPARE DISPLAY INITIALIZATION  =========

    esp_err_t ret;

	disp_buffer = heap_caps_malloc(EPD_DISPLAY_WIDTH * (EPD_DISPLAY_HEIGHT/8), MALLOC_CAP_DMA);
	assert(disp_buffer);
	drawBuff = disp_buffer;

	gs_disp_buffer = heap_caps_malloc(EPD_DISPLAY_WIDTH * EPD_DISPLAY_HEIGHT, MALLOC_CAP_DMA);
	assert(gs_disp_buffer);
	gs_drawBuff = gs_disp_buffer;

	// ====  CONFIGURE SPI DEVICES(s)  ====================================================================================

	gpio_set_direction(DC_Pin, GPIO_MODE_OUTPUT);
	gpio_set_level(DC_Pin, 1);
	gpio_set_direction(RST_Pin, GPIO_MODE_OUTPUT);
	gpio_set_level(RST_Pin, 0);
	gpio_set_direction(BUSY_Pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUSY_Pin, GPIO_PULLUP_ONLY);

#if POWER_Pin
	gpio_set_direction(POWER_Pin, GPIO_MODE_OUTPUT);
	gpio_set_level(POWER_Pin, 1);
#endif

    spi_lobo_bus_config_t buscfg={
        .miso_io_num = -1,				// set SPI MISO pin
        .mosi_io_num = MOSI_Pin,		// set SPI MOSI pin
        .sclk_io_num = SCK_Pin,			// set SPI CLK pin
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
		.max_transfer_sz = 5*1024,		// max transfer size is 4736 bytes
    };
    spi_lobo_device_interface_config_t devcfg={
#ifdef CONFIG_EPD_MODULE_B
        .clock_speed_hz=4000000,		// Module B: 4 MHz for stability (40 MHz can cause blobs)
#else
        .clock_speed_hz=40000000,		// SPI clock: try 4000000 if display fails
#endif
        .mode=0,						// SPI mode 0; try 1 or 3 if display fails
        .spics_io_num=-1,				// we will use external CS pin
		.spics_ext_io_num = CS_Pin,		// external CS pin
		.flags=SPI_DEVICE_HALFDUPLEX,	// ALWAYS SET  to HALF DUPLEX MODE for display spi !!
    };

    // ====================================================================================================================


	vTaskDelay(500 / portTICK_RATE_MS);
	printf("\r\n=================================\r\n");
    printf("ePaper display DEMO, LoBo 06/2017, build " __DATE__ " " __TIME__ "\r\n");
	printf("=================================\r\n\r\n");

	// ==================================================================
	// ==== Initialize the SPI bus and attach the EPD to the SPI bus ====

	ret=spi_lobo_bus_add_device(SPI_BUS, &buscfg, &devcfg, &disp_spi);
    assert(ret==ESP_OK);
	printf("SPI: display device added to spi bus\r\n");

	// ==== Test select/deselect ====
	ret = spi_lobo_device_select(disp_spi, 1);
    assert(ret==ESP_OK);
	ret = spi_lobo_device_deselect(disp_spi);
    assert(ret==ESP_OK);

	printf("SPI: attached display device, speed=%u\r\n", spi_lobo_get_speed(disp_spi));
	printf("SPI: bus uses native pins: %s\r\n", spi_lobo_uses_native_pins(disp_spi) ? "true" : "false");

	printf("\r\n-------------------\r\n");
	printf("ePaper demo started\r\n");
	printf("-------------------\r\n");


	EPD_DisplayClearFull();

#ifdef CONFIG_EPD_MODULE_B
	/* Show "Booting" immediately so user sees activity */
	{
		const int W = EPD_2IN9B_WIDTH;
		const int H = EPD_2IN9B_HEIGHT;
		const int BUF_SIZE = (W / 8) * H;
		const int CW = FONT_8X8_CHAR_W + 1;
		uint8_t *bb = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
		uint8_t *rb = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
		if (bb && rb) {
			memset(bb, 0xFF, BUF_SIZE);
#ifdef CONFIG_EPD_2IN9B_V4
			memset(rb, 0x00, BUF_SIZE);
#else
			memset(rb, 0xFF, BUF_SIZE);
#endif
			/* Draw "Booting" centered */
			const char *msg = "Booting";
			int mw = 7 * CW;
			int tx = (W - mw) / 2;
			int ty = (H - FONT_8X8_CHAR_H) / 2;
			for (const char *s = msg; *s; s++, tx += CW) {
				unsigned char ch = (unsigned char)*s;
				if (ch < FONT_8X8_FIRST || ch >= FONT_8X8_FIRST + FONT_8X8_COUNT) continue;
				const uint8_t *g = font_8x8[ch - FONT_8X8_FIRST];
				for (int r = 0; r < FONT_8X8_CHAR_H; r++)
					for (int c = 0; c < FONT_8X8_CHAR_W; c++)
						if (g[r] & (0x80 >> c)) {
							int px = tx + c, py = ty + r;
							if (px >= 0 && px < W && py >= 0 && py < H) {
								int bi = (py * (W/8)) + (px/8);
								bb[bi] &= ~(0x80 >> (px % 8));
							}
						}
			}
			EPD_2IN9B_Display(bb, rb);
			heap_caps_free(bb);
			heap_caps_free(rb);
		}
	}
#endif

#if defined(CONFIG_EPD_MODULE_B) && defined(CONFIG_EXAMPLE_USE_WIFI)
	/* WiFi + Module B: connect (30s timeout), NTP, print IP */
	ESP_ERROR_CHECK(nvs_flash_init());
	wifi_server_init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
	if (!wifi_server_wait_connected_timeout(30000)) {
		/* Connection failed - show "No WiFi N" */
		wifi_retry_count++;
		{
			const int W = EPD_2IN9B_WIDTH;
			const int H = EPD_2IN9B_HEIGHT;
			const int BUF_SIZE = (W / 8) * H;
			const int CW = FONT_8X8_CHAR_W + 1;
			uint8_t *bb = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
			uint8_t *rb = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
			if (bb && rb) {
				memset(bb, 0xFF, BUF_SIZE);
#ifdef CONFIG_EPD_2IN9B_V4
				memset(rb, 0x00, BUF_SIZE);
#else
				memset(rb, 0xFF, BUF_SIZE);
#endif
				char msg[24];
				snprintf(msg, sizeof(msg), "No WiFi %d", wifi_retry_count);
				int mw = (int)strlen(msg) * CW;
				int tx = (W - mw) / 2;
				int ty = (H - FONT_8X8_CHAR_H) / 2;
				for (const char *s = msg; *s; s++, tx += CW) {
					unsigned char ch = (unsigned char)*s;
					if (ch < FONT_8X8_FIRST || ch >= FONT_8X8_FIRST + FONT_8X8_COUNT) continue;
					const uint8_t *g = font_8x8[ch - FONT_8X8_FIRST];
					for (int r = 0; r < FONT_8X8_CHAR_H; r++)
						for (int c = 0; c < FONT_8X8_CHAR_W; c++)
							if (g[r] & (0x80 >> c)) {
								int px = tx + c, py = ty + r;
								if (px >= 0 && px < W && py >= 0 && py < H) {
									int bi = (py * (W/8)) + (px/8);
									bb[bi] &= ~(0x80 >> (px % 8));
								}
							}
				}
				EPD_2IN9B_Display(bb, rb);
				heap_caps_free(bb);
				heap_caps_free(rb);
			}
		}
		printf("WiFi connection timeout - no arrivals will be shown\r\n");
	} else {
		wifi_server_print_ip();
		wifi_server_init_sntp();
		setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 0);
		tzset();
		wifi_server_start_http();
	}
#endif

#ifdef CONFIG_EPD_MODULE_B
	/* Module B: Cache last result for 5 min on fetch failure. Decrement TTL manually.
	 * After 5 min without success: show red "Connection lost" */
	{
		const int W = EPD_2IN9B_WIDTH;
		const int H = EPD_2IN9B_HEIGHT;
		const int BUF_SIZE = (W / 8) * H;
		const int CW = FONT_8X8_CHAR_W + 1;       /* 9px per char, small */
		const int CW_X2 = (FONT_8X8_CHAR_W * 2) + 2;  /* 18px per char, 2x scaled */
		const int LH = FONT_8X8_CHAR_H + 1;       /* 10px line, small */
		const int LH_X2 = (FONT_8X8_CHAR_H * 2) + 2; /* 18px line, 2x */
		const int PAD_RIGHT = 0;                  /* right edge - 0 = flush to display edge */
		const int SECTION_TOP_PAD = 6;            /* extra pixels between section title and first item */
		const uint32_t STALE_MS = 5 * 60 * 1000;  /* 5 minutes */
		const uint32_t REBOOT_NO_CONN_MS = 5 * 60 * 1000;  /* reboot after 5 min no connectivity */
		uint8_t *last_black = NULL;
		uint8_t *last_red = NULL;
		arrival_t cached_arr[ARRIVALS_MAX];
		int cached_n = 0;
		char cached_time[8] = "--:--";
		TickType_t last_fetch_tick = 0;
		TickType_t no_connectivity_start = 0;  /* when we first had no connectivity */
		int has_cache = 0;

		#define DRAW_STR(buf, s, sx, sy) do { \
			const char *_s = (s); \
			int _x = (sx); \
			for (; *_s && _x < W; _s++, _x += CW) { \
				unsigned char _ch = (unsigned char)*_s; \
				if (_ch < FONT_8X8_FIRST || _ch >= FONT_8X8_FIRST + FONT_8X8_COUNT) continue; \
				const uint8_t *_g = font_8x8[_ch - FONT_8X8_FIRST]; \
				for (int _r = 0; _r < FONT_8X8_CHAR_H; _r++) { \
					for (int _col = 0; _col < FONT_8X8_CHAR_W; _col++) { \
						if (_g[_r] & (0x80 >> _col)) { \
							int _px = _x + _col, _py = (sy) + _r; \
							if (_px >= 0 && _px < W && _py >= 0 && _py < H) { \
								int _bi = (_py * (W/8)) + (_px/8); \
								(buf)[_bi] &= ~(0x80 >> (_px % 8)); \
							} \
						} \
					} \
				} \
			} \
		} while(0)

		#define DRAW_STR_RED(rbuf, s, sx, sy) do { \
			const char *_s = (s); \
			int _x = (sx); \
			for (; *_s && _x < W; _s++, _x += CW) { \
				unsigned char _ch = (unsigned char)*_s; \
				if (_ch < FONT_8X8_FIRST || _ch >= FONT_8X8_FIRST + FONT_8X8_COUNT) continue; \
				const uint8_t *_g = font_8x8[_ch - FONT_8X8_FIRST]; \
				for (int _r = 0; _r < FONT_8X8_CHAR_H; _r++) { \
					for (int _col = 0; _col < FONT_8X8_CHAR_W; _col++) { \
						if (_g[_r] & (0x80 >> _col)) { \
							int _px = _x + _col, _py = (sy) + _r; \
							if (_px >= 0 && _px < W && _py >= 0 && _py < H) { \
								int _bi = (_py * (W/8)) + (_px/8); \
								(rbuf)[_bi] &= ~(0x80 >> (_px % 8)); \
							} \
						} \
					} \
				} \
			} \
		} while(0)

		/* 2x scaled: each 8x8 pixel becomes 2x2 for bigger numbers */
		#define DRAW_STR_X2(buf, s, sx, sy) do { \
			const char *_s = (s); \
			int _x = (sx); \
			for (; *_s && _x < W; _s++, _x += CW_X2) { \
				unsigned char _ch = (unsigned char)*_s; \
				if (_ch < FONT_8X8_FIRST || _ch >= FONT_8X8_FIRST + FONT_8X8_COUNT) continue; \
				const uint8_t *_g = font_8x8[_ch - FONT_8X8_FIRST]; \
				for (int _r = 0; _r < FONT_8X8_CHAR_H; _r++) { \
					for (int _col = 0; _col < FONT_8X8_CHAR_W; _col++) { \
						if (_g[_r] & (0x80 >> _col)) { \
							int _bx = _x + _col * 2, _by = (sy) + _r * 2; \
							for (int _dy = 0; _dy < 2; _dy++) for (int _dx = 0; _dx < 2; _dx++) { \
								int _px = _bx + _dx, _py = _by + _dy; \
								if (_px >= 0 && _px < W && _py >= 0 && _py < H) { \
									int _bi = (_py * (W/8)) + (_px/8); \
									(buf)[_bi] &= ~(0x80 >> (_px % 8)); \
								} \
							} \
						} \
					} \
				} \
			} \
		} while(0)

		#define DRAW_STR_X2_RED(rbuf, s, sx, sy) do { \
			const char *_s = (s); \
			int _x = (sx); \
			for (; *_s && _x < W; _s++, _x += CW_X2) { \
				unsigned char _ch = (unsigned char)*_s; \
				if (_ch < FONT_8X8_FIRST || _ch >= FONT_8X8_FIRST + FONT_8X8_COUNT) continue; \
				const uint8_t *_g = font_8x8[_ch - FONT_8X8_FIRST]; \
				for (int _r = 0; _r < FONT_8X8_CHAR_H; _r++) { \
					for (int _col = 0; _col < FONT_8X8_CHAR_W; _col++) { \
						if (_g[_r] & (0x80 >> _col)) { \
							int _bx = _x + _col * 2, _by = (sy) + _r * 2; \
							for (int _dy = 0; _dy < 2; _dy++) for (int _dx = 0; _dx < 2; _dx++) { \
								int _px = _bx + _dx, _py = _by + _dy; \
								if (_px >= 0 && _px < W && _py >= 0 && _py < H) { \
									int _bi = (_py * (W/8)) + (_px/8); \
									(rbuf)[_bi] &= ~(0x80 >> (_px % 8)); \
								} \
							} \
						} \
					} \
				} \
			} \
		} while(0)

#if defined(CONFIG_EXAMPLE_USE_WIFI)
		vTaskDelay(3000 / portTICK_RATE_MS);  /* Allow NTP to sync */
#endif

		while (1) {
			uint8_t *black_buf = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
			uint8_t *red_buf = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
			if (!black_buf || !red_buf) {
				if (black_buf) heap_caps_free(black_buf);
				if (red_buf) heap_caps_free(red_buf);
				vTaskDelay(10000 / portTICK_RATE_MS);
				continue;
			}
			memset(black_buf, 0xFF, BUF_SIZE);
#ifdef CONFIG_EPD_2IN9B_V4
			memset(red_buf, 0x00, BUF_SIZE);
#else
			memset(red_buf, 0xFF, BUF_SIZE);
#endif

			char time_str[8] = "--:--";
			arrival_t arr[ARRIVALS_MAX];
			int n = arrivals_fetch(arr, ARRIVALS_MAX, time_str, sizeof(time_str));

			int elapsed_sec = 0;
			if (n > 0) {
				/* Success: update cache */
				has_cache = 1;
				no_connectivity_start = 0;
				last_fetch_tick = xTaskGetTickCount();
				cached_n = n;
				memcpy(cached_arr, arr, n * sizeof(arrival_t));
				strncpy(cached_time, time_str, sizeof(cached_time) - 1);
				cached_time[sizeof(cached_time) - 1] = '\0';
			} else if (has_cache) {
				/* Failure: use cache with decremented TTLs */
				elapsed_sec = (int)((xTaskGetTickCount() - last_fetch_tick) * portTICK_RATE_MS / 1000);
				uint32_t elapsed_ms = (uint32_t)(xTaskGetTickCount() - last_fetch_tick) * portTICK_RATE_MS;
				if (elapsed_ms >= STALE_MS) {
					/* Cache expired: show red "Connection lost", retry WiFi, reboot after 5 min */
					has_cache = 0;
					if (no_connectivity_start == 0)
						no_connectivity_start = last_fetch_tick;
					const char *msg = "Connection lost";
					int mw = 14 * CW;
					int tx = (W - mw) / 2;
					int ty = (H - FONT_8X8_CHAR_H) / 2;
					DRAW_STR_RED(red_buf, msg, tx, ty);
					EPD_2IN9B_Display(black_buf, red_buf);
					if (last_black && last_red) {
						memcpy(last_black, black_buf, BUF_SIZE);
						memcpy(last_red, red_buf, BUF_SIZE);
					} else if (!last_black) {
						last_black = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
						last_red = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
						if (last_black && last_red) {
							memcpy(last_black, black_buf, BUF_SIZE);
							memcpy(last_red, red_buf, BUF_SIZE);
						}
					}
					heap_caps_free(black_buf);
					heap_caps_free(red_buf);
					printf("Module B: Connection lost, retrying WiFi...\r\n");
					wifi_server_reconnect();
					if (wifi_server_wait_connected_timeout(15000)) {
						no_connectivity_start = 0;
						printf("Module B: WiFi reconnected\r\n");
					} else {
						uint32_t no_conn_ms = (uint32_t)(xTaskGetTickCount() - no_connectivity_start) * portTICK_RATE_MS;
						if (no_conn_ms >= REBOOT_NO_CONN_MS) {
							printf("Module B: No connectivity for 5 min, rebooting\r\n");
							esp_restart();
						}
					}
					vTaskDelay(10000 / portTICK_RATE_MS);
					continue;
				}
				n = cached_n;
				memcpy(arr, cached_arr, n * sizeof(arrival_t));
				strncpy(time_str, cached_time, sizeof(time_str) - 1);
				time_str[sizeof(time_str) - 1] = '\0';
				/* Decrement TTLs by elapsed */
				for (int i = 0; i < n; i++) {
					arr[i].ttl_sec -= elapsed_sec;
					if (arr[i].ttl_sec < 0) arr[i].ttl_sec = 0;
				}
			} else {
				/* No cache: retry WiFi, show attempt count, reboot after 5 min */
				heap_caps_free(black_buf);
				heap_caps_free(red_buf);
				if (no_connectivity_start == 0)
					no_connectivity_start = xTaskGetTickCount();
				wifi_retry_count++;
				{
					char msg[24];
					snprintf(msg, sizeof(msg), "No WiFi %d", wifi_retry_count);
					uint8_t *nb = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
					uint8_t *nr = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
					if (nb && nr) {
						memset(nb, 0xFF, BUF_SIZE);
#ifdef CONFIG_EPD_2IN9B_V4
						memset(nr, 0x00, BUF_SIZE);
#else
						memset(nr, 0xFF, BUF_SIZE);
#endif
						int mw = (int)strlen(msg) * CW;
						int tx = (W - mw) / 2;
						int ty = (H - FONT_8X8_CHAR_H) / 2;
						DRAW_STR(nb, msg, tx, ty);
						EPD_2IN9B_Display(nb, nr);
						if (last_black && last_red) {
							memcpy(last_black, nb, BUF_SIZE);
							memcpy(last_red, nr, BUF_SIZE);
						}
						heap_caps_free(nb);
						heap_caps_free(nr);
					}
				}
				printf("Module B: fetch failed, retry #%d\r\n", wifi_retry_count);
				wifi_server_reconnect();
				if (wifi_server_wait_connected_timeout(15000)) {
					no_connectivity_start = 0;
					wifi_retry_count = 0;
					printf("Module B: WiFi reconnected\r\n");
				} else {
					uint32_t no_conn_ms = (uint32_t)(xTaskGetTickCount() - no_connectivity_start) * portTICK_RATE_MS;
					if (no_conn_ms >= REBOOT_NO_CONN_MS) {
						printf("Module B: No connectivity for 5 min, rebooting\r\n");
						esp_restart();
					}
				}
				vTaskDelay(10000 / portTICK_RATE_MS);
				continue;
			}

			/* Numbers right-aligned, 2x size, no "m" - just "5" or "12" */
			const int NUM_RIGHT = W - PAD_RIGHT;
			const int DEST_CHARS = 8;     /* base chars before number */
			const int DEST_CHARS_1DIGIT = 11;  /* +3 when number is 1 digit */
			const int OTHER_PREFIX = 4;   /* " SE " or " TL " */
			const int OTHER_DEST_CHARS = 6;
			const int OTHER_DEST_CHARS_1DIGIT = 9;

			int y = 2;
			DRAW_STR_X2(black_buf, time_str, (W - 5*CW_X2) / 2, y);
			y += LH_X2 + 4;
			DRAW_STR(black_buf, "Woolwich", (W - 8*CW) / 2, y);
			y += LH + LH;

			#define DRAW_SECTION(title, line_substr, max_count) do { \
				DRAW_STR(black_buf, (title), 2, y); \
				y += LH + SECTION_TOP_PAD; \
				int _drawn = 0; \
				for (int _i = 0; _i < n && _drawn < (max_count); _i++) { \
					if (strstr(arr[_i].line, (line_substr))) { \
						int _m = arr[_i].ttl_sec / 60; \
						if (_m <= 1) continue; /* 0 or 1 min left - can't make it */ \
						if (_m > 99) _m = 99; \
						int _dc = (_m < 10) ? DEST_CHARS_1DIGIT : DEST_CHARS; \
						char _d[12]; \
						strncpy(_d, arr[_i].destination, _dc); \
						_d[_dc] = '\0'; \
						char _num[12]; \
						snprintf(_num, sizeof(_num), "%d", (int)_m); \
						int _nw = (int)strlen(_num) * CW_X2; \
						int _nx = NUM_RIGHT - _nw; \
						DRAW_STR(black_buf, " ", 2, y); \
						DRAW_STR(black_buf, _d, 2 + CW, y); \
						if (_m > 5) \
							DRAW_STR_X2_RED(red_buf, _num, _nx, y); \
						else \
							DRAW_STR_X2(black_buf, _num, _nx, y); \
						y += LH_X2; \
						_drawn++; \
					} \
				} \
				y += LH; \
			} while(0)

			DRAW_SECTION("Elizabeth", "Elizabeth", 3);
			DRAW_SECTION("DLR", "DLR", 2);
			DRAW_STR(black_buf, "Other", 2, y);
			y += LH + SECTION_TOP_PAD;
			int others = 0;
			for (int i = 0; i < n && others < 2; i++) {
				if (!strstr(arr[i].line, "Elizabeth") && !strstr(arr[i].line, "DLR")) {
					int m = arr[i].ttl_sec / 60;
					if (m <= 1) continue; /* 0 or 1 min left - can't make it */
					if (m > 99) m = 99;
					int dc = (m < 10) ? OTHER_DEST_CHARS_1DIGIT : OTHER_DEST_CHARS;
					char d[10];
					strncpy(d, arr[i].destination, dc);
					d[dc] = '\0';
					char num[12];
					snprintf(num, sizeof(num), "%d", (int)m);
					int nw = (int)strlen(num) * CW_X2;
					int nx = NUM_RIGHT - nw;
					const char *abbr = strstr(arr[i].line, "Southeastern") ? "SE" : \
					                  strstr(arr[i].line, "Thameslink") ? "TL" : "?";
					DRAW_STR(black_buf, " ", 2, y);
					DRAW_STR(black_buf, abbr, 2 + CW, y);
					DRAW_STR(black_buf, " ", 2 + 3*CW, y);
					DRAW_STR(black_buf, d, 2 + OTHER_PREFIX*CW, y);
					if (m > 5)
						DRAW_STR_X2_RED(red_buf, num, nx, y);
					else
						DRAW_STR_X2(black_buf, num, nx, y);
					y += LH_X2;
					others++;
				}
			}
			#undef DRAW_SECTION

#if defined(CONFIG_EXAMPLE_USE_WIFI)
			char ip_str[16];
			if (wifi_server_get_ip(ip_str, sizeof(ip_str))) {
				DRAW_STR(black_buf, ip_str, 2, H - LH - 2);
			}
#endif

			int need_refresh = 1;
			if (last_black && last_red) {
				if (memcmp(black_buf, last_black, BUF_SIZE) == 0 &&
				    memcmp(red_buf, last_red, BUF_SIZE) == 0) {
					need_refresh = 0;
				}
			}
			if (need_refresh) {
				EPD_2IN9B_Display(black_buf, red_buf);
				if (!last_black) {
					last_black = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
					last_red = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
				}
				if (last_black && last_red) {
					memcpy(last_black, black_buf, BUF_SIZE);
					memcpy(last_red, red_buf, BUF_SIZE);
				}
			}
			heap_caps_free(black_buf);
			heap_caps_free(red_buf);

			printf("Module B: %d arrivals%s%s, next in 10s\r\n",
				n, need_refresh ? " (refreshed)" : "", elapsed_sec ? " (cached)" : "");
			vTaskDelay(10000 / portTICK_RATE_MS);
		}
		#undef DRAW_STR
		#undef DRAW_STR_RED
		#undef DRAW_STR_X2
		#undef DRAW_STR_X2_RED
	}
#endif

#ifdef CONFIG_EXAMPLE_USE_WIFI

    ESP_ERROR_CHECK( nvs_flash_init() );

    EPD_DisplayClearPart();
	EPD_fillScreen(_bg);
	EPD_setFont(DEFAULT_FONT, NULL);
	sprintf(tmp_buff, "Waiting for NTP time...");
	EPD_print(tmp_buff, CENTER, CENTER);
	EPD_drawRect(10,10,274,108, EPD_BLACK);
	EPD_drawRect(12,12,270,104, EPD_BLACK);
	EPD_UpdateScreen();

	// ===== Set time zone ======
	setenv("TZ", "CET-1CEST", 0);
	tzset();
	// ==========================

    time(&time_now);
	tm_info = localtime(&time_now);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if (tm_info->tm_year < (2016 - 1900)) {
        ESP_LOGI(tag, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        time(&time_now);
    }
    else {
        /* Time already set, connect WiFi for web server */
        wifi_server_init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
        wifi_server_wait_connected();
        wifi_server_print_ip();
    }
    /* Start web server (WiFi stays connected) */
    wifi_server_start_http();
#endif

    // ==== Initialize the file system ====
    printf("\r\n\n");
	vfs_spiffs_register();
    if (spiffs_is_mounted) {
        ESP_LOGI(tag, "File system mounted.");
    }
    else {
        ESP_LOGE(tag, "Error mounting file system.");
    }

	//=========
    // Run demo (disabled when CONFIG_EPD_MODULE_B - Woolwich arrivals only)
    //=========
#ifndef CONFIG_EPD_MODULE_B
	/*
	EPD_DisplayClearFull();
    EPD_DisplayClearPart();
	EPD_fillScreen(_bg);
	//EPD_DisplaySetPart(0x00);
	//EPD_DisplaySetPart(0xFF);


    uint8_t LUTTest1[31]	= {0x32, 0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    uint8_t LUTTest2[31]	= {0x32, 0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	_gs = 0;
	_fg = 1;
	_bg = 0;
	int n = 0;
	while (1) {
		//EPD_DisplayClearFull();
		EPD_fillRect(14, 14, 100, 100, ((n&1) ? 0 : 1));
		EPD_fillRect(_width/2+14, 14, 100, 100, ((n&1) ? 1 : 0));
		//LUT_part = LUTTest1;
		EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
		//EPD_wait(2000);
		//LUT_part = LUTTest2;
		//EPD_DisplayFull(disp_buffer);
		printf("Updated\r\n");
		EPD_wait(4000);
		n++;


		n = 0;
		printf("\r\n==== FULL UPDATE TEST ====\r\n\n");
		EPD_DisplayClearFull();
		while (n < 2) {
			EPD_fillScreen(_bg);
			printf("Black\r\n");
			EPD_fillRect(0,0,_width/2,_height-1, EPD_BLACK);

			EPD_fillRect(20,20,_width/2-40,_height-1-40, EPD_WHITE);
			EPD_DisplayFull(disp_buffer);
			EPD_wait(4000);

			printf("White\r\n");
			EPD_fillRect(0,0,_width/2,_height-1, EPD_WHITE);
			EPD_DisplayFull(disp_buffer);
			EPD_wait(2000);
			n++;
		}

		printf("\r\n==== PARTIAL UPDATE TEST ====\r\n\n");
		EPD_DisplayClearFull();
		n = 0;
		while (n < 2) {
			EPD_fillScreen(_bg);
			printf("Black\r\n");
			EPD_fillRect(0,0,_width/2,_height-1, EPD_BLACK);

			EPD_fillRect(20,20,_width/2-40,_height-1-40, EPD_WHITE);
			EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
			EPD_wait(4000);

			printf("White\r\n");
			EPD_fillRect(0,0,_width/2,_height-1, EPD_WHITE);
			EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
			EPD_wait(2000);
			n++;
		}

		printf("\r\n==== PARTIAL UPDATE TEST - gray scale ====\r\n\n");
		EPD_DisplayClearFull();
		n = 0;
		while (n < 3) {
			EPD_fillScreen(_bg);
			LUT_part = LUT_gs;
			for (uint8_t sh=1; sh<16; sh++) {
				LUT_gs[21] = sh;
				printf("Black (%d)\r\n", LUT_gs[21]);
				EPD_fillRect((sh-1)*19,0,19,_height, EPD_BLACK);
				EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
			}
			EPD_wait(4000);

			//LUT_part = LUTDefault_part;
			printf("White\r\n");
			//EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
			//EPD_DisplaySetPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, 0xFF);
			LUT_gs[21] = 15;
			LUT_gs[1] = 0x28;
			EPD_fillRect(190,0,76,_height, EPD_WHITE);
			EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
			EPD_wait(2000);

			EPD_fillRect(0,0,_width,_height, EPD_WHITE);
			EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
			LUT_gs[1] = 0x18;
			EPD_wait(2000);
			n++;
		}
		LUT_part = LUTDefault_part;
	}
*/


	printf("==== START ====\r\n\n");

	_gs = 1;
	uint32_t tstart;
	int pass = 0, ftype = 9;
    while (1) {
    	ftype++;
    	if (ftype > 10) {
    		ftype = 1;
    		for (int t=40; t>0; t--) {
				printf("Wait %d seconds ...  \r", t);
				fflush(stdout);
				EPD_wait(1000);
    		}
			printf("                      \r");
			fflush(stdout);
			_gs ^= 1;
    	}
    	printf("\r\n-- Test %d\r\n", ftype);
    	EPD_DisplayClearPart();

    	//EPD_Cls(0);
		EPD_fillScreen(_bg);
		_fg = 15;
		_bg = 0;

		EPD_drawRect(1,1,294,126, EPD_BLACK);

		int y = 4;
		tstart = clock();
		if (ftype == 1) {
			for (int f=0; f<4; f++) {
				if (f == 0) _fg = 15;
				else if (f == 1) _fg = 9;
				else if (f == 2) _fg = 5;
				else if (f == 2) _fg = 3;
				EPD_setFont(f, NULL);
				if (f == 3) {
					EPD_print("Welcome to ", 4, y);
					font_rotate = 90;
					EPD_print("ESP32", EPD_getStringWidth("Welcome to ")+EPD_getfontheight()+4, y);
					font_rotate = 0;
				}
				else if (f == 1) {
					EPD_print("HR chars: \xA6\xA8\xB4\xB5\xB0", 4, y);
				}
				else {
					EPD_print("Welcome to ESP32", 4, y);
				}
				y += EPD_getfontheight() + 2;
			}
			font_rotate = 45;
			EPD_print("ESP32", LASTX+8, LASTY);
			font_rotate = 0;
			_fg = 15;

			EPD_setFont(DEFAULT_FONT, NULL);
			sprintf(tmp_buff, "Pass: %d", pass+1);
			EPD_print(tmp_buff, 4, 128-EPD_getfontheight()-2);
			EPD_UpdateScreen();
		}
		else if (ftype == 2) {
			orientation = LANDSCAPE_180;
			for (int f=4; f<FONT_7SEG; f++) {
				if (f == 4) _fg = 15;
				else if (f == 5) _fg = 9;
				else if (f == 6) _fg = 5;
				else if (f == 7) _fg = 3;
				EPD_setFont(f, NULL);
				EPD_print("Welcome to ESP32", 4, y);
				y += EPD_getfontheight() + 1;
			}
			_fg = 15;
			EPD_setFont(DEFAULT_FONT, NULL);
			sprintf(tmp_buff, "Pass: %d (rotated 180)", pass+1);
			EPD_print(tmp_buff, 4, 128-EPD_getfontheight()-2);
			orientation = LANDSCAPE_0;
			EPD_UpdateScreen();
		}
		else if (ftype == 3) {
			for (int f=0; f<3; f++) {
				if (f == 0) _fg = 15;
				else if (f == 1) _fg = 9;
				else if (f == 2) _fg = 5;
				EPD_setFont(USER_FONT, file_fonts[f]);
				if (f == 0) font_line_space = 4;
				EPD_print("Welcome to ESP32", 4, y);
				font_line_space = 0;
				y += EPD_getfontheight() + 2;
			}
			_fg = 15;
			EPD_setFont(DEFAULT_FONT, NULL);
			sprintf(tmp_buff, "Pass: %d (Fonts from file)", pass+1);
			EPD_print(tmp_buff, 4, 128-EPD_getfontheight()-2);
			EPD_UpdateScreen();
		}
		else if (ftype == 4) {
			y = 16;
			time(&time_now);
			tm_info = localtime(&time_now);
			int _sec = -1, y2, _day = -1;

			EPD_setFont(FONT_7SEG, NULL);
			set_7seg_font_atrib(10, 2, 0, 15);
			y2 = y + EPD_getfontheight() + 10;

			for (int t=0; t<100; t++) {
				time(&time_now);
				tm_info = localtime(&time_now);
				if (tm_info->tm_sec != _sec) {
					_sec = tm_info->tm_sec;
					sprintf(tmp_buff, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
					_fg = 15;							// fill = 15
					set_7seg_font_atrib(10, 2, 0, 15);	// outline = 15
					EPD_print(tmp_buff, CENTER, y);
					_fg = 15;
					if (tm_info->tm_mday != _day) {
						sprintf(tmp_buff, "%02d.%02d.%04d", tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year+1900);
						_fg = 7;							// fill = 7
						set_7seg_font_atrib(8, 2, 1, 15);	// outline = 15
						EPD_print(tmp_buff, CENTER, y2);
						_fg = 15;
					}
					EPD_UpdateScreen();
				}
				EPD_wait(100);
			}
			tstart = clock();
			_fg = 15;
			EPD_setFont(DEFAULT_FONT, NULL);
			font_rotate = 90;
			sprintf(tmp_buff, "%02d:%02d:%02d %02d/%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tm_info->tm_mday, tm_info->tm_mon + 1);
			EPD_print(tmp_buff, 20, 4);
			font_rotate = 0;
			sprintf(tmp_buff, "Pass: %d", pass+1);
			EPD_print(tmp_buff, 4, 128-EPD_getfontheight()-2);
			EPD_UpdateScreen();
		}
		else if (ftype == 5) {
			uint8_t old_gs = _gs;
			_gs = 1;
			EPD_drawRect(4,4,20,20, 15);

			EPD_fillRect(27,5,18,18, 1);
			EPD_drawRect(26,4,20,20, 15);

			EPD_drawCircle(66,16,10, 15);

			EPD_fillCircle(92,16,10, 2);
			EPD_drawCircle(92,16,11, 15);

			EPD_fillRect(185,4,80,80, 3);
			EPD_drawRect(185,4,80,80, 15);

			EPD_fillCircle(225,44,35, 0);
			EPD_drawCircle(225,44,35, 15);
			EPD_fillCircle(225,44,35, 5);

			EPD_fillCircle(225,44,20, 0);
			EPD_drawCircle(225,44,20, 15);

			orientation = LANDSCAPE_180;
			EPD_drawRect(4,4,20,20, 15);

			EPD_fillRect(27,5,18,18, 1);
			EPD_drawRect(26,4,20,20, 15);

			EPD_drawCircle(66,16,10, 15);

			EPD_fillCircle(92,16,10, 2);
			EPD_drawCircle(92,16,11, 15);

			EPD_fillRect(185,4,80,80, 3);
			EPD_drawRect(185,4,80,80, 15);

			EPD_fillCircle(225,44,35, 0);
			EPD_drawCircle(225,44,35, 15);
			EPD_fillCircle(225,44,35, 5);

			EPD_fillCircle(225,44,20, 0);
			EPD_drawCircle(225,44,20, 15);
			orientation = LANDSCAPE_0;

			EPD_setFont(DEFAULT_FONT, NULL);
			font_rotate = 90;
			sprintf(tmp_buff, "Pass: %d", pass+1);
			EPD_print("Gray scale demo", _width/2+EPD_getfontheight()+2, 4);
			EPD_print(tmp_buff, _width/2, 4);
			font_rotate = 0;

			EPD_UpdateScreen();
			_gs = old_gs;
		}
		else if (ftype == 6) {
			uint8_t old_gs = _gs;
			_gs = 0;
			memcpy(disp_buffer, (unsigned char *)gImage_img1, sizeof(gImage_img1));

			EPD_setFont(DEFAULT_FONT, NULL);
			sprintf(tmp_buff, "Pass: %d", pass+1);
			EPD_print(tmp_buff, 4, 128-EPD_getfontheight()-2);

			EPD_UpdateScreen();
			_gs = old_gs;
		}
		else if (ftype == 7) {
			uint8_t old_gs = _gs;
			_gs = 0;
			memcpy(disp_buffer, (unsigned char *)gImage_img3, sizeof(gImage_img3));

			EPD_setFont(DEFAULT_FONT, NULL);
			_fg = 0;
			_bg = 1;
			sprintf(tmp_buff, "Pass: %d", pass+1);
			EPD_print(tmp_buff, 4, 128-EPD_getfontheight()-2);

			EPD_UpdateScreen();
			_fg = 15;
			_bg = 0;
			_gs = old_gs;
		}
		else if (ftype == 8) {
			uint8_t old_gs = _gs;
			_gs = 1;
			int i, x, y;
	        uint8_t last_lvl = 0;
		    for (i=0; i<16; i++) {
		        for (x = 0; x < EPD_DISPLAY_WIDTH; x++) {
		          for (y = 0; y < EPD_DISPLAY_HEIGHT; y++) {
		        	  uint8_t pix = img_hacking[(x * EPD_DISPLAY_HEIGHT) + (EPD_DISPLAY_HEIGHT-y-1)];
		        	  if ((pix > last_lvl) && (pix <= lvl_buf[i])) {
		        		  gs_disp_buffer[(y * EPD_DISPLAY_WIDTH) + x] = i;
		        		  gs_used_shades |= (1 << i);
		        	  }
		          }
		        }
		        last_lvl = lvl_buf[i];
		    }

			EPD_setFont(DEFAULT_FONT, NULL);
			sprintf(tmp_buff, "Pass: %d (Gray scale image)", pass+1);
			EPD_print(tmp_buff, 4, 128-EPD_getfontheight()-2);

			EPD_UpdateScreen();
			_gs = old_gs;
		}
		else if (ftype == 9) {
			uint8_t old_gs = _gs;
			_gs = 0;
			memcpy(disp_buffer, gImage_img2, sizeof(gImage_img2));

			EPD_setFont(DEFAULT_FONT, NULL);
			sprintf(tmp_buff, "Pass: %d", pass+1);
			EPD_print(tmp_buff, 4, 4);

			EPD_UpdateScreen();
			_gs = old_gs;
		}
		else if (ftype == 10) {
			if (spiffs_is_mounted) {
				// ** Show scaled (1/8, 1/4, 1/2 size) JPG images
					uint8_t old_gs = _gs;
					_gs = 1;
					EPD_Cls();
					EPD_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/evolution-of-human.jpg", NULL, 0);
					EPD_UpdateScreen();
					EPD_wait(5000);

					EPD_Cls();
					EPD_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/people_silhouettes.jpg", NULL, 0);
					EPD_UpdateScreen();
					EPD_wait(5000);

					EPD_Cls();
					EPD_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/silhouettes-dancing.jpg", NULL, 0);
					EPD_UpdateScreen();
					EPD_wait(5000);

					EPD_Cls();
					EPD_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/girl_silhouettes.jpg", NULL, 0);
					EPD_UpdateScreen();
					EPD_wait(5000);

					EPD_Cls();
					EPD_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/animal-silhouettes.jpg", NULL, 0);
					EPD_UpdateScreen();
					EPD_wait(5000);

					EPD_Cls();
					EPD_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/Flintstones.jpg", NULL, 0);
					EPD_UpdateScreen();
					EPD_wait(5000);

					_gs = old_gs;
			}
		}

		//EPD_DisplayPart(0, EPD_DISPLAY_WIDTH-1, 0, EPD_DISPLAY_HEIGHT-1, disp_buffer);
		tstart = clock() - tstart;
		pass++;
    	printf("-- Type: %d Pass: %d Time: %u ms\r\n", ftype, pass, tstart);

    	EPD_PowerOff();
		EPD_wait(8000);
    }
#endif /* !CONFIG_EPD_MODULE_B */

}
