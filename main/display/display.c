#include "display/display.h"

#include <string.h>
#include <stdbool.h>
#include "esp_check.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "display/Vernon_ST7789T/Vernon_ST7789T.h"

#define LCD_HOST  SPI3_HOST

#define LCD_PIXEL_CLOCK_HZ     (12 * 1000 * 1000)
#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

#define LCD_H_RES              172
#define LCD_V_RES              320

#define BANNER_W               320
#define BANNER_H               172

#define LCD_PIN_SCLK           40
#define LCD_PIN_MOSI           45
#define LCD_PIN_MISO           -1
#define LCD_PIN_DC             41
#define LCD_PIN_RST            39
#define LCD_PIN_CS             42
#define LCD_PIN_BK_LIGHT       46

#define LCD_X_GAP              34
#define LCD_Y_GAP              0

#define LEDC_TIMER             LEDC_TIMER_0
#define LEDC_MODE              LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL           LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY_HZ       4000

#define BACKLIGHT_MIN_PERCENT  10
#define BACKLIGHT_MAX_PERCENT  100
#define BACKLIGHT_STEP_PERCENT 10

static const char *TAG = "display";

static esp_lcd_panel_handle_t panel_handle = NULL;
static uint8_t backlight_percent = 50;

extern const uint8_t _binary_banner_320x172_rgb565_start[];
extern const uint8_t _binary_banner_320x172_rgb565_end[];

static void backlight_ledc_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LCD_PIN_BK_LIGHT,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void display_set_backlight_percent(uint8_t percent)
{
    if (percent > BACKLIGHT_MAX_PERCENT) {
        percent = BACKLIGHT_MAX_PERCENT;
    }
    backlight_percent = percent;

    uint32_t duty_max = (1U << LEDC_DUTY_RES) - 1;
    uint32_t duty = (duty_max * backlight_percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

uint8_t display_get_backlight_percent(void)
{
    return backlight_percent;
}

void display_cycle_backlight(void)
{
    uint8_t next = backlight_percent + BACKLIGHT_STEP_PERCENT;
    if (next > BACKLIGHT_MAX_PERCENT) {
        next = BACKLIGHT_MIN_PERCENT;
    }
    display_set_backlight_percent(next);
    ESP_LOGI(TAG, "Backlight -> %u%%", next);
}

esp_err_t display_init(void)
{
    esp_err_t ret = ESP_OK;

    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus init failed");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle), TAG, "panel io init failed");

    esp_lcd_panel_dev_st7789t_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789t(io_handle, &panel_config, &panel_handle), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, true, true), TAG, "panel mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_handle, true), TAG, "panel swap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel_handle, LCD_Y_GAP, LCD_X_GAP), TAG, "panel gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "panel on failed");

    backlight_ledc_init();
    display_set_backlight_percent(backlight_percent);

    return ret;
}

void display_show_banner(void)
{
    if (!panel_handle) {
        ESP_LOGW(TAG, "display not initialized");
        return;
    }

    const uint8_t *start = _binary_banner_320x172_rgb565_start;
    const uint8_t *end = _binary_banner_320x172_rgb565_end;
    size_t len = (size_t)(end - start);
    size_t expected = (size_t)BANNER_W * (size_t)BANNER_H * 2;
    if (len < expected) {
        ESP_LOGW(TAG, "banner data too small (%u < %u)", (unsigned)len, (unsigned)expected);
        return;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, BANNER_W, BANNER_H, start));
}

bool display_get_banner_center_rgb(uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!r || !g || !b) {
        return false;
    }

    const uint8_t *start = _binary_banner_320x172_rgb565_start;
    const uint8_t *end = _binary_banner_320x172_rgb565_end;
    size_t len = (size_t)(end - start);
    size_t expected = (size_t)BANNER_W * (size_t)BANNER_H * 2;
    if (len < expected) {
        return false;
    }

    size_t cx = BANNER_W / 2;
    size_t cy = BANNER_H / 2;
    size_t idx = (cy * BANNER_W + cx) * 2;
    uint16_t pixel = (uint16_t)start[idx] | ((uint16_t)start[idx + 1] << 8);

    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5) & 0x3F;
    uint8_t b5 = pixel & 0x1F;

    *r = (uint8_t)((r5 * 255) / 31);
    *g = (uint8_t)((g6 * 255) / 63);
    *b = (uint8_t)((b5 * 255) / 31);
    return true;
}
