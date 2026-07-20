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

#include "jkps-source.h"
#include "jkps-input.h"
#include "jkps-render.h"
#include "jkps-keynames.h"
#include "jkps-noteskin.h"

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default key layout: D F J K, the classic osu!mania / 4-key layout, plus
 * four more slots left disabled by default that the user can turn on and
 * rebind for 6k/7k/8k mania, ADOFAI, etc. */
static const int default_vk[JKPS_MAX_KEYS] = {'D', 'F', 'J', 'K', 'S', 'L', 'A', ';'};
static const bool default_enabled[JKPS_MAX_KEYS] = {true, true, true, true, false, false, false, false};

#define TEXTURE_UPDATE_INTERVAL_MS 33 /* ~30 Hz refresh for the GDI-rendered texture */

/* Native Skins are the procedural color themes above (jkps_themes) and work
 * at any key count. Funkin' Skins load a real Sparrow/TexturePacker atlas
 * (see jkps-noteskin.h) from a folder the user points the plugin at; it
 * starts out empty since obs-jkps ships with no fan art of its own. Atlas
 * noteskins are inherently 4-directional, so picking one locks the layout
 * to 4K. */
enum jkps_skin_category {
	JKPS_SKIN_CAT_NATIVE = 0,
	JKPS_SKIN_CAT_FUNKIN = 1,
};

/* FNF's standard lane order (left, down, up, right) mapped onto the
 * plugin's first 4 key slots, left to right - matches the jkps_pal_fnf_*
 * palettes above and is fixed regardless of which physical keys the user
 * has slots 0-3 bound to. */
static const enum jkps_noteskin_dir jkps_slot_to_dir[4] = {JKPS_DIR_LEFT, JKPS_DIR_DOWN, JKPS_DIR_UP, JKPS_DIR_RIGHT};

struct jkps_source_context {
	obs_source_t *source;

	bool key_enabled[JKPS_MAX_KEYS];
	int key_vk[JKPS_MAX_KEYS];
	char key_label[JKPS_MAX_KEYS][32];

	bool vertical_layout;
	int key_size;
	int key_spacing;
	int key_font_size;
	int corner_radius;
	bool show_press_trail;
	bool show_key_labels;
	uint32_t trail_color;
	int press_bar_max_height;
	int press_bar_rise_speed; /* 1..100, % of remaining distance closed per tick (~30/s) */
	bool bars_mode;
	bool show_kps_graph;
	uint32_t kps_graph_color;
	float kps_history[JKPS_KPS_GRAPH_SAMPLES];
	float press_level[JKPS_MAX_KEYS];  /* 0..1, drives bars-mode VU fill */
	float press_bar_px[JKPS_MAX_KEYS]; /* current press-bar height in px */
	bool press_bar_was_down[JKPS_MAX_KEYS];
	float float_bar_len[JKPS_MAX_KEYS];   /* 0 = no floating remnant active */
	float float_bar_drift[JKPS_MAX_KEYS]; /* px traveled since detaching from the key */
	float float_bar_alpha[JKPS_MAX_KEYS]; /* 1..0 fade as it exits the margin */

	uint32_t key_color_idle[JKPS_MAX_KEYS];
	uint32_t key_color_pressed[JKPS_MAX_KEYS];

	/* Optional per-key custom skin images. If a key has a loaded idle
	 * and/or pressed image, the renderer skips drawing the colored box
	 * for it and jkps_source_video_render draws this texture on top
	 * instead, at the position cached in key_screen_x/y below. */
	char key_skin_idle_path[JKPS_MAX_KEYS][512];
	char key_skin_pressed_path[JKPS_MAX_KEYS][512];
	gs_image_file_t key_skin_idle_img[JKPS_MAX_KEYS];
	gs_image_file_t key_skin_pressed_img[JKPS_MAX_KEYS];
	bool key_skin_idle_loaded[JKPS_MAX_KEYS];
	bool key_skin_pressed_loaded[JKPS_MAX_KEYS];
	int key_screen_x[JKPS_MAX_KEYS];
	int key_screen_y[JKPS_MAX_KEYS];

	/* Atlas-format noteskin (Funkin' Skins), active only when
	 * skin_category selects it. */
	bool custom_skins_enabled;
	enum jkps_skin_category skin_category;

	char funkin_folder[512];
	char funkin_skin_xml[512];
	struct jkps_noteskin funkin_noteskin;
	bool funkin_noteskin_loaded;

	uint32_t color_text;
	uint32_t color_bg;

	bool show_kps;
	bool show_total;
	bool show_bpm;
	int stats_font_size;
	uint32_t stats_color;

	struct jkps_key_state keys[JKPS_MAX_KEYS];
	struct jkps_stats stats;

	uint32_t width;
	uint32_t height;
	uint8_t *pixel_buffer;
	gs_texture_t *texture;
	uint64_t last_texture_update_ms;

	obs_hotkey_id reset_hotkey_id;
};

static const char *jkps_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("JkpsSource.Name");
}

/* (Re)loads a custom key-skin image only if the path actually changed, so
 * we don't hit the disk/GPU on every settings tweak (update() fires for
 * any changed setting, not just skin paths). Passing an empty path frees
 * whatever was loaded and falls back to the colored box renderer. */
static void jkps_load_key_skin(gs_image_file_t *img, bool *loaded, char *stored_path, size_t stored_path_size,
			       const char *new_path)
{
	if (strcmp(stored_path, new_path) == 0)
		return;

	if (*loaded) {
		obs_enter_graphics();
		gs_image_file_free(img);
		obs_leave_graphics();
		*loaded = false;
	}

	strncpy(stored_path, new_path, stored_path_size - 1);
	stored_path[stored_path_size - 1] = '\0';

	if (new_path[0] == '\0')
		return;

	gs_image_file_init(img, new_path);
	obs_enter_graphics();
	gs_image_file_init_texture(img);
	obs_leave_graphics();
	*loaded = img->loaded;
}

/* Mirrors jkps_load_key_skin's "only touch the disk/GPU if the path
 * actually changed" behavior, but for a whole atlas pack instead of a
 * single flat image. */
static void jkps_load_noteskin(struct jkps_noteskin *ns, bool *loaded, char *stored_path, size_t stored_path_size,
			       const char *new_path)
{
	if (strcmp(stored_path, new_path) == 0)
		return;

	if (*loaded) {
		obs_enter_graphics();
		jkps_noteskin_free(ns);
		obs_leave_graphics();
		*loaded = false;
	}

	strncpy(stored_path, new_path, stored_path_size - 1);
	stored_path[stored_path_size - 1] = '\0';

	if (new_path[0] == '\0')
		return;

	obs_enter_graphics();
	*loaded = jkps_noteskin_load(new_path, ns);
	obs_leave_graphics();
}

/* Which atlas (if any) should currently be drawn, based on the selected
 * category. NULL means "no atlas active" - fall back to flat per-key skin
 * images or the colored box, same as before this feature existed. */
static struct jkps_noteskin *jkps_active_noteskin(struct jkps_source_context *ctx)
{
	if (ctx->skin_category == JKPS_SKIN_CAT_FUNKIN && ctx->funkin_noteskin_loaded)
		return &ctx->funkin_noteskin;
	return NULL;
}

