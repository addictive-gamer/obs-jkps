/*
obs-jkps - Keys Per Second overlay source for OBS Studio
Copyright (C) 2026 addictive-gamer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "jkps-input.h"

#ifdef __cplusplus
extern "C" {
#endif

struct jkps_render_key {
	const char *label;
	bool down;
	uint64_t total;
};

/* Everything the renderer needs to know to draw one frame. All colors are
 * 0xAABBGGRR (OBS's obs_data_get_int color convention). */
struct jkps_render_params {
	int num_keys;
	struct jkps_render_key keys[JKPS_MAX_KEYS];
	bool vertical_layout;

	int key_size;
	int key_spacing;
	int key_font_size;

	uint32_t color_idle;
	uint32_t color_pressed;
	uint32_t color_text;
	uint32_t color_bg;

	bool show_kps;
	bool show_total;
	bool show_bpm;
	int stats_font_size;
	uint32_t stats_color;
	float kps;
	uint64_t total_presses;
	float bpm;
};

/* Computes the pixel dimensions the canvas needs for the given params. */
void jkps_render_measure(const struct jkps_render_params *p, uint32_t *out_width, uint32_t *out_height);

/* Renders the current state into `pixels`, a caller-allocated buffer of at
 * least width*height*4 bytes (BGRA, straight/non-premultiplied alpha,
 * matching GS_BGRA). Returns false if rendering failed (Windows only). */
bool jkps_render_frame(const struct jkps_render_params *p, uint32_t width, uint32_t height, uint8_t *pixels);

#ifdef __cplusplus
}
#endif
