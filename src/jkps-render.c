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

#include "jkps-render.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define STATS_PADDING 10
#define CANVAS_PADDING 4
#define KPS_GRAPH_HEIGHT 50
#define KPS_GRAPH_MARGIN 8

static int stats_line_height(const struct jkps_render_params *p)
{
	if (!p->show_kps && !p->show_total && !p->show_bpm)
		return 0;
	return p->stats_font_size + STATS_PADDING;
}

void jkps_render_measure(const struct jkps_render_params *p, uint32_t *out_width, uint32_t *out_height)
{
	int n = p->num_keys > 0 ? p->num_keys : 1;
	int keys_w, keys_h;

	if (p->vertical_layout) {
		keys_w = p->key_size;
		keys_h = n * p->key_size + (n - 1) * p->key_spacing;
	} else {
		keys_w = n * p->key_size + (n - 1) * p->key_spacing;
		keys_h = p->key_size;
	}

	/* If the press bar is configured wider than the key box, it overhangs
	 * past the first/last key's outer edge (it's centered against the
	 * key). Widen the canvas along that same axis so it isn't clipped;
	 * jkps_render_get_key_positions/render_frame shift the keys inward
	 * by half of this to keep them centered in the extra room. */
	int perp_extra = 0;
	if (p->show_press_trail && p->press_bar_width > p->key_size)
		perp_extra = p->press_bar_width - p->key_size;
	if (p->vertical_layout)
		keys_h += perp_extra;
	else
		keys_w += perp_extra;

	int trail_w = 0, trail_h = 0;
	if (p->show_press_trail) {
		int trail_extent = p->press_bar_max_height;
		if (p->vertical_layout)
			trail_w = trail_extent;
		else
			trail_h = trail_extent;
	}

	/* Reserve enough width for a generous stats string so the canvas does
	 * not need to be resized on every frame just because a counter grew
	 * an extra digit (KPS/BPM up to 3 digits, Total up to 7 digits). */
	int stats_w = 0;
	if (p->show_kps || p->show_total || p->show_bpm) {
		int approx_chars = 0;
		if (p->show_kps)
			approx_chars += 9; /* "KPS: 999 " */
		if (p->show_total)
			approx_chars += 15; /* "Total: 9999999 " */
		if (p->show_bpm)
			approx_chars += 10; /* "BPM: 999" */
		stats_w = approx_chars * (p->stats_font_size * 6 / 10);
	}

	int width = keys_w > stats_w ? keys_w : stats_w;
	width += trail_w;
	int graph_h = p->show_kps_graph ? (KPS_GRAPH_HEIGHT + KPS_GRAPH_MARGIN) : 0;
	int height = keys_h + stats_line_height(p) + graph_h;
	height += trail_h;

	*out_width = (uint32_t)(width + CANVAS_PADDING * 2);
	*out_height = (uint32_t)(height + CANVAS_PADDING * 2);
}

void jkps_render_get_key_positions(const struct jkps_render_params *p, int out_x[], int out_y[])
{
	int n = p->num_keys > 0 ? p->num_keys : 0;
	int trail_y_shift = (p->show_press_trail && !p->vertical_layout) ? p->press_bar_max_height : 0;

	int perp_extra = 0;
	if (p->show_press_trail && p->press_bar_width > p->key_size)
		perp_extra = p->press_bar_width - p->key_size;
	int perp_shift = perp_extra / 2;

	for (int i = 0; i < n; i++) {
		if (p->vertical_layout) {
			out_x[i] = CANVAS_PADDING;
			out_y[i] = CANVAS_PADDING + perp_shift + i * (p->key_size + p->key_spacing);
		} else {
			out_x[i] = CANVAS_PADDING + perp_shift + i * (p->key_size + p->key_spacing);
			out_y[i] = CANVAS_PADDING + trail_y_shift;
		}
	}
}

static void blend_over(uint8_t *dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	if (a == 0)
		return;
	if (a == 255) {
		dst[0] = b;
		dst[1] = g;
		dst[2] = r;
		dst[3] = 255;
		return;
	}

	float sa = a / 255.0f;
	float da = dst[3] / 255.0f;
	float outA = sa + da * (1.0f - sa);
	if (outA <= 0.0001f) {
		dst[0] = dst[1] = dst[2] = dst[3] = 0;
		return;
	}

	float outB = (b * sa + dst[0] * da * (1.0f - sa)) / outA;
	float outG = (g * sa + dst[1] * da * (1.0f - sa)) / outA;
	float outR = (r * sa + dst[2] * da * (1.0f - sa)) / outA;

	dst[0] = (uint8_t)(outB + 0.5f);
	dst[1] = (uint8_t)(outG + 0.5f);
	dst[2] = (uint8_t)(outR + 0.5f);
	dst[3] = (uint8_t)(outA * 255.0f + 0.5f);
}