static void rebuild_canvas(struct jkps_source_context *ctx)
{
	struct jkps_render_params p;
	memset(&p, 0, sizeof(p));
	p.num_keys = JKPS_MAX_KEYS;
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		p.keys[i].label = ctx->key_label[i];
	}
	/* Measurement only depends on layout/sizes and which stat lines are
	 * enabled, so the enabled-key filtering below is fine to skip here;
	 * we still pass JKPS_MAX_KEYS worth of slots trimmed to active count. */
	int active = 0;
	for (int i = 0; i < JKPS_MAX_KEYS; i++)
		if (ctx->key_enabled[i])
			active++;
	p.num_keys = active > 0 ? active : 1;
	p.vertical_layout = ctx->vertical_layout;
	p.key_size = ctx->key_size;
	p.key_spacing = ctx->key_spacing;
	p.key_font_size = ctx->key_font_size;
	p.corner_radius = ctx->corner_radius;
	p.show_press_trail = ctx->show_press_trail;
	p.press_bar_max_height = ctx->press_bar_max_height;
	p.show_kps_graph = ctx->show_kps_graph;
	p.show_kps = ctx->show_kps;
	p.show_total = ctx->show_total;
	p.show_bpm = ctx->show_bpm;
	p.stats_font_size = ctx->stats_font_size;

	uint32_t w, h;
	jkps_render_measure(&p, &w, &h);

	if (w != ctx->width || h != ctx->height || !ctx->pixel_buffer) {
		ctx->width = w;
		ctx->height = h;
		free(ctx->pixel_buffer);
		ctx->pixel_buffer = malloc((size_t)w * h * 4);

		obs_enter_graphics();
		if (ctx->texture) {
			gs_texture_destroy(ctx->texture);
			ctx->texture = NULL;
		}
		ctx->texture = gs_texture_create(w, h, GS_BGRA, 1, NULL, GS_DYNAMIC);
		obs_leave_graphics();
	}
}

static void jkps_source_reset_stats(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	struct jkps_source_context *ctx = data;
	if (!pressed || !ctx)
		return;
	jkps_stats_reset(&ctx->stats, ctx->keys, JKPS_MAX_KEYS);
}

static bool jkps_reset_button_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct jkps_source_context *ctx = data;
	jkps_stats_reset(&ctx->stats, ctx->keys, JKPS_MAX_KEYS);
	return false;
}

static void jkps_source_update(void *data, obs_data_t *settings)
{
	struct jkps_source_context *ctx = data;

	char key[32];
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		snprintf(key, sizeof(key), "key_enabled_%d", i);
		ctx->key_enabled[i] = obs_data_get_bool(settings, key);

		snprintf(key, sizeof(key), "key_vcode_%d", i);
		ctx->key_vk[i] = (int)obs_data_get_int(settings, key);
		ctx->keys[i].vk = ctx->key_vk[i];

		snprintf(key, sizeof(key), "key_label_%d", i);
		const char *custom = obs_data_get_string(settings, key);
		if (custom && custom[0]) {
			strncpy(ctx->key_label[i], custom, sizeof(ctx->key_label[i]) - 1);
			ctx->key_label[i][sizeof(ctx->key_label[i]) - 1] = '\0';
		} else {
			strncpy(ctx->key_label[i], jkps_keyname_default_label(ctx->key_vk[i]),
				sizeof(ctx->key_label[i]) - 1);
			ctx->key_label[i][sizeof(ctx->key_label[i]) - 1] = '\0';
		}

		snprintf(key, sizeof(key), "key_color_idle_%d", i);
		ctx->key_color_idle[i] = (uint32_t)obs_data_get_int(settings, key);

		snprintf(key, sizeof(key), "key_color_pressed_%d", i);
		ctx->key_color_pressed[i] = (uint32_t)obs_data_get_int(settings, key);

		snprintf(key, sizeof(key), "key_skin_idle_%d", i);
		jkps_load_key_skin(&ctx->key_skin_idle_img[i], &ctx->key_skin_idle_loaded[i],
				   ctx->key_skin_idle_path[i], sizeof(ctx->key_skin_idle_path[i]),
				   obs_data_get_string(settings, key));

		snprintf(key, sizeof(key), "key_skin_pressed_%d", i);
		jkps_load_key_skin(&ctx->key_skin_pressed_img[i], &ctx->key_skin_pressed_loaded[i],
				   ctx->key_skin_pressed_path[i], sizeof(ctx->key_skin_pressed_path[i]),
				   obs_data_get_string(settings, key));
	}

	ctx->custom_skins_enabled = obs_data_get_bool(settings, "custom_skins_enabled");
	ctx->skin_category = ctx->custom_skins_enabled
				     ? (enum jkps_skin_category)obs_data_get_int(settings, "skin_category")
				     : JKPS_SKIN_CAT_NATIVE;

	jkps_load_noteskin(&ctx->funkin_noteskin, &ctx->funkin_noteskin_loaded, ctx->funkin_skin_xml,
			   sizeof(ctx->funkin_skin_xml), obs_data_get_string(settings, "funkin_skin_xml"));

	const char *funkin_folder = obs_data_get_string(settings, "funkin_folder");
	strncpy(ctx->funkin_folder, funkin_folder, sizeof(ctx->funkin_folder) - 1);
	ctx->funkin_folder[sizeof(ctx->funkin_folder) - 1] = '\0';

	/* Atlas noteskins are inherently 4-directional (left/down/up/right),
	 * so picking one locks the layout to the first 4 key slots - Native
	 * Skins remains the only category that supports 5K-8K. */
	if (ctx->skin_category != JKPS_SKIN_CAT_NATIVE) {
		for (int i = 4; i < JKPS_MAX_KEYS; i++)
			ctx->key_enabled[i] = false;
	}

	ctx->vertical_layout = obs_data_get_bool(settings, "vertical_layout");
	ctx->key_size = (int)obs_data_get_int(settings, "key_size");
	ctx->key_spacing = (int)obs_data_get_int(settings, "key_spacing");
	ctx->key_font_size = (int)obs_data_get_int(settings, "key_font_size");
	ctx->corner_radius = (int)obs_data_get_int(settings, "corner_radius");
	ctx->show_press_trail = obs_data_get_bool(settings, "show_press_trail");
	ctx->show_key_labels = obs_data_get_bool(settings, "show_key_labels");
	ctx->trail_color = (uint32_t)obs_data_get_int(settings, "trail_color");
	ctx->press_bar_max_height = (int)obs_data_get_int(settings, "press_bar_max_height");
	ctx->press_bar_rise_speed = (int)obs_data_get_int(settings, "press_bar_rise_speed");
	ctx->bars_mode = obs_data_get_bool(settings, "bars_mode");
	ctx->show_kps_graph = obs_data_get_bool(settings, "show_kps_graph");
	ctx->kps_graph_color = (uint32_t)obs_data_get_int(settings, "kps_graph_color");

	ctx->color_text = (uint32_t)obs_data_get_int(settings, "color_text");
	ctx->color_bg = (uint32_t)obs_data_get_int(settings, "color_bg");

	ctx->show_kps = obs_data_get_bool(settings, "show_kps");
	ctx->show_total = obs_data_get_bool(settings, "show_total");
	ctx->show_bpm = obs_data_get_bool(settings, "show_bpm");
	ctx->stats_font_size = (int)obs_data_get_int(settings, "stats_font_size");
	ctx->stats_color = (uint32_t)obs_data_get_int(settings, "stats_color");

	rebuild_canvas(ctx);
}

