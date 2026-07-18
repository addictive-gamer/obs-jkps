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

#include <graphics/graphics.h>
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
	float trail[JKPS_MAX_KEYS][JKPS_TRAIL_SEGMENTS];

	uint32_t color_idle;
	uint32_t color_pressed;
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
	}

	ctx->vertical_layout = obs_data_get_bool(settings, "vertical_layout");
	ctx->key_size = (int)obs_data_get_int(settings, "key_size");
	ctx->key_spacing = (int)obs_data_get_int(settings, "key_spacing");
	ctx->key_font_size = (int)obs_data_get_int(settings, "key_font_size");
	ctx->corner_radius = (int)obs_data_get_int(settings, "corner_radius");
	ctx->show_press_trail = obs_data_get_bool(settings, "show_press_trail");

	ctx->color_idle = (uint32_t)obs_data_get_int(settings, "color_idle");
	ctx->color_pressed = (uint32_t)obs_data_get_int(settings, "color_pressed");
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
	}

	obs_data_set_default_bool(settings, "vertical_layout", false);
	obs_data_set_default_int(settings, "key_size", 60);
	obs_data_set_default_int(settings, "key_spacing", 8);
	obs_data_set_default_int(settings, "key_font_size", 22);
	obs_data_set_default_int(settings, "corner_radius", 0);
	obs_data_set_default_bool(settings, "show_press_trail", false);

	obs_data_set_default_int(settings, "color_idle", 0xFF3A3A3A);
	obs_data_set_default_int(settings, "color_pressed", 0xFF37B2FF);
	obs_data_set_default_int(settings, "color_text", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "color_bg", 0x00000000);

	obs_data_set_default_bool(settings, "show_kps", true);
	obs_data_set_default_bool(settings, "show_total", true);
	obs_data_set_default_bool(settings, "show_bpm", false);
	obs_data_set_default_int(settings, "stats_font_size", 20);
	obs_data_set_default_int(settings, "stats_color", 0xFFFFFFFF);
}

/* Quick-apply theme presets: each bundles key/text/background colors with a
 * corner style, so one click gives a coherent look instead of the user
 * having to tweak 5 separate color pickers + a radius slider by hand. */
struct jkps_theme {
	const char *id;         /* used as the button's property name */
	const char *locale_key; /* obs_module_text() key for the button label */
	uint32_t color_idle;
	uint32_t color_pressed;
	uint32_t color_text;
	uint32_t color_bg;
	uint32_t stats_color;
	int corner_radius;
};

static const struct jkps_theme jkps_themes[] = {
	{
		.id = "theme_classic",
		.locale_key = "JkpsSource.ThemeClassic",
		.color_idle = 0xFF3A3A3A,
		.color_pressed = 0xFF37B2FF,
		.color_text = 0xFFFFFFFF,
		.color_bg = 0x00000000,
		.stats_color = 0xFFFFFFFF,
		.corner_radius = 6,
	},
	{
		.id = "theme_neon",
		.locale_key = "JkpsSource.ThemeNeon",
		.color_idle = 0xFF2E1A3D,
		.color_pressed = 0xFFFF2BD6,
		.color_text = 0xFF00F5FF,
		.color_bg = 0x00000000,
		.stats_color = 0xFF00F5FF,
		.corner_radius = 30, /* pill-shaped at the default 60px key size */
	},
	{
		.id = "theme_retro",
		.locale_key = "JkpsSource.ThemeRetroArcade",
		.color_idle = 0xFF7C2D2D,
		.color_pressed = 0xFF00D400,
		.color_text = 0xFF000000,
		.color_bg = 0xFF0A0A0A,
		.stats_color = 0xFF00FF00,
		.corner_radius = 0, /* crisp, blocky pixel-art corners */
	},
	{
		.id = "theme_minimal",
		.locale_key = "JkpsSource.ThemeMinimal",
		.color_idle = 0xFFE5E5E5,
		.color_pressed = 0xFF3A3A3A,
		.color_text = 0xFF222222,
		.color_bg = 0x00000000,
		.stats_color = 0xFF222222,
		.corner_radius = 2,
	},
	{
		.id = "theme_pastel",
		.locale_key = "JkpsSource.ThemePastel",
		.color_idle = 0xFFF7D6E8,
		.color_pressed = 0xFFC9F7DE,
		.color_text = 0xFF6B4E6E,
		.color_bg = 0x00000000,
		.stats_color = 0xFF6B4E6E,
		.corner_radius = 16,
	},
};
#define JKPS_THEME_COUNT (sizeof(jkps_themes) / sizeof(jkps_themes[0]))

static bool jkps_apply_theme(struct jkps_source_context *ctx, const struct jkps_theme *theme)
{
	obs_data_t *settings = obs_source_get_settings(ctx->source);

	obs_data_set_int(settings, "color_idle", theme->color_idle);
	obs_data_set_int(settings, "color_pressed", theme->color_pressed);
	obs_data_set_int(settings, "color_text", theme->color_text);
	obs_data_set_int(settings, "color_bg", theme->color_bg);
	obs_data_set_int(settings, "stats_color", theme->stats_color);
	obs_data_set_int(settings, "corner_radius", theme->corner_radius);

	obs_source_update(ctx->source, settings);
	obs_data_release(settings);

	/* Return true so the properties dialog re-reads and displays the new
	 * values (color pickers, slider) instead of showing stale ones. */
	return true;
}

