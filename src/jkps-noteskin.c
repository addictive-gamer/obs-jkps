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

#include "jkps-noteskin.h"

#include <util/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- tiny helpers -------------------------------------------------- */

static bool file_exists(const char *path)
{
	FILE *f = os_fopen(path, "rb");
	if (!f)
		return false;
	fclose(f);
	return true;
}

static bool ends_with_ci(const char *s, const char *suffix)
{
	size_t ls = strlen(s), lf = strlen(suffix);
	if (lf > ls)
		return false;
	for (size_t i = 0; i < lf; i++) {
		char a = (char)tolower((unsigned char)s[ls - lf + i]);
		char b = (char)tolower((unsigned char)suffix[i]);
		if (a != b)
			return false;
	}
	return true;
}

/* Swaps a path's ".xml" suffix for ".png", in place. Caller guarantees the
 * buffer already ends in ".xml" (checked via ends_with_ci beforehand). */
static void xml_path_to_png(char *path)
{
	size_t len = strlen(path);
	if (len >= 4)
		memcpy(path + len - 4, ".png", 4);
}

static char *jkps_read_text_file(const char *path)
{
	FILE *f = os_fopen(path, "rb");
	if (!f)
		return NULL;

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	char *buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	size_t read = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[read] = '\0';
	return buf;
}

static bool find_attr_str(const char *tag, const char *attr, char *out, size_t out_size)
{
	char needle[32];
	snprintf(needle, sizeof(needle), "%s=\"", attr);

	const char *p = strstr(tag, needle);
	if (!p)
		return false;
	p += strlen(needle);

	const char *end = strchr(p, '"');
	if (!end)
		return false;

	size_t len = (size_t)(end - p);
	if (len >= out_size)
		len = out_size - 1;
	memcpy(out, p, len);
	out[len] = '\0';
	return true;
}

static bool find_attr_int(const char *tag, const char *attr, int *out)
{
	char buf[32];
	if (!find_attr_str(tag, attr, buf, sizeof(buf)))
		return false;
	*out = atoi(buf);
	return true;
}

/* Classifies a <SubTexture name="..."> into one of the 4 arrow lanes and
 * one of the 3 states, the same way Psych Engine (and every noteskin pack
 * built for it) names frames: "left"/"down"/"up"/"right" for the lane, plus
 * "press"/"confirm" for the state (no suffix = static/idle). Frames that
 * don't match a lane (rating sprites, combo numbers, etc.) are ignored. */
static bool classify_frame(const char *raw_name, enum jkps_noteskin_dir *dir, enum jkps_noteskin_state *state)
{
	char lower[JKPS_NOTESKIN_NAME_LEN];
	size_t n = strlen(raw_name);
	if (n >= sizeof(lower))
		n = sizeof(lower) - 1;
	for (size_t i = 0; i < n; i++)
		lower[i] = (char)tolower((unsigned char)raw_name[i]);
	lower[n] = '\0';

	if (strstr(lower, "left"))
		*dir = JKPS_DIR_LEFT;
	else if (strstr(lower, "down"))
		*dir = JKPS_DIR_DOWN;
	else if (strstr(lower, "up"))
		*dir = JKPS_DIR_UP;
	else if (strstr(lower, "right"))
		*dir = JKPS_DIR_RIGHT;
	else
		return false;

	if (strstr(lower, "confirm"))
		*state = JKPS_SKIN_CONFIRM;
	else if (strstr(lower, "press"))
		*state = JKPS_SKIN_PRESSED;
	else
		*state = JKPS_SKIN_STATIC;

	return true;
}

/* ---- atlas loading --------------------------------------------------- */