static void jkps_source_get_defaults(obs_data_t *settings)
{
	char key[32];
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		snprintf(key, sizeof(key), "key_enabled_%d", i);
		obs_data_set_default_bool(settings, key, default_enabled[i]);

		snprintf(key, sizeof(key), "key_vcode_%d", i);
		obs_data_set_default_int(settings, key, default_vk[i]);

		snprintf(key, sizeof(key), "key_label_%d", i);
		obs_data_set_default_string(settings, key, "");

		snprintf(key, sizeof(key), "key_color_idle_%d", i);
		obs_data_set_default_int(settings, key, 0xFF3A3A3A);

		snprintf(key, sizeof(key), "key_color_pressed_%d", i);
		obs_data_set_default_int(settings, key, 0xFF37B2FF);

		snprintf(key, sizeof(key), "key_skin_idle_%d", i);
		obs_data_set_default_string(settings, key, "");

		snprintf(key, sizeof(key), "key_skin_pressed_%d", i);
		obs_data_set_default_string(settings, key, "");
	}

	obs_data_set_default_bool(settings, "vertical_layout", false);
	obs_data_set_default_int(settings, "key_size", 60);
	obs_data_set_default_int(settings, "key_spacing", 8);
	obs_data_set_default_int(settings, "key_font_size", 22);
	obs_data_set_default_int(settings, "corner_radius", 0);
	obs_data_set_default_bool(settings, "show_press_trail", false);
	obs_data_set_default_bool(settings, "show_key_labels", true);
	obs_data_set_default_int(settings, "trail_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "press_bar_max_height", 200);
	obs_data_set_default_int(settings, "press_bar_rise_speed", 15);
	obs_data_set_default_bool(settings, "bars_mode", false);
	obs_data_set_default_bool(settings, "show_kps_graph", false);
	obs_data_set_default_int(settings, "kps_graph_color", 0xFF37B2FF);

	obs_data_set_default_int(settings, "color_text", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "color_bg", 0x00000000);

	obs_data_set_default_bool(settings, "show_kps", true);
	obs_data_set_default_bool(settings, "show_total", true);
	obs_data_set_default_bool(settings, "show_bpm", false);
	obs_data_set_default_int(settings, "stats_font_size", 20);
	obs_data_set_default_int(settings, "stats_color", 0xFFFFFFFF);

	/* Custom (atlas) skins are opt-in and start on Native Skins - an
	 * empty Funkin'/Local folder is otherwise indistinguishable from
	 * "not configured yet" for a first-time user. */
	obs_data_set_default_bool(settings, "custom_skins_enabled", false);
	obs_data_set_default_int(settings, "skin_category", JKPS_SKIN_CAT_NATIVE);
	obs_data_set_default_int(settings, "native_theme", 0); /* jkps_themes[0] == Classic */
	obs_data_set_default_string(settings, "funkin_folder", "");
	obs_data_set_default_string(settings, "funkin_skin_xml", "");
	obs_data_set_default_string(settings, "noteskins_info", obs_module_text("JkpsSource.NoteskinsInfo"));
}

/* Quick-apply theme presets: each bundles per-key colors with a corner
 * style, so one click gives a coherent look instead of the user having to
 * tweak up to 16 color pickers + a radius slider by hand.
 *
 * key_idle/key_pressed are small palettes (1 to 4 colors) that get cycled
 * across the 8 key slots: simple themes use a single repeated color, while
 * "directional" themes (DDR, FNF) use a 4-color palette matching the
 * default D F J K layout so each lane gets its own hue, the way those
 * games' own arrow skins do. Note: these are original color palettes
 * inspired by each game's look, not reproductions of any specific artist's
 * noteskin/skin graphics. */
struct jkps_theme {
	const char *id;         /* used as the button's property name */
	const char *locale_key; /* obs_module_text() key for the button label */
	const uint32_t *key_idle;
	const uint32_t *key_pressed;
	size_t key_color_count;
	uint32_t color_text;
	uint32_t color_bg;
	uint32_t stats_color;
	uint32_t trail_color;
	int corner_radius;
	bool bars_mode;
};

static const uint32_t jkps_pal_classic_idle[] = {0xFF3A3A3A};
static const uint32_t jkps_pal_classic_pressed[] = {0xFF37B2FF};

static const uint32_t jkps_pal_neon_idle[] = {0xFF2E1A3D};
static const uint32_t jkps_pal_neon_pressed[] = {0xFFFF2BD6};

static const uint32_t jkps_pal_retro_idle[] = {0xFF7C2D2D};
static const uint32_t jkps_pal_retro_pressed[] = {0xFF00D400};

static const uint32_t jkps_pal_minimal_idle[] = {0xFFE5E5E5};
static const uint32_t jkps_pal_minimal_pressed[] = {0xFF3A3A3A};

static const uint32_t jkps_pal_pastel_idle[] = {0xFFF7D6E8};
static const uint32_t jkps_pal_pastel_pressed[] = {0xFFC9F7DE};

/* osu!mania-inspired: alternating blue/white lanes with the game's brand
 * pink as the hit accent, and pill-shaped corners evoking circular skins. */
static const uint32_t jkps_pal_osu_idle[] = {0xFFF5B67E, 0xFFE0E0E0};
static const uint32_t jkps_pal_osu_pressed[] = {0xFF42B9F5, 0xFFAB66FF};

/* DDR-inspired: one common arcade-style 4-panel palette (blue / orange /
 * green / red). Actual DDR noteskins vary a lot by mix/version - this is a
 * clean original interpretation, easy to re-tweak per key afterwards. */
static const uint32_t jkps_pal_ddr_idle[] = {0xFF7A3B1A, 0xFF1A5E7A, 0xFF1A7A2E, 0xFF1A2A7A};
static const uint32_t jkps_pal_ddr_pressed[] = {0xFF33B0FF, 0xFF33D4FF, 0xFF4DDB6E, 0xFF4D5FFF};

/* FNF-inspired: the four lane hues most players recognize (purple/magenta,
 * cyan, green, red), left-to-right matching the default D F J K layout. */
static const uint32_t jkps_pal_fnf_idle[] = {0xFF6E2E5C, 0xFF2E6E6E, 0xFF2E6E31, 0xFF6E2E30};
static const uint32_t jkps_pal_fnf_pressed[] = {0xFFD680E0, 0xFFFFE666, 0xFF60FF6B, 0xFF7F7AFF};

/* Bars: a dim track color plus a bright accent fill, meant to be paired
 * with bars_mode's VU-meter-style rendering. */
static const uint32_t jkps_pal_bars_idle[] = {0xFF2A2A2A};
static const uint32_t jkps_pal_bars_pressed[] = {0xFF37B2FF};

