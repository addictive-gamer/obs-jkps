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

#define JKPS_KPS_GRAPH_SAMPLES 60

/* Max number of just-released bars a single key can have in flight (rising
 * away from the key, fading out) at the same time. Needed because rapid
 * repeated presses on the same key each spawn their own detached remnant -
 * with only one slot, a new tap would stomp/cut off the previous tap's
 * still-fading remnant before it finished its trip. */
#define JKPS_MAX_FLOAT_BARS 12

struct jkps_render_key {
	const char *label;
	bool down;
	uint64_t total;

	/* 0..1, jumps to 1 on press and decays on release - used as the
	 * bars-mode VU-meter fill level. */
	float press_level;

	/* Current height (px) of the press bar: grows from 0 toward
	 * press_bar_max_height while held; drops to 0 the instant the key is
	 * released (see float_bar_* below - the visible piece doesn't retract,
	 * it detaches instead). */
	float press_bar_px;

	/* The just-released segments: instead of shrinking back into the key,
	 * each one keeps its height (float_bar_len, frozen at whatever
	 * press_bar_px was at the moment of release) and drifts away from the
	 * key (float_bar_drift, growing each tick), fading out
	 * (float_bar_alpha, 1..0) as it nears/exits the press_bar_max_height
	 * margin. len <= 0 means that slot has no floating remnant to draw.
	 * A pool of these (instead of a single one) so back-to-back taps on
	 * the same key each get their own independent rise-and-drift-off
	 * animation rather than a new tap cutting off the previous one. */
	float float_bar_len[JKPS_MAX_FLOAT_BARS];
	float float_bar_drift[JKPS_MAX_FLOAT_BARS];
	float float_bar_alpha[JKPS_MAX_FLOAT_BARS];

	/* True when the active Funkin' Skin ships hold/sustain art for this
	 * lane: the caller draws that art (tiled) on top instead, so the
	 * flat-color bar below is skipped entirely rather than showing
	 * through underneath it. Only applies to the anchored (still-held)
	 * bar; the floating/fading remnant always uses the flat color. */
	bool use_custom_hold_bar;

	uint32_t color_idle;
	uint32_t color_pressed;
	bool has_custom_skin; /* if true, the box/label are skipped here; the
				* caller draws its own texture on top instead */
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
	int corner_radius;     /* 0 = square corners; up to key_size/2 for a pill/circle look */
	bool show_press_trail; /* show the growing/shrinking press bar above each key */
	bool show_key_labels;
	uint32_t trail_color;     /* press bar color */
	int press_bar_max_height; /* px cap on how far the press bar can grow */
	int press_bar_width;      /* perpendicular thickness of the press bar, px; 0 = match key_size */
	bool bars_mode;           /* draw thin equalizer-style bars instead of square boxes */

	bool show_kps_graph;
	uint32_t kps_graph_color;
	float kps_history[JKPS_KPS_GRAPH_SAMPLES]; /* oldest first, newest last */

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

/* Fills out_x/out_y[0..p->num_keys-1] with the same box positions
 * jkps_render_frame uses internally. Lets the caller draw its own texture
 * (a custom key skin image) at the exact right spot on top of the base
 * texture, without duplicating the layout math. */
void jkps_render_get_key_positions(const struct jkps_render_params *p, int out_x[], int out_y[]);

/* Renders the current state into `pixels`, a caller-allocated buffer of at
 * least width*height*4 bytes (BGRA, straight/non-premultiplied alpha,
 * matching GS_BGRA). Returns false if rendering failed (Windows only). */
bool jkps_render_frame(const struct jkps_render_params *p, uint32_t width, uint32_t height, uint8_t *pixels);

#ifdef __cplusplus
}
#endif