static void fill_rect(uint8_t *pixels, uint32_t width, uint32_t height, int x, int y, int w, int h, uint32_t color)
{
	uint8_t r = (uint8_t)(color & 0xFF);
	uint8_t g = (uint8_t)((color >> 8) & 0xFF);
	uint8_t b = (uint8_t)((color >> 16) & 0xFF);
	uint8_t a = (uint8_t)((color >> 24) & 0xFF);

	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = (x + w) > (int)width ? (int)width : (x + w);
	int y1 = (y + h) > (int)height ? (int)height : (y + h);

	for (int py = y0; py < y1; py++) {
		uint8_t *row = pixels + (size_t)py * width * 4;
		for (int px = x0; px < x1; px++)
			blend_over(row + (size_t)px * 4, r, g, b, a);
	}
}

/* Same as fill_rect, but with optionally rounded corners. Corners get a
 * 1px soft falloff so they don't look jagged at typical key-box sizes. */
static void fill_rounded_rect(uint8_t *pixels, uint32_t width, uint32_t height, int x, int y, int w, int h,
			      uint32_t color, int radius)
{
	if (radius <= 0) {
		fill_rect(pixels, width, height, x, y, w, h, color);
		return;
	}

	uint8_t r = (uint8_t)(color & 0xFF);
	uint8_t g = (uint8_t)((color >> 8) & 0xFF);
	uint8_t b = (uint8_t)((color >> 16) & 0xFF);
	uint8_t a = (uint8_t)((color >> 24) & 0xFF);

	int max_radius = (w < h ? w : h) / 2;
	if (radius > max_radius)
		radius = max_radius;

	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = (x + w) > (int)width ? (int)width : (x + w);
	int y1 = (y + h) > (int)height ? (int)height : (y + h);

	int rx0 = x + radius;
	int rx1 = x + w - radius;
	int ry0 = y + radius;
	int ry1 = y + h - radius;

	for (int py = y0; py < y1; py++) {
		uint8_t *row = pixels + (size_t)py * width * 4;
		bool outside_v = (py < ry0 || py >= ry1);

		for (int px = x0; px < x1; px++) {
			bool outside_h = (px < rx0 || px >= rx1);

			if (outside_h && outside_v) {
				int cx = (px < rx0) ? rx0 : rx1;
				int cy = (py < ry0) ? ry0 : ry1;
				float dx = (float)(px - cx) + 0.5f;
				float dy = (float)(py - cy) + 0.5f;
				float dist = sqrtf(dx * dx + dy * dy);

				if (dist > radius + 0.5f)
					continue; /* fully outside the rounded corner */

				if (dist > radius - 0.5f) {
					/* 1px soft edge for basic anti-aliasing */
					float coverage = (radius + 0.5f) - dist;
					uint8_t edge_a = (uint8_t)(a * coverage + 0.5f);
					blend_over(row + (size_t)px * 4, r, g, b, edge_a);
					continue;
				}
			}

			blend_over(row + (size_t)px * 4, r, g, b, a);
		}
	}
}

#if defined(_WIN32)
#include <windows.h>

/* Renders `text` in `color` centered inside the (x, y, w, h) rectangle of
 * `pixels`, using GDI's grayscale anti-aliasing as a per-pixel alpha mask
 * (classic "white text on black" trick: since ANTIALIASED_QUALITY forces
 * R == G == B, the pixel intensity doubles as the glyph coverage/alpha). */
static void draw_text_centered(uint8_t *pixels, uint32_t canvas_w, uint32_t canvas_h, int x, int y, int w, int h,
			       const char *text, int font_size, uint32_t color)
{
	if (!text || !text[0] || w <= 0 || h <= 0)
		return;

	HDC screen_dc = GetDC(NULL);
	HDC mem_dc = CreateCompatibleDC(screen_dc);

	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h; /* top-down */
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *bits = NULL;
	HBITMAP dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, dib);

	RECT full = {0, 0, w, h};
	HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
	FillRect(mem_dc, &full, black);

	LOGFONTA lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = -font_size;
	lf.lfWeight = FW_SEMIBOLD;
	lf.lfQuality = ANTIALIASED_QUALITY; /* force grayscale AA -> R == G == B */
	lf.lfCharSet = DEFAULT_CHARSET;
	strncpy_s(lf.lfFaceName, sizeof(lf.lfFaceName), "Segoe UI", _TRUNCATE);
	HFONT font = CreateFontIndirectA(&lf);
	HFONT old_font = (HFONT)SelectObject(mem_dc, font);

	SetTextColor(mem_dc, RGB(255, 255, 255));
	SetBkColor(mem_dc, RGB(0, 0, 0));
	SetBkMode(mem_dc, OPAQUE);

	RECT text_rect = {0, 0, w, h};
	DrawTextA(mem_dc, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

	uint8_t r = (uint8_t)(color & 0xFF);
	uint8_t g = (uint8_t)((color >> 8) & 0xFF);
	uint8_t b = (uint8_t)((color >> 16) & 0xFF);
	uint32_t *src = (uint32_t *)bits;

	for (int py = 0; py < h; py++) {
		int dest_y = y + py;
		if (dest_y < 0 || dest_y >= (int)canvas_h)
			continue;
		for (int px = 0; px < w; px++) {
			int dest_x = x + px;
			if (dest_x < 0 || dest_x >= (int)canvas_w)
				continue;

			uint32_t pixel = src[(size_t)py * w + px];
			uint8_t alpha = (uint8_t)(pixel & 0xFF); /* B channel == intensity */

			blend_over(pixels + ((size_t)dest_y * canvas_w + dest_x) * 4, r, g, b, alpha);
		}
	}

	SelectObject(mem_dc, old_font);
	DeleteObject(font);
	SelectObject(mem_dc, old_bmp);
	DeleteObject(dib);
	DeleteDC(mem_dc);
	ReleaseDC(NULL, screen_dc);
}