static const struct jkps_theme jkps_themes[] = {
	{
		.id = "theme_classic",
		.locale_key = "JkpsSource.ThemeClassic",
		.key_idle = jkps_pal_classic_idle,
		.key_pressed = jkps_pal_classic_pressed,
		.key_color_count = 1,
		.color_text = 0xFFFFFFFF,
		.color_bg = 0x00000000,
		.stats_color = 0xFFFFFFFF,
		.trail_color = 0xFFFFFFFF,
		.corner_radius = 6,
	},
	{
		.id = "theme_neon",
		.locale_key = "JkpsSource.ThemeNeon",
		.key_idle = jkps_pal_neon_idle,
		.key_pressed = jkps_pal_neon_pressed,
		.key_color_count = 1,
		.color_text = 0xFF00F5FF,
		.color_bg = 0x00000000,
		.stats_color = 0xFF00F5FF,
		.trail_color = 0xFF00F5FF,
		.corner_radius = 30, /* pill-shaped at the default 60px key size */
	},
	{
		.id = "theme_retro",
		.locale_key = "JkpsSource.ThemeRetroArcade",
		.key_idle = jkps_pal_retro_idle,
		.key_pressed = jkps_pal_retro_pressed,
		.key_color_count = 1,
		.color_text = 0xFF000000,
		.color_bg = 0xFF0A0A0A,
		.stats_color = 0xFF00FF00,
		.trail_color = 0xFF00FF00,
		.corner_radius = 0, /* crisp, blocky pixel-art corners */
	},
	{
		.id = "theme_minimal",
		.locale_key = "JkpsSource.ThemeMinimal",
		.key_idle = jkps_pal_minimal_idle,
		.key_pressed = jkps_pal_minimal_pressed,
		.key_color_count = 1,
		.color_text = 0xFF222222,
		.color_bg = 0x00000000,
		.stats_color = 0xFF222222,
		.trail_color = 0xFF222222,
		.corner_radius = 2,
	},
	{
		.id = "theme_pastel",
		.locale_key = "JkpsSource.ThemePastel",
		.key_idle = jkps_pal_pastel_idle,
		.key_pressed = jkps_pal_pastel_pressed,
		.key_color_count = 1,
		.color_text = 0xFF6B4E6E,
		.color_bg = 0x00000000,
		.stats_color = 0xFF6B4E6E,
		.trail_color = 0xFF6B4E6E,
		.corner_radius = 16,
	},
	{
		.id = "theme_osu",
		.locale_key = "JkpsSource.ThemeOsu",
		.key_idle = jkps_pal_osu_idle,
		.key_pressed = jkps_pal_osu_pressed,
		.key_color_count = 2,
		.color_text = 0xFF333333,
		.color_bg = 0x00000000,
		.stats_color = 0xFF42B9F5,
		.trail_color = 0xFF42B9F5,
		.corner_radius = 30, /* circle/pill look, matching mania-style skins */
	},
	{
		.id = "theme_ddr",
		.locale_key = "JkpsSource.ThemeDdr",
		.key_idle = jkps_pal_ddr_idle,
		.key_pressed = jkps_pal_ddr_pressed,
		.key_color_count = 4,
		.color_text = 0xFFFFFFFF,
		.color_bg = 0x00000000,
		.stats_color = 0xFFFFFFFF,
		.trail_color = 0xFFFFFFFF,
		.corner_radius = 12,
	},
	{
		.id = "theme_fnf",
		.locale_key = "JkpsSource.ThemeFnf",
		.key_idle = jkps_pal_fnf_idle,
		.key_pressed = jkps_pal_fnf_pressed,
		.key_color_count = 4,
		.color_text = 0xFF1A1A1A,
		.color_bg = 0x00000000,
		.stats_color = 0xFFFFFFFF,
		.trail_color = 0xFFFFFFFF,
		.corner_radius = 10,
	},
	{
		.id = "theme_bars",
		.locale_key = "JkpsSource.ThemeBars",
		.key_idle = jkps_pal_bars_idle,
		.key_pressed = jkps_pal_bars_pressed,
		.key_color_count = 1,
		.color_text = 0xFFFFFFFF,
		.color_bg = 0x00000000,
		.stats_color = 0xFF37B2FF,
		.trail_color = 0xFF37B2FF,
		.corner_radius = 3,
		.bars_mode = true,
	},
};
#define JKPS_THEME_COUNT (sizeof(jkps_themes) / sizeof(jkps_themes[0]))

static bool jkps_theme_list_modified(void *data, obs_properties_t *props, obs_property_t *property,
				     obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct jkps_source_context *ctx = data;

	long long idx = obs_data_get_int(settings, "native_theme");
	if (idx < 0 || (size_t)idx >= JKPS_THEME_COUNT)
		return false;

	/* Only the theme picker's own value is guaranteed to already be
	 * committed here - the rest of the theme's settings (colors, corner
	 * radius, bars mode) still need to be pushed in ourselves, same as
	 * the old per-theme buttons did. */
	const struct jkps_theme *theme = &jkps_themes[idx];
	char key[32];
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		size_t slot = (size_t)i % theme->key_color_count;

		snprintf(key, sizeof(key), "key_color_idle_%d", i);
		obs_data_set_int(settings, key, theme->key_idle[slot]);

		snprintf(key, sizeof(key), "key_color_pressed_%d", i);
		obs_data_set_int(settings, key, theme->key_pressed[slot]);
	}

	obs_data_set_int(settings, "color_text", theme->color_text);
	obs_data_set_int(settings, "color_bg", theme->color_bg);
	obs_data_set_int(settings, "stats_color", theme->stats_color);
	obs_data_set_int(settings, "trail_color", theme->trail_color);
	obs_data_set_int(settings, "corner_radius", theme->corner_radius);
	obs_data_set_bool(settings, "bars_mode", theme->bars_mode);

	/* A list's own selected value is committed by the properties dialog
	 * automatically, but the *extra* fields above are only guaranteed to
	 * actually take effect on the running source if we push them through
	 * ourselves here - same as the buttons this replaced used to do via
	 * obs_source_update, rather than relying on the dialog to notice a
	 * modified-callback touched other settings behind its back. */
	if (ctx && ctx->source)
		obs_source_update(ctx->source, settings);

	/* Still return true so the dialog's own widgets (color pickers,
	 * corner-radius slider, bars-mode checkbox) refresh to show the new
	 * values instead of the stale ones. */
	return true;
}

/* (Re)fills a skin-name dropdown by scanning `folder` for packs. Always
 * keeps a "None" entry first so the user can clear the selection. */
static void jkps_populate_skin_list(obs_property_t *list, const char *folder)
{
	obs_property_list_clear(list);
	obs_property_list_add_string(list, obs_module_text("JkpsSource.SkinNone"), "");

	if (!folder || !folder[0])
		return;

	struct jkps_noteskin_entry entries[JKPS_NOTESKIN_MAX_ENTRIES];
	int n = jkps_noteskin_scan_folder(folder, entries, JKPS_NOTESKIN_MAX_ENTRIES);
	for (int i = 0; i < n; i++)
		obs_property_list_add_string(list, entries[i].display_name, entries[i].xml_path);
}

static bool jkps_funkin_folder_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	jkps_populate_skin_list(obs_properties_get(props, "funkin_skin_xml"),
				obs_data_get_string(settings, "funkin_folder"));
	return true;
}

/* Shared modified-callback for both "custom_skins_enabled" and
 * "skin_category": shows/hides the folder+skin pickers for whichever
 * category is active, and hides key slots 5-8 while an atlas skin is
 * selected (they're inert at that point - atlas skins are 4K-only). */
