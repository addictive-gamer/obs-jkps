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

#include <obs-module.h>
#include <graphics/image-file.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* obs-jkps reads (never ships) Sparrow/TexturePacker atlas noteskins - the
 * same "one PNG + one XML" format Psych Engine and most modern FNF engines
 * use for arrow noteskins. This module only understands the format; it
 * contains no game or fan art. "Funkin' Skins" and "Local Skins" are both
 * empty by default and only ever show packs the user points the plugin at
 * on their own disk. See NOTICE.md. */

/* Lane order matches the plugin's default D F J K layout, left to right.
 * Atlas-format noteskins are inherently 4-directional, so this is fixed
 * regardless of how many keys the user has enabled elsewhere. */
enum jkps_noteskin_dir {
	JKPS_DIR_LEFT = 0,
	JKPS_DIR_DOWN,
	JKPS_DIR_UP,
	JKPS_DIR_RIGHT,
	JKPS_DIR_COUNT,
};

enum jkps_noteskin_state {
	JKPS_SKIN_STATIC = 0, /* idle / "static" frame */
	JKPS_SKIN_PRESSED,    /* "press" frame */
	JKPS_SKIN_CONFIRM,    /* "confirm" frame (held/hit flash); falls back
				* to PRESSED if the pack doesn't have one */
	JKPS_SKIN_STATE_COUNT,
};

struct jkps_atlas_rect {
	int x, y, w, h;
	bool valid;
};

struct jkps_noteskin {
	gs_image_file_t atlas_img;
	bool atlas_loaded;
	struct jkps_atlas_rect frames[JKPS_DIR_COUNT][JKPS_SKIN_STATE_COUNT];
	char source_xml_path[512];
};

/* Loads a Sparrow/TexturePacker atlas: `xml_path` is the .xml file, and the
 * PNG is expected next to it with the same base name (this matches every
 * Psych-Engine-style pack: e.g. "NoteSkin.xml" + "NoteSkin.png"). Returns
 * false if the XML can't be read/parsed, the matching PNG is missing, or no
 * recognizable arrow frames were found in it (i.e. this isn't this kind of
 * noteskin atlas). Must be called between obs_enter_graphics/leave_graphics
 * like any other gs_image_file_t load. */
bool jkps_noteskin_load(const char *xml_path, struct jkps_noteskin *out);

/* Frees the atlas texture. Must be called within obs_enter_graphics/leave_graphics. */
void jkps_noteskin_free(struct jkps_noteskin *ns);

#define JKPS_NOTESKIN_MAX_ENTRIES 64
#define JKPS_NOTESKIN_NAME_LEN 128
#define JKPS_NOTESKIN_PATH_LEN 512

struct jkps_noteskin_entry {
	char display_name[JKPS_NOTESKIN_NAME_LEN];
	char xml_path[JKPS_NOTESKIN_PATH_LEN];
};

/* Recursively scans `folder` (a few levels deep) for every .xml file that
 * has a same-named .png sitting next to it (a "pack"), so the user can drop
 * noteskin packs into a root folder - directly, one folder deep, or nested
 * inside a "collection" folder that groups several packs together - and see
 * all of them listed. A folder that flattens several packs into one place
 * (no subfolders of their own) gets each entry's name disambiguated with
 * its xml filename; a folder holding exactly one pack keeps a clean name.
 * Returns the number of packs found and written into out_entries (capped at
 * max_entries). */
int jkps_noteskin_scan_folder(const char *folder, struct jkps_noteskin_entry *out_entries, int max_entries);

#ifdef __cplusplus
}
#endif
