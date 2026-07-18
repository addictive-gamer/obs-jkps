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

	int trail_w = 0, trail_h = 0;
	if (p->show_press_trail) {
		int trail_extent = JKPS_TRAIL_SEGMENTS * (p->key_size + p->key_spacing);
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
	int height = keys_h + stats_line_height(p);
	height += trail_h;

	*out_width = (uint32_t)(width + CANVAS_PADDING * 2);
	*out_height = (uint32_t)(height + CANVAS_PADDING * 2);
}

void jkps_render_get_key_positions(const struct jkps_render_params *p, int out_x[], int out_y[])
{
	int n = p->num_keys > 0 ? p->num_keys : 0;
	int trail_y_shift =
		(p->show_press_trail && !p->vertical_layout) ? JKPS_TRAIL_SEGMENTS * (p->key_size + p->key_spacing) : 0;

	for (int i = 0; i < n; i++) {
		if (p->vertical_layout) {
			out_x[i] = CANVAS_PADDING;
			out_y[i] = CANVAS_PADDING + i * (p->key_size + p->key_spacing);
		} else {
			out_x[i] = CANVAS_PADDING + i * (p->key_size + p->key_spacing);
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
	int trail_y_shift =
		(p->show_press_trail && !p->vertical_layout) ? JKPS_TRAIL_SEGMENTS * (p->key_size + p->key_spacing) : 0;

	for (int i = 0; i < n; i++) {
		int x, y;
		if (p->vertical_layout) {
			x = CANVAS_PADDING;
			y = CANVAS_PADDING + i * (p->key_size + p->key_spacing);
		} else {
			x = CANVAS_PADDING + i * (p->key_size + p->key_spacing);
			y = CANVAS_PADDING + trail_y_shift;
		}

		if (p->show_press_trail) {
			for (int s = 0; s < JKPS_TRAIL_SEGMENTS; s++) {
				float intensity = p->keys[i].trail[s];
				if (intensity <= 0.01f)
					continue;

				/* Taper the far end of the trail smoothly instead of
				 * letting it pop out of existence at the last segment. */
				float tip_fade = (float)(JKPS_TRAIL_SEGMENTS - s) / (float)JKPS_TRAIL_SEGMENTS;
				intensity *= tip_fade;
				if (intensity <= 0.01f)
					continue;

				int seg_x = x, seg_y = y;
				int offset = (s + 1) * (p->key_size + p->key_spacing);
				if (p->vertical_layout)
					seg_x += offset; /* trail extends rightward per row */
				else
					seg_y -= offset; /* trail extends upward per column */

				uint32_t base = p->trail_color;
				uint8_t base_a = (uint8_t)((base >> 24) & 0xFF);
				uint8_t seg_a = (uint8_t)(base_a * intensity);
				uint32_t seg_color = (base & 0x00FFFFFFu) | ((uint32_t)seg_a << 24);
				fill_rounded_rect(pixels, width, height, seg_x, seg_y, p->key_size, p->key_size,
						  seg_color, p->corner_radius);
			}
		}

		uint32_t box_color = p->keys[i].down ? p->keys[i].color_pressed : p->keys[i].color_idle;
		if (!p->keys[i].has_custom_skin) {
			fill_rounded_rect(pixels, width, height, x, y, p->key_size, p->key_size, box_color,
					  p->corner_radius);
			if (p->show_key_labels)
				draw_text_centered(pixels, width, height, x, y, p->key_size, p->key_size,
						   p->keys[i].label, p->key_font_size, p->color_text);
		}
	}

	int line_h = stats_line_height(p);
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