static bool jkps_skin_controls_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	bool enabled = obs_data_get_bool(settings, "custom_skins_enabled");
	long long category = obs_data_get_int(settings, "skin_category");

	obs_property_set_visible(obs_properties_get(props, "skin_category"), enabled);
	obs_property_set_visible(obs_properties_get(props, "noteskins_info"), enabled);

	bool show_funkin = enabled && category == JKPS_SKIN_CAT_FUNKIN;
	bool show_native = !enabled || category == JKPS_SKIN_CAT_NATIVE;
	obs_property_set_visible(obs_properties_get(props, "native_theme"), show_native);
	obs_property_set_visible(obs_properties_get(props, "funkin_folder"), show_funkin);
	obs_property_set_visible(obs_properties_get(props, "funkin_skin_xml"), show_funkin);

	bool locked_to_4k = enabled && category != JKPS_SKIN_CAT_NATIVE;
	for (int i = 4; i < JKPS_MAX_KEYS; i++) {
		char group_name[32];
		snprintf(group_name, sizeof(group_name), "key_group_%d", i);
		obs_property_set_visible(obs_properties_get(props, group_name), !locked_to_4k);
	}

	return true;
}

static obs_properties_t *jkps_source_get_properties(void *data)
{
	struct jkps_source_context *ctx = data;
	obs_properties_t *props = obs_properties_create();

	/* Noteskins: Native Skins (the procedural themes above) vs a real
	 * atlas-format Funkin' noteskin read from the user's own disk.
	 * Funkin' Skins ships empty - obs-jkps never bundles any fan or game
	 * art - and only shows packs once the user points a folder at their
	 * own. See NOTICE.md and jkps-noteskin.h. */
	obs_properties_t *skins_group = obs_properties_create();

	obs_property_t *enabled_prop = obs_properties_add_bool(skins_group, "custom_skins_enabled",
							       obs_module_text("JkpsSource.CustomSkinsEnabled"));
	obs_property_set_long_description(enabled_prop, obs_module_text("JkpsSource.CustomSkinsEnabledDesc"));
	obs_property_set_modified_callback(enabled_prop, jkps_skin_controls_modified);

	obs_properties_add_text(skins_group, "noteskins_info", obs_module_text("JkpsSource.NoteskinsInfoLabel"),
				OBS_TEXT_INFO);

	obs_property_t *cat_prop = obs_properties_add_list(skins_group, "skin_category",
							   obs_module_text("JkpsSource.SkinCategory"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(cat_prop, obs_module_text("JkpsSource.SkinCategoryNative"), JKPS_SKIN_CAT_NATIVE);
	obs_property_list_add_int(cat_prop, obs_module_text("JkpsSource.SkinCategoryFunkin"), JKPS_SKIN_CAT_FUNKIN);
	obs_property_set_modified_callback(cat_prop, jkps_skin_controls_modified);

	/* Native Skins: the procedural color-theme presets, as a single
	 * dropdown (instead of one button per theme) so it sits consistently
	 * alongside every other "pick one" control in the panel. */
	obs_property_t *theme_prop = obs_properties_add_list(skins_group, "native_theme",
							     obs_module_text("JkpsSource.Themes"), OBS_COMBO_TYPE_LIST,
							     OBS_COMBO_FORMAT_INT);
	for (size_t i = 0; i < JKPS_THEME_COUNT; i++)
		obs_property_list_add_int(theme_prop, obs_module_text(jkps_themes[i].locale_key), (long long)i);
	obs_property_set_modified_callback2(theme_prop, jkps_theme_list_modified, ctx);

	obs_property_t *funkin_folder_prop = obs_properties_add_path(skins_group, "funkin_folder",
								     obs_module_text("JkpsSource.FunkinFolder"),
								     OBS_PATH_DIRECTORY, NULL, NULL);
	obs_property_set_long_description(funkin_folder_prop, obs_module_text("JkpsSource.NoteskinsInfo"));
	obs_property_set_modified_callback(funkin_folder_prop, jkps_funkin_folder_modified);

	obs_property_t *funkin_skin_prop = obs_properties_add_list(skins_group, "funkin_skin_xml",
								   obs_module_text("JkpsSource.FunkinSkin"),
								   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	jkps_populate_skin_list(funkin_skin_prop, ctx ? ctx->funkin_folder : NULL);

	obs_properties_add_group(props, "skins_group", obs_module_text("JkpsSource.NoteskinsGroup"), OBS_GROUP_NORMAL,
				 skins_group);

	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		char group_name[32], enabled_name[32], vcode_name[32], label_name[32], group_title[64];
		char color_idle_name[32], color_pressed_name[32];
		char skin_idle_name[32], skin_pressed_name[32];
		snprintf(group_name, sizeof(group_name), "key_group_%d", i);
		snprintf(enabled_name, sizeof(enabled_name), "key_enabled_%d", i);
		snprintf(vcode_name, sizeof(vcode_name), "key_vcode_%d", i);
		snprintf(label_name, sizeof(label_name), "key_label_%d", i);
		snprintf(color_idle_name, sizeof(color_idle_name), "key_color_idle_%d", i);
		snprintf(color_pressed_name, sizeof(color_pressed_name), "key_color_pressed_%d", i);
		snprintf(skin_idle_name, sizeof(skin_idle_name), "key_skin_idle_%d", i);
		snprintf(skin_pressed_name, sizeof(skin_pressed_name), "key_skin_pressed_%d", i);
		snprintf(group_title, sizeof(group_title), "%s %d", obs_module_text("JkpsSource.Key"), i + 1);

		obs_properties_t *group = obs_properties_create();
		obs_properties_add_bool(group, enabled_name, obs_module_text("JkpsSource.KeyEnabled"));

		obs_property_t *list = obs_properties_add_list(group, vcode_name, obs_module_text("JkpsSource.KeyBind"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (size_t k = 0; k < jkps_keynames_count; k++)
			obs_property_list_add_int(list, jkps_keynames[k].label, jkps_keynames[k].vk);

		obs_properties_add_text(group, label_name, obs_module_text("JkpsSource.KeyLabel"), OBS_TEXT_DEFAULT);
		obs_properties_add_color(group, color_idle_name, obs_module_text("JkpsSource.ColorIdle"));
		obs_properties_add_color(group, color_pressed_name, obs_module_text("JkpsSource.ColorPressed"));
		obs_properties_add_path(group, skin_idle_name, obs_module_text("JkpsSource.SkinIdleImage"),
					OBS_PATH_FILE, "Images (*.png *.jpg *.jpeg *.bmp *.gif)", NULL);
		obs_properties_add_path(group, skin_pressed_name, obs_module_text("JkpsSource.SkinPressedImage"),
					OBS_PATH_FILE, "Images (*.png *.jpg *.jpeg *.bmp *.gif)", NULL);

		obs_properties_add_group(props, group_name, group_title, OBS_GROUP_NORMAL, group);
	}

	obs_properties_add_bool(props, "vertical_layout", obs_module_text("JkpsSource.VerticalLayout"));
	obs_properties_add_int(props, "key_size", obs_module_text("JkpsSource.KeySize"), 20, 300, 1);
	obs_properties_add_int(props, "key_spacing", obs_module_text("JkpsSource.KeySpacing"), 0, 100, 1);
	obs_properties_add_int(props, "key_font_size", obs_module_text("JkpsSource.KeyFontSize"), 8, 150, 1);
	obs_properties_add_int(props, "corner_radius", obs_module_text("JkpsSource.CornerRadius"), 0, 150, 1);
	obs_properties_add_bool(props, "show_press_trail", obs_module_text("JkpsSource.ShowPressTrail"));
	obs_properties_add_bool(props, "show_key_labels", obs_module_text("JkpsSource.ShowKeyLabels"));
	obs_properties_add_color_alpha(props, "trail_color", obs_module_text("JkpsSource.TrailColor"));
	obs_properties_add_int(props, "press_bar_max_height", obs_module_text("JkpsSource.PressBarMaxHeight"), 10, 2000,
			       1);
	obs_properties_add_int(props, "press_bar_rise_speed", obs_module_text("JkpsSource.PressBarRiseSpeed"), 1, 100,
			       1);
	obs_properties_add_bool(props, "bars_mode", obs_module_text("JkpsSource.BarsMode"));
	obs_properties_add_bool(props, "show_kps_graph", obs_module_text("JkpsSource.ShowKpsGraph"));
	obs_properties_add_color_alpha(props, "kps_graph_color", obs_module_text("JkpsSource.KpsGraphColor"));

	obs_properties_add_color(props, "color_text", obs_module_text("JkpsSource.ColorText"));
	obs_properties_add_color_alpha(props, "color_bg", obs_module_text("JkpsSource.ColorBg"));

	obs_properties_add_bool(props, "show_kps", obs_module_text("JkpsSource.ShowKps"));
	obs_properties_add_bool(props, "show_total", obs_module_text("JkpsSource.ShowTotal"));
	obs_properties_add_bool(props, "show_bpm", obs_module_text("JkpsSource.ShowBpm"));
	obs_properties_add_int(props, "stats_font_size", obs_module_text("JkpsSource.StatsFontSize"), 8, 150, 1);
	obs_properties_add_color(props, "stats_color", obs_module_text("JkpsSource.StatsColor"));

	obs_properties_add_button(props, "reset_stats_button", obs_module_text("JkpsSource.ResetButton"),
				  jkps_reset_button_clicked);

	UNUSED_PARAMETER(ctx);
	return props;
}

static void *jkps_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct jkps_source_context *ctx = bzalloc(sizeof(struct jkps_source_context));
	ctx->source = source;

	ctx->reset_hotkey_id = obs_hotkey_register_source(source, "JkpsSource.ResetHotkey",
							  obs_module_text("JkpsSource.ResetHotkeyDesc"),
							  jkps_source_reset_stats, ctx);

	jkps_source_update(ctx, settings);
	return ctx;
}

static void jkps_source_destroy(void *data)
{
	struct jkps_source_context *ctx = data;

	obs_hotkey_unregister(ctx->reset_hotkey_id);

	obs_enter_graphics();
	if (ctx->texture)
		gs_texture_destroy(ctx->texture);
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		if (ctx->key_skin_idle_loaded[i])
			gs_image_file_free(&ctx->key_skin_idle_img[i]);
		if (ctx->key_skin_pressed_loaded[i])
			gs_image_file_free(&ctx->key_skin_pressed_img[i]);
	}
	if (ctx->funkin_noteskin_loaded)
		jkps_noteskin_free(&ctx->funkin_noteskin);
	obs_leave_graphics();

	free(ctx->pixel_buffer);
	bfree(ctx);
}

static void jkps_source_video_tick(void *data, float seconds)
{
	struct jkps_source_context *ctx = data;
	if (!ctx->pixel_buffer || !ctx->texture)
		return;

	uint64_t now_ms = jkps_now_ms();
	jkps_poll_keys(ctx->keys, JKPS_MAX_KEYS, &ctx->stats, now_ms);
	jkps_stats_update(&ctx->stats, now_ms, seconds);

	if (now_ms - ctx->last_texture_update_ms < TEXTURE_UPDATE_INTERVAL_MS)
		return;
	ctx->last_texture_update_ms = now_ms;

	/* press_level: instant jump on press, exponential decay on release -
	 * still used as-is for the bars-mode VU-meter fill.
	 * press_bar_px: eases toward press_bar_max_height while held (fast
	 * rise). On release it does NOT shrink back into the key anymore -
	 * it drops to 0 immediately and hands off its current height to a
	 * separate floating remnant (float_bar_*) that keeps drifting away
	 * from the key and fades out as it nears/exits the margin, like a
	 * released hold-note tail continuing to scroll off instead of
	 * snapping back. */
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		bool down = ctx->keys[i].down;
		ctx->press_level[i] = down ? 1.0f : ctx->press_level[i] * 0.55f;

		float max_h = (float)ctx->press_bar_max_height;
		if (down) {
			float rise = (float)ctx->press_bar_rise_speed / 100.0f;
			ctx->press_bar_px[i] += (max_h - ctx->press_bar_px[i]) * rise;
			if (ctx->press_bar_px[i] > max_h)
				ctx->press_bar_px[i] = max_h;
		} else {
			if (ctx->press_bar_was_down[i] && ctx->press_bar_px[i] > 0.5f) {
				ctx->float_bar_len[i] = ctx->press_bar_px[i];
				ctx->float_bar_drift[i] = 0.0f;
				ctx->float_bar_alpha[i] = 1.0f;
			}
			ctx->press_bar_px[i] = 0.0f;
		}
		ctx->press_bar_was_down[i] = down;

		if (ctx->float_bar_len[i] > 0.0f) {
			/* Drift speed: crosses the full margin in ~1.1s at 30
			 * ticks/sec, independent of how tall the piece is. */
			ctx->float_bar_drift[i] += max_h * 0.03f;

			float top_edge = ctx->float_bar_drift[i] + ctx->float_bar_len[i];
			if (top_edge >= max_h) {
				/* Fades out over a distance equal to its own
				 * height once its leading edge exits the margin,
				 * so a tall bar (barely tapped) doesn't vanish
				 * instantly while a short one lingers oddly long. */
				float fade_span = ctx->float_bar_len[i] > 1.0f ? ctx->float_bar_len[i] : 1.0f;
				float past = top_edge - max_h;
				float alpha = 1.0f - past / fade_span;
				if (alpha <= 0.0f) {
					ctx->float_bar_len[i] = 0.0f;
					ctx->float_bar_alpha[i] = 0.0f;
				} else {
					ctx->float_bar_alpha[i] = alpha;
				}
			}
		}
	}

	/* Scroll the KPS graph history: drop the oldest sample, append the
	 * current KPS at the end. */
	memmove(&ctx->kps_history[0], &ctx->kps_history[1], sizeof(float) * (JKPS_KPS_GRAPH_SAMPLES - 1));
	ctx->kps_history[JKPS_KPS_GRAPH_SAMPLES - 1] = ctx->stats.kps;

	struct jkps_render_params p;
	memset(&p, 0, sizeof(p));

	struct jkps_noteskin *active_skin = jkps_active_noteskin(ctx);

	int active = 0;
	int slot_to_key[JKPS_MAX_KEYS];
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		if (!ctx->key_enabled[i])
			continue;
		p.keys[active].label = ctx->key_label[i];
		p.keys[active].down = ctx->keys[i].down;
		p.keys[active].total = ctx->keys[i].total_presses;
		p.keys[active].color_idle = ctx->key_color_idle[i];
		p.keys[active].color_pressed = ctx->key_color_pressed[i];
		p.keys[active].has_custom_skin = ctx->key_skin_idle_loaded[i] || ctx->key_skin_pressed_loaded[i] ||
						 (active_skin != NULL && i < 4);
		p.keys[active].press_level = ctx->press_level[i];
		p.keys[active].press_bar_px = ctx->press_bar_px[i];
		p.keys[active].float_bar_len = ctx->float_bar_len[i];
		p.keys[active].float_bar_drift = ctx->float_bar_drift[i];
		p.keys[active].float_bar_alpha = ctx->float_bar_alpha[i];
		p.keys[active].use_custom_hold_bar = active_skin != NULL && i < 4 &&
						     (active_skin->hold_piece[jkps_slot_to_dir[i]].valid ||
						      active_skin->hold_end[jkps_slot_to_dir[i]].valid);
		slot_to_key[active] = i;
		active++;
	}
	p.num_keys = active > 0 ? active : 1;
	if (active == 0)
		p.keys[0].label = "";

	p.vertical_layout = ctx->vertical_layout;
	p.key_size = ctx->key_size;
	p.key_spacing = ctx->key_spacing;
	p.key_font_size = ctx->key_font_size;
	p.corner_radius = ctx->corner_radius;
	p.show_press_trail = ctx->show_press_trail;
	p.press_bar_max_height = ctx->press_bar_max_height;
	p.show_key_labels = ctx->show_key_labels;
	p.trail_color = ctx->trail_color;
	p.bars_mode = ctx->bars_mode;
	p.show_kps_graph = ctx->show_kps_graph;
	p.kps_graph_color = ctx->kps_graph_color;
	memcpy(p.kps_history, ctx->kps_history, sizeof(p.kps_history));
	p.color_text = ctx->color_text;
	p.color_bg = ctx->color_bg;
	p.show_kps = ctx->show_kps;
	p.show_total = ctx->show_total;
	p.show_bpm = ctx->show_bpm;
	p.stats_font_size = ctx->stats_font_size;
	p.stats_color = ctx->stats_color;
	p.kps = ctx->stats.kps;
	p.total_presses = ctx->stats.total_presses;
	p.bpm = ctx->stats.bpm;

	int slot_x[JKPS_MAX_KEYS], slot_y[JKPS_MAX_KEYS];
	jkps_render_get_key_positions(&p, slot_x, slot_y);
	for (int s = 0; s < active; s++) {
		int orig = slot_to_key[s];
		ctx->key_screen_x[orig] = slot_x[s];
		ctx->key_screen_y[orig] = slot_y[s];
	}

	if (jkps_render_frame(&p, ctx->width, ctx->height, ctx->pixel_buffer)) {
		obs_enter_graphics();
		gs_texture_set_image(ctx->texture, ctx->pixel_buffer, ctx->width * 4, false);
		obs_leave_graphics();
	}
}