bool jkps_noteskin_load(const char *xml_path, struct jkps_noteskin *out)
{
	memset(out, 0, sizeof(*out));

	if (!xml_path || !xml_path[0] || !ends_with_ci(xml_path, ".xml"))
		return false;

	char *xml = jkps_read_text_file(xml_path);
	if (!xml)
		return false;

	int found = 0;
	const char *p = xml;
	while ((p = strstr(p, "<SubTexture")) != NULL) {
		const char *tag_end = strchr(p, '>');
		if (!tag_end)
			break;

		size_t tag_len = (size_t)(tag_end - p);
		char tag[512];
		if (tag_len >= sizeof(tag))
			tag_len = sizeof(tag) - 1;
		memcpy(tag, p, tag_len);
		tag[tag_len] = '\0';

		char name[JKPS_NOTESKIN_NAME_LEN];
		int x = 0, y = 0, w = 0, h = 0;
		if (find_attr_str(tag, "name", name, sizeof(name)) && find_attr_int(tag, "x", &x) &&
		    find_attr_int(tag, "y", &y) && find_attr_int(tag, "width", &w) &&
		    find_attr_int(tag, "height", &h) && w > 0 && h > 0) {
			enum jkps_noteskin_dir dir;
			enum jkps_noteskin_state state;
			if (classify_frame(name, &dir, &state)) {
				struct jkps_atlas_rect *r = &out->frames[dir][state];
				/* First match wins: packs list animation frames in
				 * ascending order (0000, 0001, ...), and a static
				 * overlay only ever needs the first one. */
				if (!r->valid) {
					r->x = x;
					r->y = y;
					r->w = w;
					r->h = h;
					r->valid = true;
					found++;
				}
			}
		}

		p = tag_end + 1;
	}

	free(xml);

	if (found == 0)
		return false;

	/* Let missing states fall back gracefully: confirm -> press -> static,
	 * so a pack only needs a static frame per lane to work at all. */
	for (int d = 0; d < JKPS_DIR_COUNT; d++) {
		if (!out->frames[d][JKPS_SKIN_PRESSED].valid)
			out->frames[d][JKPS_SKIN_PRESSED] = out->frames[d][JKPS_SKIN_STATIC];
		if (!out->frames[d][JKPS_SKIN_CONFIRM].valid)
			out->frames[d][JKPS_SKIN_CONFIRM] = out->frames[d][JKPS_SKIN_PRESSED];
	}

	char png_path[JKPS_NOTESKIN_PATH_LEN];
	strncpy(png_path, xml_path, sizeof(png_path) - 1);
	png_path[sizeof(png_path) - 1] = '\0';
	xml_path_to_png(png_path);

	if (!file_exists(png_path))
		return false;

	gs_image_file_init(&out->atlas_img, png_path);
	gs_image_file_init_texture(&out->atlas_img);
	if (!out->atlas_img.loaded) {
		gs_image_file_free(&out->atlas_img);
		return false;
	}
	out->atlas_loaded = true;

	strncpy(out->source_xml_path, xml_path, sizeof(out->source_xml_path) - 1);
	out->source_xml_path[sizeof(out->source_xml_path) - 1] = '\0';

	return true;
}

void jkps_noteskin_free(struct jkps_noteskin *ns)
{
	if (ns->atlas_loaded) {
		gs_image_file_free(&ns->atlas_img);
		ns->atlas_loaded = false;
	}
}

/* ---- folder scanning --------------------------------------------------- */

static void basename_into(const char *path, char *out, size_t out_size)
{
	const char *slash = strrchr(path, '/');
	const char *bslash = strrchr(path, '\\');
	const char *base = path;
	if (slash && slash + 1 > base)
		base = slash + 1;
	if (bslash && bslash + 1 > base)
		base = bslash + 1;
	strncpy(out, base, out_size - 1);
	out[out_size - 1] = '\0';
}

/* Same as basename_into, but also strips a trailing ".xml" (case
 * insensitive), so a pack's display name reads "Bf Skin" instead of
 * "Bf Skin.xml". */
static void basename_noext_into(const char *path, char *out, size_t out_size)
{
	basename_into(path, out, out_size);
	size_t len = strlen(out);
	if (ends_with_ci(out, ".xml"))
		out[len - 4] = '\0';
}

/* How many folder levels deep to look for packs below the root the user
 * points the plugin at. Covers root/Pack/xml+png (1), root/Collection/Pack/
 * xml+png (2), and one more spare level for anything packaged a bit deeper
 * without letting a pathological folder tree recurse forever. */
#define JKPS_NOTESKIN_MAX_SCAN_DEPTH 4

/* Walks dir_path looking for .xml+.png pairs, at any depth up to
 * JKPS_NOTESKIN_MAX_SCAN_DEPTH below the original root. label_hint is the
 * name of the folder that led here (NULL at the root itself) and is used to
 * build a readable display name:
 *   - a folder holding exactly one pack keeps a clean name (just the
 *     folder's own name, e.g. "Dash Note Noteskin");
 *   - a folder that flattens several packs together (e.g. a "noteSkins"
 *     folder full of loose xml/png pairs) disambiguates each one with its
 *     xml filename appended (e.g. "noteSkins - 17bucks"). */
