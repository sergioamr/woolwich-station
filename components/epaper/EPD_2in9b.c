/*
 * 2.9" e-Paper Module (B) - Tricolor (red/black/white) - SSD1680
 * V3: 128 x 296, commands 0x10/0x13/0x12
 * V4: 128 x 296, commands 0x24/0x26, 0x22/0x20 refresh
 */
#include "EPDspi.h"
#include "EPD_2in9b.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>

#define EPD_2IN9B_WIDTH  128
#define EPD_2IN9B_HEIGHT 296
#define BUF_SIZE ((EPD_2IN9B_WIDTH / 8) * EPD_2IN9B_HEIGHT)

static void SendCmd(uint8_t reg)
{
    spi_lobo_device_select(disp_spi, 0);
    EPD_DC_0;
    SPI_Write(reg);
    spi_lobo_device_deselect(disp_spi);
}

static void SendData(uint8_t data)
{
    spi_lobo_device_select(disp_spi, 0);
    EPD_DC_1;
    SPI_Write(data);
    spi_lobo_device_deselect(disp_spi);
}

/* V3: busy when LOW. Poll 0x71 until BUSY goes high */
static void ReadBusy_V3(void)
{
    uint8_t busy;
    do {
        SendCmd(0x71);
        vTaskDelay(10 / portTICK_RATE_MS);
        busy = gpio_get_level(BUSY_Pin);
        busy = !(busy & 0x01);
    } while (busy);
    vTaskDelay(200 / portTICK_RATE_MS);
}

/* V4: busy when LOW, no 0x71 poll - just wait for pin */
static void ReadBusy_V4(void)
{
    while (gpio_get_level(BUSY_Pin) == 1) {
        vTaskDelay(50 / portTICK_RATE_MS);
    }
    vTaskDelay(200 / portTICK_RATE_MS);
}

static void Reset(void)
{
    gpio_set_level(RST_Pin, 1);
    vTaskDelay(200 / portTICK_RATE_MS);
    gpio_set_level(RST_Pin, 0);
    vTaskDelay(10 / portTICK_RATE_MS);
    gpio_set_level(RST_Pin, 1);
    vTaskDelay(300 / portTICK_RATE_MS);
}

/* ========== V3 Init (original) ========== */
static void Init_V3(void)
{
    Reset();

    SendCmd(0x04);
    ReadBusy_V3();

    SendCmd(0x00);
    SendData(0x0f);
    SendData(0x89);

    SendCmd(0x61);
    SendData(0x80);
    SendData(0x01);
    SendData(0x28);

    SendCmd(0x50);
    SendData(0x77);
}

/* ========== V4 Init (newer hardware) ========== */
static void Init_V4(void)
{
    Reset();

    ReadBusy_V4();
    SendCmd(0x12);  /* SWRESET */
    ReadBusy_V4();

    SendCmd(0x01);
    SendData((EPD_2IN9B_HEIGHT - 1) % 256);
    SendData((EPD_2IN9B_HEIGHT - 1) / 256);
    SendData(0x00);

    SendCmd(0x11);
    SendData(0x03);

    SendCmd(0x44);
    SendData(0x00);
    SendData((EPD_2IN9B_WIDTH / 8) - 1);

    SendCmd(0x45);
    SendData(0x00);
    SendData(0x00);
    SendData((EPD_2IN9B_HEIGHT - 1) % 256);
    SendData((EPD_2IN9B_HEIGHT - 1) / 256);

    SendCmd(0x3C);
    SendData(0x05);

    SendCmd(0x21);
    SendData(0x00);
    SendData(0x80);

    SendCmd(0x18);
    SendData(0x80);

    SendCmd(0x4E);
    SendData(0x00);
    SendCmd(0x4F);
    SendData(0x00);
    SendData(0x00);
    ReadBusy_V4();
}

void EPD_2IN9B_Init(void)
{
#ifdef CONFIG_EPD_2IN9B_V4
    Init_V4();
#else
    Init_V3();
#endif
}

void EPD_2IN9B_Clear(void)
{
    uint16_t w = (EPD_2IN9B_WIDTH % 8 == 0) ? (EPD_2IN9B_WIDTH / 8) : (EPD_2IN9B_WIDTH / 8 + 1);
    uint16_t h = EPD_2IN9B_HEIGHT;
    uint8_t *buf = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
    if (!buf) return;

    memset(buf, 0xFF, BUF_SIZE);

#ifdef CONFIG_EPD_2IN9B_V4
    SendCmd(0x24);
    EPD_SendDataBlock(buf, w * h);
    memset(buf, 0x00, BUF_SIZE);  /* V4 red: 0x00 = white (inverted on send) */
    SendCmd(0x26);
    EPD_SendDataBlock(buf, w * h);
    SendCmd(0x22);
    SendData(0xF7);
    SendCmd(0x20);
    ReadBusy_V4();
#else
    SendCmd(0x10);
    EPD_SendDataBlock(buf, w * h);
    SendCmd(0x13);
    EPD_SendDataBlock(buf, w * h);
    SendCmd(0x12);
    ReadBusy_V3();
#endif

    heap_caps_free(buf);
}

void EPD_2IN9B_Display(const uint8_t *blackimage, const uint8_t *ryimage)
{
    uint16_t w = (EPD_2IN9B_WIDTH % 8 == 0) ? (EPD_2IN9B_WIDTH / 8) : (EPD_2IN9B_WIDTH / 8 + 1);
    uint16_t h = EPD_2IN9B_HEIGHT;

#ifdef CONFIG_EPD_2IN9B_V4
    SendCmd(0x24);
    EPD_SendDataBlock(blackimage, w * h);
    SendCmd(0x26);
    {
        uint8_t *inv = heap_caps_malloc(w * h, MALLOC_CAP_DMA);
        if (inv) {
            for (uint16_t i = 0; i < w * h; i++) inv[i] = ~ryimage[i];
            EPD_SendDataBlock(inv, w * h);
            heap_caps_free(inv);
        } else {
            for (uint16_t i = 0; i < w * h; i++) SendData(~ryimage[i]);
        }
    }
    SendCmd(0x22);
    SendData(0xF7);
    SendCmd(0x20);
    ReadBusy_V4();
#else
    SendCmd(0x10);
    EPD_SendDataBlock(blackimage, w * h);
    SendCmd(0x92);
    SendCmd(0x13);
    EPD_SendDataBlock(ryimage, w * h);
    SendCmd(0x92);
    SendCmd(0x12);
    ReadBusy_V3();
#endif
}

void EPD_2IN9B_Sleep(void)
{
#ifdef CONFIG_EPD_2IN9B_V4
    SendCmd(0x10);
    SendData(0x01);
    vTaskDelay(100 / portTICK_RATE_MS);
#else
    SendCmd(0x02);
    ReadBusy_V3();
    SendCmd(0x07);
    SendData(0xA5);
#endif
}
