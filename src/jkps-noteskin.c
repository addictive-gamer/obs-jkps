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

/* Looks directly inside dir_path (one level, no recursion) for exactly one
 * .xml file that has a same-named .png sitting next to it. */
static bool find_single_pack_in_dir(const char *dir_path, char *xml_path_out, size_t out_size)
{
	os_dir_t *dir = os_opendir(dir_path);
	if (!dir)
		return false;

	bool found = false;
	struct os_dirent *ent;
	while ((ent = os_readdir(dir)) != NULL) {
		if (ent->directory)
			continue;
		if (!ends_with_ci(ent->d_name, ".xml"))
			continue;

		char xml_path[JKPS_NOTESKIN_PATH_LEN];
		snprintf(xml_path, sizeof(xml_path), "%s/%s", dir_path, ent->d_name);

		char png_path[JKPS_NOTESKIN_PATH_LEN];
		strncpy(png_path, xml_path, sizeof(png_path) - 1);
		png_path[sizeof(png_path) - 1] = '\0';
		xml_path_to_png(png_path);

		if (!file_exists(png_path))
			continue;

		strncpy(xml_path_out, xml_path, out_size - 1);
		xml_path_out[out_size - 1] = '\0';
		found = true;
		break; /* first valid pair wins */
	}

	os_closedir(dir);
	return found;
}

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

int jkps_noteskin_scan_folder(const char *folder, struct jkps_noteskin_entry *out_entries, int max_entries)
{
	if (!folder || !folder[0] || max_entries <= 0)
		return 0;

	int count = 0;

	/* List every valid .xml+.png pair found directly at the folder's
	 * root - not just the first one - so pointing at a folder that has
	 * several packs dumped flat inside it (no subfolders) lets the user
	 * pick between all of them instead of silently only ever offering
	 * whichever pair the OS happens to enumerate first. */
	os_dir_t *root_dir = os_opendir(folder);
	if (root_dir) {
		struct os_dirent *root_ent;
		while (count < max_entries && (root_ent = os_readdir(root_dir)) != NULL) {
			if (root_ent->directory)
				continue;
			if (!ends_with_ci(root_ent->d_name, ".xml"))
				continue;

			char xml_path[JKPS_NOTESKIN_PATH_LEN];
			snprintf(xml_path, sizeof(xml_path), "%s/%s", folder, root_ent->d_name);

			char png_path[JKPS_NOTESKIN_PATH_LEN];
			strncpy(png_path, xml_path, sizeof(png_path) - 1);
			png_path[sizeof(png_path) - 1] = '\0';
			xml_path_to_png(png_path);

			if (!file_exists(png_path))
				continue;

			strncpy(out_entries[count].xml_path, xml_path, sizeof(out_entries[count].xml_path) - 1);
			out_entries[count].xml_path[sizeof(out_entries[count].xml_path) - 1] = '\0';
			basename_noext_into(xml_path, out_entries[count].display_name,
					    sizeof(out_entries[count].display_name));
			count++;
		}
		os_closedir(root_dir);
	}

	/* Plus one entry per subfolder that is itself a pack, so a root
	 * folder holding several noteskins (each in its own subfolder) lists
	 * all of them alongside anything found directly above. */
	os_dir_t *dir = os_opendir(folder);
	if (!dir)
		return count;

	struct os_dirent *ent;
	while (count < max_entries && (ent = os_readdir(dir)) != NULL) {
		if (!ent->directory)
			continue;
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		char subdir[JKPS_NOTESKIN_PATH_LEN];
		snprintf(subdir, sizeof(subdir), "%s/%s", folder, ent->d_name);

		char xml_path[JKPS_NOTESKIN_PATH_LEN];
		if (!find_single_pack_in_dir(subdir, xml_path, sizeof(xml_path)))
			continue;

		strncpy(out_entries[count].xml_path, xml_path, sizeof(out_entries[count].xml_path) - 1);
		out_entries[count].xml_path[sizeof(out_entries[count].xml_path) - 1] = '\0';
		strncpy(out_entries[count].display_name, ent->d_name, sizeof(out_entries[count].display_name) - 1);
		out_entries[count].display_name[sizeof(out_entries[count].display_name) - 1] = '\0';
		count++;
	}
	os_closedir(dir);

	return count;
}