bool jkps_render_frame(const struct jkps_render_params *p, uint32_t width, uint32_t height, uint8_t *pixels)
{
	memset(pixels, 0, (size_t)width * height * 4);
	fill_rect(pixels, width, height, 0, 0, (int)width, (int)height, p->color_bg);

	int n = p->num_keys > 0 ? p->num_keys : 0;
	int trail_y_shift = (p->show_press_trail && !p->vertical_layout) ? p->press_bar_max_height : 0;

	int perp_extra = 0;
	if (p->show_press_trail && p->press_bar_width > p->key_size)
		perp_extra = p->press_bar_width - p->key_size;
	int perp_shift = perp_extra / 2;

	for (int i = 0; i < n; i++) {
		int x, y;
		if (p->vertical_layout) {
			x = CANVAS_PADDING;
			y = CANVAS_PADDING + perp_shift + i * (p->key_size + p->key_spacing);
		} else {
			x = CANVAS_PADDING + perp_shift + i * (p->key_size + p->key_spacing);
			y = CANVAS_PADDING + trail_y_shift;
		}

		if (p->show_press_trail) {
			int thickness = p->press_bar_width > 0 ? p->press_bar_width : p->key_size;

			int bar_len = (int)(p->keys[i].press_bar_px + 0.5f);
			if (bar_len > 0) {
				if (bar_len > p->press_bar_max_height)
					bar_len = p->press_bar_max_height;

				/* The bar is anchored at the key box and grows away
				 * from it - upward for a horizontal row of keys,
				 * rightward for a vertical column - matching how a
				 * key-press-visualization meter reads regardless of
				 * layout. Its perpendicular thickness may differ from
				 * key_size, so it's centered against the key on that
				 * axis instead of assumed flush with it. */
				int bar_x = x, bar_y = y;
				if (p->vertical_layout) {
					bar_x += p->key_size; /* grows rightward, flush against the key */
					bar_y += (p->key_size - thickness) / 2;
				} else {
					bar_y -= bar_len; /* grows upward, flush against the key */
					bar_x += (p->key_size - thickness) / 2;
				}

				int bar_w = p->vertical_layout ? bar_len : thickness;
				int bar_h = p->vertical_layout ? thickness : bar_len;

				if (!p->keys[i].use_custom_hold_bar)
					fill_rounded_rect(pixels, width, height, bar_x, bar_y, bar_w, bar_h,
							  p->trail_color, p->corner_radius);
			}

			/* The just-released remnants: each detached from the key at
			 * its own moment, drifting further away and fading out -
			 * instead of shrinking back down, they keep traveling like a
			 * released hold-note tail. Independent slots so back-to-back
			 * taps don't cut each other's animation off. */
			for (int b = 0; b < JKPS_MAX_FLOAT_BARS; b++) {
				if (p->keys[i].float_bar_len[b] <= 0.5f || p->keys[i].float_bar_alpha[b] <= 0.01f)
					continue;

				int flen = (int)(p->keys[i].float_bar_len[b] + 0.5f);
				int fdrift = (int)(p->keys[i].float_bar_drift[b] + 0.5f);

				int fx = x, fy = y;
				if (p->vertical_layout) {
					fx += p->key_size + fdrift;
					fy += (p->key_size - thickness) / 2;
				} else {
					fy -= fdrift + flen;
					fx += (p->key_size - thickness) / 2;
				}

				int fw = p->vertical_layout ? flen : thickness;
				int fh = p->vertical_layout ? thickness : flen;

				uint32_t base = p->trail_color;
				uint8_t base_a = (uint8_t)((base >> 24) & 0xFF);
				uint8_t faded_a = (uint8_t)((float)base_a * p->keys[i].float_bar_alpha[b]);
				uint32_t faded_color = (base & 0x00FFFFFFu) | ((uint32_t)faded_a << 24);

				fill_rounded_rect(pixels, width, height, fx, fy, fw, fh, faded_color, p->corner_radius);
			}
		}

		uint32_t box_color = p->keys[i].down ? p->keys[i].color_pressed : p->keys[i].color_idle;
		if (!p->keys[i].has_custom_skin) {
			if (p->bars_mode) {
				/* press_level is always maintained (decays on release,
				 * jumps to 1.0 on press) regardless of whether the
				 * multi-segment trail visual is enabled, so it
				 * doubles nicely as a VU-meter-style fill level. */
				float fill_frac = p->keys[i].press_level;
				if (fill_frac < 0.06f)
					fill_frac = p->keys[i].down ? 1.0f : 0.06f;
				if (fill_frac > 1.0f)
					fill_frac = 1.0f;

				int bar_h = (p->key_size * 6) / 10;
				if (bar_h < 4)
					bar_h = 4;
				int bar_y = y + (p->key_size - bar_h) / 2;
				int fill_h = (int)(bar_h * fill_frac);
				if (fill_h < 2)
					fill_h = 2;

				/* Dim background track spans the full key width, so
				 * it always reads as a horizontal strip; the active
				 * fill grows downward from its top edge instead of
				 * sideways, like a level meter filling from above. */
				fill_rounded_rect(pixels, width, height, x, bar_y, p->key_size, bar_h,
						  p->keys[i].color_idle, p->corner_radius);
				fill_rounded_rect(pixels, width, height, x, bar_y, p->key_size, fill_h,
						  p->keys[i].color_pressed, p->corner_radius);
			} else {
				fill_rounded_rect(pixels, width, height, x, y, p->key_size, p->key_size, box_color,
						  p->corner_radius);
			}
			if (p->show_key_labels)
				draw_text_centered(pixels, width, height, x, y, p->key_size, p->key_size,
						   p->keys[i].label, p->key_font_size, p->color_text);
		}
	}

	int line_h = stats_line_height(p);

	if (p->show_kps_graph) {
		int graph_bottom = (int)height - CANVAS_PADDING - line_h;
		int graph_top = graph_bottom - KPS_GRAPH_HEIGHT;
		int graph_x0 = CANVAS_PADDING;
		int graph_w = (int)width - CANVAS_PADDING * 2;

		/* Faint background track so the graph area reads even at 0 KPS. */
		fill_rect(pixels, width, height, graph_x0, graph_top, graph_w, KPS_GRAPH_HEIGHT,
			  (p->kps_graph_color & 0x00FFFFFFu) | 0x20000000u);

		float max_val = 1.0f;
		for (int j = 0; j < JKPS_KPS_GRAPH_SAMPLES; j++)
			if (p->kps_history[j] > max_val)
				max_val = p->kps_history[j];

		int bar_w = graph_w / JKPS_KPS_GRAPH_SAMPLES;
		if (bar_w < 1)
			bar_w = 1;

		for (int j = 0; j < JKPS_KPS_GRAPH_SAMPLES; j++) {
			int bar_h = (int)((p->kps_history[j] / max_val) * KPS_GRAPH_HEIGHT);
			if (bar_h < 1)
				bar_h = 1;
			int bx = graph_x0 + j * bar_w;
			int by = graph_bottom - bar_h;
			fill_rect(pixels, width, height, bx, by, bar_w > 1 ? bar_w - 1 : 1, bar_h, p->kps_graph_color);
		}
	}

	if (line_h > 0) {
		char buf[160];
		buf[0] = '\0';

		if (p->show_kps)
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "KPS: %.0f  ", p->kps);
		if (p->show_total)
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "Total: %llu  ",
				 (unsigned long long)p->total_presses);
		if (p->show_bpm)
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "BPM: %.0f", p->bpm);

		int stats_y = (int)height - line_h - CANVAS_PADDING + STATS_PADDING / 2;
		draw_text_centered(pixels, width, height, CANVAS_PADDING, stats_y, (int)width - CANVAS_PADDING * 2,
				   line_h, buf, p->stats_font_size, p->stats_color);
	}

	return true;
}

#else /* Non-Windows: input polling is unavailable, draw a static placeholder */

bool jkps_render_frame(const struct jkps_render_params *p, uint32_t width, uint32_t height, uint8_t *pixels)
{
	memset(pixels, 0, (size_t)width * height * 4);
	fill_rect(pixels, width, height, 0, 0, (int)width, (int)height, p->color_bg);
	return true;
}

#endif
