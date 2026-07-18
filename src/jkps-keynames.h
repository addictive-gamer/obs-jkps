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

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single entry mapping a Windows virtual-key code to a human readable
 * name. Used to populate the "key" dropdown in the source properties and
 * to derive a default on-screen label when the user does not set a custom
 * one. This list intentionally covers the keys/buttons that matter for
 * rhythm games (osu!, Etterna, StepMania, ADOFAI, mania-style games...),
 * mirroring the most common bindings used with the original JKPS project. */
struct jkps_keyname_entry {
	int vk;
	const char *id;    /* stable identifier stored in the .json scene file */
	const char *label; /* short default on-screen label                   */
};

extern const struct jkps_keyname_entry jkps_keynames[];
extern const size_t jkps_keynames_count;

/* Look up the default short label for a given virtual-key code.
 * Falls back to a generic "VK <n>" style string when unknown. */
const char *jkps_keyname_default_label(int vk);

#ifdef __cplusplus
}
#endif