static bool jkps_theme_button_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	struct jkps_source_context *ctx = data;
	const char *name = obs_property_name(property);

	for (size_t i = 0; i < JKPS_THEME_COUNT; i++) {
		if (strcmp(name, jkps_themes[i].id) == 0)
			return jkps_apply_theme(ctx, &jkps_themes[i]);
	}
	return false;
}

static obs_properties_t *jkps_source_get_properties(void *data)
{
	struct jkps_source_context *ctx = data;
	obs_properties_t *props = obs_properties_create();

	obs_properties_t *themes_group = obs_properties_create();
	for (size_t i = 0; i < JKPS_THEME_COUNT; i++) {
		obs_properties_add_button(themes_group, jkps_themes[i].id, obs_module_text(jkps_themes[i].locale_key),
					  jkps_theme_button_clicked);
	}
	obs_properties_add_group(props, "themes_group", obs_module_text("JkpsSource.Themes"), OBS_GROUP_NORMAL,
				 themes_group);

	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		char group_name[32], enabled_name[32], vcode_name[32], label_name[32], group_title[64];
		snprintf(group_name, sizeof(group_name), "key_group_%d", i);
		snprintf(enabled_name, sizeof(enabled_name), "key_enabled_%d", i);
		snprintf(vcode_name, sizeof(vcode_name), "key_vcode_%d", i);
		snprintf(label_name, sizeof(label_name), "key_label_%d", i);
		snprintf(group_title, sizeof(group_title), "%s %d", obs_module_text("JkpsSource.Key"), i + 1);

		obs_properties_t *group = obs_properties_create();
		obs_properties_add_bool(group, enabled_name, obs_module_text("JkpsSource.KeyEnabled"));

		obs_property_t *list = obs_properties_add_list(group, vcode_name, obs_module_text("JkpsSource.KeyBind"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (size_t k = 0; k < jkps_keynames_count; k++)
			obs_property_list_add_int(list, jkps_keynames[k].label, jkps_keynames[k].vk);

		obs_properties_add_text(group, label_name, obs_module_text("JkpsSource.KeyLabel"), OBS_TEXT_DEFAULT);

		obs_properties_add_group(props, group_name, group_title, OBS_GROUP_NORMAL, group);
	}

	obs_properties_add_bool(props, "vertical_layout", obs_module_text("JkpsSource.VerticalLayout"));
	obs_properties_add_int(props, "key_size", obs_module_text("JkpsSource.KeySize"), 20, 300, 1);
	obs_properties_add_int(props, "key_spacing", obs_module_text("JkpsSource.KeySpacing"), 0, 100, 1);
	obs_properties_add_int(props, "key_font_size", obs_module_text("JkpsSource.KeyFontSize"), 8, 150, 1);
	obs_properties_add_int(props, "corner_radius", obs_module_text("JkpsSource.CornerRadius"), 0, 150, 1);
	obs_properties_add_bool(props, "show_press_trail", obs_module_text("JkpsSource.ShowPressTrail"));

	obs_properties_add_color(props, "color_idle", obs_module_text("JkpsSource.ColorIdle"));
	obs_properties_add_color(props, "color_pressed", obs_module_text("JkpsSource.ColorPressed"));
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

	/* Shift each key's trail buffer outward one slot (like a conveyor
	 * belt) and inject the current down state at the front. Segment 0
	 * fades out gradually when the key is up, so a quick tap still
	 * leaves a brief, natural-looking blip instead of vanishing on the
	 * very next frame. */
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		for (int s = JKPS_TRAIL_SEGMENTS - 1; s > 0; s--)
			ctx->trail[i][s] = ctx->trail[i][s - 1];
		ctx->trail[i][0] = ctx->keys[i].down ? 1.0f : ctx->trail[i][0] * 0.55f;
	}

	struct jkps_render_params p;
	memset(&p, 0, sizeof(p));

	int active = 0;
	for (int i = 0; i < JKPS_MAX_KEYS; i++) {
		if (!ctx->key_enabled[i])
			continue;
		p.keys[active].label = ctx->key_label[i];
		p.keys[active].down = ctx->keys[i].down;
		p.keys[active].total = ctx->keys[i].total_presses;
		memcpy(p.keys[active].trail, ctx->trail[i], sizeof(p.keys[active].trail));
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
	p.color_idle = ctx->color_idle;
	p.color_pressed = ctx->color_pressed;
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

	if (jkps_render_frame(&p, ctx->width, ctx->height, ctx->pixel_buffer)) {
		obs_enter_graphics();
		gs_texture_set_image(ctx->texture, ctx->pixel_buffer, ctx->width * 4, false);
		obs_leave_graphics();
	}
}

static void jkps_source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct jkps_source_context *ctx = data;
	if (!ctx->texture)
		return;

	obs_source_draw(ctx->texture, 0, 0, 0, 0, false);
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