/* obs_source_draw() can only draw a whole texture scaled into a box - it has
 * no source-rectangle support - so atlas noteskin frames (a crop out of one
 * big sheet) need this instead. Draws the `src` pixel rect of `tex` so the
 * frame's full *logical* box (frame_w x frame_h - the untrimmed size Sparrow
 * records for a trimmed frame, equal to w/h when untrimmed) fills a dst_w x
 * dst_h box at (dst_x, dst_y); the actual stored pixels are positioned/sized
 * inside that box per frame_x/frame_y, and rotated back upright first if the
 * pack was exported with rotation (Free Texture Packer's "Starling"/Sparrow
 * preset - what most Psych Engine noteskins use - can pack frames rotated
 * 90 deg to save atlas space). Without this, rotated or trimmed frames end
 * up sideways, squashed, or misaligned instead of just not appearing, which
 * is what "doesn't render right" for a real-world pack usually turns out to
 * be. */
static void jkps_draw_atlas_subregion(gs_texture_t *tex, int dst_x, int dst_y, int dst_w, int dst_h,
				      const struct jkps_atlas_rect *src)
{
	if (!tex || !src->valid || dst_w <= 0 || dst_h <= 0 || src->w <= 0 || src->h <= 0)
		return;

	int frame_w = src->frame_w > 0 ? src->frame_w : src->w;
	int frame_h = src->frame_h > 0 ? src->frame_h : src->h;

	/* Size/position of the actual stored (trimmed) pixels, in *display*
	 * (post-rotation) terms, as a sub-rect of the logical frame box. */
	int vis_w = src->rotated ? src->h : src->w;
	int vis_h = src->rotated ? src->w : src->h;

	float scale_x = (float)dst_w / (float)frame_w;
	float scale_y = (float)dst_h / (float)frame_h;

	float vis_dst_x = (float)dst_x + (float)src->frame_x * scale_x;
	float vis_dst_y = (float)dst_y + (float)src->frame_y * scale_y;
	float vis_dst_w = (float)vis_w * scale_x;
	float vis_dst_h = (float)vis_h * scale_y;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, tex);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	/* No gs_effect_loop here on purpose: OBS already has this exact
	 * effect's "Draw" technique/pass active for the whole duration of a
	 * (non-OBS_SOURCE_CUSTOM_DRAW) source's video_render callback - the
	 * same reason a plain obs_source_draw() call works with no loop of
	 * its own. Wrapping this in a second gs_effect_loop nests a technique
	 * pass inside an already-active one on the same effect object, which
	 * doesn't error but simply never iterates - so the draw call below
	 * silently never runs. That was the actual cause of the noteskin
	 * being fully invisible: has_custom_skin correctly hid the flat
	 * fallback box, and then nothing replaced it. */
	gs_matrix_push();
	/* Pivot around the visible rect's center so a 90 deg turn (for
	 * rotated frames) lands the content back in the same place
	 * instead of spinning it off to a corner. */
	gs_matrix_translate3f(vis_dst_x + vis_dst_w * 0.5f, vis_dst_y + vis_dst_h * 0.5f, 0.0f);
	if (src->rotated) {
		gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, -1.57079632679f /* -90 deg */);
		gs_matrix_scale3f(vis_dst_h / (float)src->w, vis_dst_w / (float)src->h, 1.0f);
	} else {
		gs_matrix_scale3f(vis_dst_w / (float)src->w, vis_dst_h / (float)src->h, 1.0f);
	}
	gs_matrix_translate3f(-(float)src->w * 0.5f, -(float)src->h * 0.5f, 0.0f);
	gs_draw_sprite_subregion(tex, 0, (uint32_t)src->x, (uint32_t)src->y, (uint32_t)src->w, (uint32_t)src->h);
	gs_matrix_pop();

	gs_blend_state_pop();
}

