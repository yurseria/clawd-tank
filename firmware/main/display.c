// firmware/main/display.c
//
// LCD display driver for WT32-SC01 Plus (ESP32-S3-WROOM-1).
//
// Target: 3.5" 480x320 ST7796 over Intel 8080 8-bit parallel interface.
// LVGL RGB565 with partial render buffers in PSRAM.
#include "display.h"
#include "config_store.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "display";

/* WT32-SC01 Plus — ST7796 8-bit parallel (MCU 8080) pin mapping.
 * Datasheet: ZX3D50CE08S-USRC-4832 (Wireless Tag). */
#define LCD_DB0        9
#define LCD_DB1        46
#define LCD_DB2        3
#define LCD_DB3        8
#define LCD_DB4        18
#define LCD_DB5        17
#define LCD_DB6        16
#define LCD_DB7        15
#define LCD_RS         0       /* Data/Command (D/CX) */
#define LCD_WR         47
#define LCD_RST        4
#define LCD_BL         45      /* Backlight PWM, active high */

/* Display config */
#define LCD_H_RES       480   /* landscape width */
#define LCD_V_RES       320   /* landscape height */
#define LCD_CMD_BITS    8
#define LCD_PARAM_BITS  8
#define LCD_PCLK_HZ     (20 * 1000 * 1000)  /* 20 MHz — verified stable on this panel */
#define LVGL_BUF_LINES  20    /* internal DMA SRAM only — keep small to fit */
#define LVGL_TICK_MS    2

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                     esp_lcd_panel_io_event_data_t *edata,
                                     void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *px_map) {
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);

    esp_lcd_panel_draw_bitmap(panel,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static void lvgl_tick_cb(void *arg) {
    lv_tick_inc(LVGL_TICK_MS);
}

lv_display_t *display_init(void) {
    ESP_LOGI(TAG, "Initializing display (WT32-SC01 Plus, ST7796 parallel)...");

    /* --- LVGL must be initialized before panel I/O so the display handle
     *     can be passed as user_ctx to the color-transfer-done callback. --- */
    lv_init();
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!display) {
        ESP_LOGE(TAG, "lv_display_create failed — out of memory");
        abort();
    }

    /* PWM backlight via LEDC — keep duty low to reduce heat */
    ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));
    ledc_channel_config_t bl_channel = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,  // off during init
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_channel));

    /* Intel 8080 bus — 8 data lines + DC + WR */
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = LCD_RS,
        .wr_gpio_num = LCD_WR,
        .data_gpio_nums = {
            LCD_DB0, LCD_DB1, LCD_DB2, LCD_DB3,
            LCD_DB4, LCD_DB5, LCD_DB6, LCD_DB7,
        },
        .bus_width = 8,
        .max_transfer_bytes = LCD_H_RES * LVGL_BUF_LINES * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    /* Panel I/O on the I80 bus — flush-ready callback targets the LVGL display */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = -1,  /* WT32-SC01 Plus has no CS line for the LCD */
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = display,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            /* LVGL renders RGB565 little-endian; ST7796 expects the high
             * byte first, so let the I80 peripheral swap each 16-bit word. */
            .swap_color_bytes = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    /* ST7796 panel */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,  /* panel wiring is BGR */
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    /* The WT32-SC01 Plus IPS panel needs color inversion (same as ST7789 IPS) */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));

    /* Landscape: swap X/Y to rotate from native 320x480 portrait to 480x320.
     * Portrait-correct orientation on this panel is mirror_x=true/swap=false,
     * so a 90° rotation is swap_xy=true with no mirroring. If the image ends
     * up 180° rotated, use mirror(true, true) instead. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, false));

    /* Clear screen to black before turning on backlight. INVON compensates
     * the panel's inverted optics, so the chain is WYSIWYG: 0x0000 = black. */
    {
        size_t clear_sz = LCD_H_RES * LVGL_BUF_LINES * sizeof(uint16_t);
        void *clear_buf = heap_caps_calloc(1, clear_sz, MALLOC_CAP_DMA);
        configASSERT(clear_buf);
        for (int y = 0; y < LCD_V_RES; y += LVGL_BUF_LINES) {
            int h = (y + LVGL_BUF_LINES <= LCD_V_RES) ? LVGL_BUF_LINES : (LCD_V_RES - y);
            esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y + h, clear_buf);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        free(clear_buf);
    }

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, config_store_get_brightness());
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    /* LVGL render buffers — must be in internal DMA-capable SRAM because the
     * flush callback sends them directly to the I80 parallel bus, whose DMA
     * cannot access PSRAM. Keep buffers small (20 lines) to fit in SRAM.
     * Scene sprite frame buffers (decoded separately) can still use PSRAM. */
    size_t buf_sz = LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    configASSERT(buf1 && buf2);

    lv_display_set_buffers(display, buf1, buf2, buf_sz,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    /* LVGL tick timer */
    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    ESP_LOGI(TAG, "Display initialized: %dx%d landscape (parallel I80)", LCD_H_RES, LCD_V_RES);
    return display;
}

void display_set_brightness(uint8_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "Brightness set to %u", duty);
}