static void jkps_noteskin_collect(const char *dir_path, const char *label_hint,
				   struct jkps_noteskin_entry *out_entries, int *count, int max_entries, int depth)
{
	if (*count >= max_entries || depth > JKPS_NOTESKIN_MAX_SCAN_DEPTH)
		return;

	/* Pass 1: how many valid pairs live directly in this folder? Decides
	 * whether entries below need the xml filename appended to stay
	 * distinguishable from each other. */
	int local_pairs = 0;
	os_dir_t *dir = os_opendir(dir_path);
	if (!dir)
		return;
	struct os_dirent *ent;
	while ((ent = os_readdir(dir)) != NULL) {
		if (ent->directory || !ends_with_ci(ent->d_name, ".xml"))
			continue;

		char xml_path[JKPS_NOTESKIN_PATH_LEN];
		int len = snprintf(xml_path, sizeof(xml_path), "%s/%s", dir_path, ent->d_name);
		if (len < 0 || (size_t)len >= sizeof(xml_path))
			continue;

		char png_path[JKPS_NOTESKIN_PATH_LEN];
		strncpy(png_path, xml_path, sizeof(png_path) - 1);
		png_path[sizeof(png_path) - 1] = '\0';
		xml_path_to_png(png_path);

		if (file_exists(png_path))
			local_pairs++;
	}
	os_closedir(dir);

	/* Pass 2: emit an entry per valid pair found directly here. */
	dir = os_opendir(dir_path);
	if (!dir)
		return;
	while (*count < max_entries && (ent = os_readdir(dir)) != NULL) {
		if (ent->directory || !ends_with_ci(ent->d_name, ".xml"))
			continue;

		char xml_path[JKPS_NOTESKIN_PATH_LEN];
		int len = snprintf(xml_path, sizeof(xml_path), "%s/%s", dir_path, ent->d_name);
		if (len < 0 || (size_t)len >= sizeof(xml_path))
			continue;

		char png_path[JKPS_NOTESKIN_PATH_LEN];
		strncpy(png_path, xml_path, sizeof(png_path) - 1);
		png_path[sizeof(png_path) - 1] = '\0';
		xml_path_to_png(png_path);

		if (!file_exists(png_path))
			continue;

		struct jkps_noteskin_entry *out = &out_entries[*count];
		strncpy(out->xml_path, xml_path, sizeof(out->xml_path) - 1);
		out->xml_path[sizeof(out->xml_path) - 1] = '\0';

		if (local_pairs > 1) {
			char base[JKPS_NOTESKIN_NAME_LEN];
			basename_noext_into(ent->d_name, base, sizeof(base));
			if (label_hint && label_hint[0])
				snprintf(out->display_name, sizeof(out->display_name), "%s - %s", label_hint, base);
			else
				strncpy(out->display_name, base, sizeof(out->display_name) - 1),
					out->display_name[sizeof(out->display_name) - 1] = '\0';
		} else if (label_hint && label_hint[0]) {
			strncpy(out->display_name, label_hint, sizeof(out->display_name) - 1);
			out->display_name[sizeof(out->display_name) - 1] = '\0';
		} else {
			basename_noext_into(ent->d_name, out->display_name, sizeof(out->display_name));
		}

		(*count)++;
	}
	os_closedir(dir);

	/* Pass 3: recurse into subfolders, each labeled with its own name so
	 * packs nested inside a "collection" folder (or a collection nested
	 * inside another one) stay identifiable however deep they sit. */
	dir = os_opendir(dir_path);
	if (!dir)
		return;
	while (*count < max_entries && (ent = os_readdir(dir)) != NULL) {
		if (!ent->directory)
			continue;
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		char subdir[JKPS_NOTESKIN_PATH_LEN];
		int len = snprintf(subdir, sizeof(subdir), "%s/%s", dir_path, ent->d_name);
		if (len < 0 || (size_t)len >= sizeof(subdir))
			continue;

		jkps_noteskin_collect(subdir, ent->d_name, out_entries, count, max_entries, depth + 1);
	}
	os_closedir(dir);
}

int jkps_noteskin_scan_folder(const char *folder, struct jkps_noteskin_entry *out_entries, int max_entries)
{
	if (!folder || !folder[0] || max_entries <= 0)
		return 0;

	int count = 0;
	jkps_noteskin_collect(folder, NULL, out_entries, &count, max_entries, 0);
	return count;
}