/* Same as jkps_draw_atlas_subregion, plus an independent extra 90 deg turn
 * around the destination box's own center - on top of whatever the frame's
 * own `rotated` flag already applies. Used for a vertical key layout, where
 * the press bar grows sideways but the pack's hold art is always drawn for
 * FNF's native vertical note-scroll orientation, so it needs one more turn
 * to lie on its side correctly. */
static void jkps_draw_atlas_tile(gs_texture_t *tex, int dst_x, int dst_y, int dst_w, int dst_h, bool extra_rotate_90,
				 const struct jkps_atlas_rect *src)
{
	if (!extra_rotate_90) {
		jkps_draw_atlas_subregion(tex, dst_x, dst_y, dst_w, dst_h, src);
		return;
	}

	gs_matrix_push();
	gs_matrix_translate3f((float)dst_x + (float)dst_w * 0.5f, (float)dst_y + (float)dst_h * 0.5f, 0.0f);
	gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, -1.57079632679f /* -90 deg */);
	jkps_draw_atlas_subregion(tex, -(dst_h / 2), -(dst_w / 2), dst_h, dst_w, src);
	gs_matrix_pop();
}

/* Draws a Funkin' Skin's own hold/sustain-note art - a tiled "hold piece"
 * capped with a "hold end" - stretched to fill (bar_x, bar_y, bar_w, bar_h)
 * instead of the flat-color press bar, matching how FNF itself renders long
 * notes. grows_up: true for a horizontal key row (bar grows upward, flush
 * against the key at the bottom - the pack's native orientation, no extra
 * turn needed); false for a vertical key column (bar grows rightward, flush
 * against the key on the left - needs the extra 90 deg turn above). Either
 * piece or end (or both) may be invalid; whichever is missing is simply
 * skipped, e.g. tiling the piece across the full length with no end cap. */
