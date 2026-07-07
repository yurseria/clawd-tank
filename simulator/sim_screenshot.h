#ifndef SIM_SCREENSHOT_H
#define SIM_SCREENSHOT_H

#include <stdint.h>

/** Initialize screenshot system. Creates output_dir if it doesn't exist. */
void sim_screenshot_init(const char *output_dir);

/**
 * Capture a screenshot from the RGB565 framebuffer.
 * @param framebuffer  Pointer to 480*320 uint16_t array
 * @param w            Width (480)
 * @param h            Height (320)
 * @param time_ms      Simulated time in ms (used for filename)
 * @param suffix       Optional suffix for filename (e.g. "connect"), or NULL
 */
void sim_screenshot_capture(const uint16_t *framebuffer, int w, int h,
                            uint32_t time_ms, const char *suffix);

#endif
