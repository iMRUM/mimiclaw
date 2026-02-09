#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(void);
void display_show_banner(void);
void display_set_backlight_percent(uint8_t percent);
uint8_t display_get_backlight_percent(void);
void display_cycle_backlight(void);
bool display_get_banner_center_rgb(uint8_t *r, uint8_t *g, uint8_t *b);

#ifdef __cplusplus
}
#endif