static void jkps_draw_hold_bar(gs_texture_t *tex, int bar_x, int bar_y, int bar_w, int bar_h, bool grows_up,
			       const struct jkps_atlas_rect *piece, const struct jkps_atlas_rect *end)
{
	if (!tex)
		return;

	int thickness = grows_up ? bar_w : bar_h; /* fixed axis, matches key_size */
	int extent = grows_up ? bar_h : bar_w;    /* the axis the bar grows along */
	if (thickness <= 0 || extent <= 0)
		return;

	int end_len = 0;
	if (end->valid && end->frame_w > 0) {
		float aspect = (float)end->frame_h / (float)end->frame_w;
		end_len = (int)((float)thickness * aspect + 0.5f);
		if (end_len > extent)
			end_len = extent;
	}

	int tile_len = thickness;
	if (piece->valid && piece->frame_w > 0) {
		tile_len = (int)((float)thickness * (float)piece->frame_h / (float)piece->frame_w + 0.5f);
		if (tile_len < 2)
			tile_len = 2;
	}

	int piece_extent = extent - end_len;

	/* drawn=0 is the tile right next to the end cap (farthest from the
	 * key); a partial leftover tile lands next to the key instead of
	 * leaving a gap or seam right under the end cap. */
	if (piece->valid) {
		int drawn = 0;
		while (drawn < piece_extent) {
			int this_len = tile_len;
			if (drawn + this_len > piece_extent)
				this_len = piece_extent - drawn;

			int tx, ty, tw, th;
			if (grows_up) {
				tw = bar_w;
				th = this_len;
				tx = bar_x;
				ty = bar_y + end_len + drawn;
			} else {
				tw = this_len;
				th = bar_h;
				tx = bar_x + drawn;
				ty = bar_y;
			}
			jkps_draw_atlas_tile(tex, tx, ty, tw, th, !grows_up, piece);
			drawn += this_len;
		}
	}

	if (end->valid && end_len > 0) {
		int tx, ty, tw, th;
		if (grows_up) {
			tw = bar_w;
			th = end_len;
			tx = bar_x;
			ty = bar_y;
		} else {
			tw = end_len;
			th = bar_h;
			tx = bar_x + bar_w - end_len;
			ty = bar_y;
		}
		jkps_draw_atlas_tile(tex, tx, ty, tw, th, !grows_up, end);
	}
}

static void jkps_source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct jkps_source_context *ctx = data;
	if (!ctx->texture)
		return;

	obs_source_draw(ctx->texture, 0, 0, 0, 0, false);

	struct jkps_noteskin *active_skin = jkps_active_noteskin(ctx);

	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		if (!ctx->key_enabled[i])
			continue;

		bool down = ctx->keys[i].down;

		/* Mirrors jkps-render.c's flat press-bar geometry exactly, but
		 * only for keys whose lane the active Funkin' Skin actually
		 * ships hold art for - see use_custom_hold_bar in that file. */
		if (ctx->show_press_trail && active_skin && i < 4) {
			enum jkps_noteskin_dir dir = jkps_slot_to_dir[i];
			const struct jkps_atlas_rect *piece = &active_skin->hold_piece[dir];
			const struct jkps_atlas_rect *end = &active_skin->hold_end[dir];
			if (piece->valid || end->valid) {
				int bar_len = (int)(ctx->press_bar_px[i] + 0.5f);
				if (bar_len > ctx->press_bar_max_height)
					bar_len = ctx->press_bar_max_height;
				if (bar_len > 0) {
					int bar_x = ctx->key_screen_x[i], bar_y = ctx->key_screen_y[i];
					if (ctx->vertical_layout)
						bar_x += ctx->key_size;
					else
						bar_y -= bar_len;

					int bar_w = ctx->vertical_layout ? bar_len : ctx->key_size;
					int bar_h = ctx->vertical_layout ? ctx->key_size : bar_len;

					jkps_draw_hold_bar(active_skin->atlas_img.texture, bar_x, bar_y, bar_w, bar_h,
							   !ctx->vertical_layout, piece, end);
				}
			}
		}

		/* Atlas noteskin takes priority over the flat per-key images
		 * below, and only ever applies to slots 0-3 (the ones a
		 * locked-to-4K layout actually has). */
		if (active_skin && i < 4) {
			enum jkps_noteskin_state state = down ? JKPS_SKIN_PRESSED : JKPS_SKIN_STATIC;
			const struct jkps_atlas_rect *frame = &active_skin->frames[jkps_slot_to_dir[i]][state];
			if (frame->valid) {
				jkps_draw_atlas_subregion(active_skin->atlas_img.texture, ctx->key_screen_x[i],
							  ctx->key_screen_y[i], ctx->key_size, ctx->key_size, frame);
				continue;
			}
		}

		gs_image_file_t *img = NULL;
		if (down && ctx->key_skin_pressed_loaded[i])
			img = &ctx->key_skin_pressed_img[i];
		else if (ctx->key_skin_idle_loaded[i])
			img = &ctx->key_skin_idle_img[i];
		else if (ctx->key_skin_pressed_loaded[i])
			img = &ctx->key_skin_pressed_img[i]; /* only a pressed image was given */

		if (img && img->texture)
			obs_source_draw(img->texture, ctx->key_screen_x[i], ctx->key_screen_y[i],
					(uint32_t)ctx->key_size, (uint32_t)ctx->key_size, false);
	}
}

static uint32_t jkps_source_get_width(void *data)
{
	struct jkps_source_context *ctx = data;
	return ctx->width;
}

static uint32_t jkps_source_get_height(void *data)
{
	struct jkps_source_context *ctx = data;
	return ctx->height;
}

struct obs_source_info jkps_source_info = {
	.id = "jkps_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = jkps_source_get_name,
	.create = jkps_source_create,
	.destroy = jkps_source_destroy,
	.update = jkps_source_update,
	.get_defaults = jkps_source_get_defaults,
	.get_properties = jkps_source_get_properties,
	.video_tick = jkps_source_video_tick,
	.video_render = jkps_source_video_render,
	.get_width = jkps_source_get_width,
	.get_height = jkps_source_get_height,
	.icon_type = OBS_ICON_TYPE_GAME_CAPTURE,
};
