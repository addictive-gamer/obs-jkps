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

#include "jkps-keynames.h"
#include <stddef.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>

/* clang-format off */
const struct jkps_keyname_entry jkps_keynames[] = {
	/* Letters */
	{ 'A', "key_a", "A" }, { 'B', "key_b", "B" }, { 'C', "key_c", "C" },
	{ 'D', "key_d", "D" }, { 'E', "key_e", "E" }, { 'F', "key_f", "F" },
	{ 'G', "key_g", "G" }, { 'H', "key_h", "H" }, { 'I', "key_i", "I" },
	{ 'J', "key_j", "J" }, { 'K', "key_k", "K" }, { 'L', "key_l", "L" },
	{ 'M', "key_m", "M" }, { 'N', "key_n", "N" }, { 'O', "key_o", "O" },
	{ 'P', "key_p", "P" }, { 'Q', "key_q", "Q" }, { 'R', "key_r", "R" },
	{ 'S', "key_s", "S" }, { 'T', "key_t", "T" }, { 'U', "key_u", "U" },
	{ 'V', "key_v", "V" }, { 'W', "key_w", "W" }, { 'X', "key_x", "X" },
	{ 'Y', "key_y", "Y" }, { 'Z', "key_z", "Z" },

	/* Digits (top row) */
	{ '0', "key_0", "0" }, { '1', "key_1", "1" }, { '2', "key_2", "2" },
	{ '3', "key_3", "3" }, { '4', "key_4", "4" }, { '5', "key_5", "5" },
	{ '6', "key_6", "6" }, { '7', "key_7", "7" }, { '8', "key_8", "8" },
	{ '9', "key_9", "9" },

	/* Common rhythm-game / modifier keys */
	{ VK_SPACE, "key_space", "Space" },
	{ VK_LSHIFT, "key_lshift", "LShift" },
	{ VK_RSHIFT, "key_rshift", "RShift" },
	{ VK_LCONTROL, "key_lctrl", "LCtrl" },
	{ VK_RCONTROL, "key_rctrl", "RCtrl" },
	{ VK_LMENU, "key_lalt", "LAlt" },
	{ VK_RMENU, "key_ralt", "RAlt" },
	{ VK_TAB, "key_tab", "Tab" },
	{ VK_CAPITAL, "key_capslock", "CapsLock" },
	{ VK_RETURN, "key_enter", "Enter" },
	{ VK_BACK, "key_backspace", "Backspace" },
	{ VK_UP, "key_up", "Up" },
	{ VK_DOWN, "key_down", "Down" },
	{ VK_LEFT, "key_left", "Left" },
	{ VK_RIGHT, "key_right", "Right" },
	{ VK_OEM_COMMA, "key_comma", "," },
	{ VK_OEM_PERIOD, "key_period", "." },
	{ VK_OEM_2, "key_slash", "/" },
	{ VK_OEM_1, "key_semicolon", ";" },
	{ VK_OEM_4, "key_lbracket", "[" },
	{ VK_OEM_6, "key_rbracket", "]" },

	/* Numpad */
	{ VK_NUMPAD0, "key_num0", "Num0" }, { VK_NUMPAD1, "key_num1", "Num1" },
	{ VK_NUMPAD2, "key_num2", "Num2" }, { VK_NUMPAD3, "key_num3", "Num3" },
	{ VK_NUMPAD4, "key_num4", "Num4" }, { VK_NUMPAD5, "key_num5", "Num5" },
	{ VK_NUMPAD6, "key_num6", "Num6" }, { VK_NUMPAD7, "key_num7", "Num7" },
	{ VK_NUMPAD8, "key_num8", "Num8" }, { VK_NUMPAD9, "key_num9", "Num9" },

	/* Mouse buttons */
	{ VK_LBUTTON, "mouse_left", "M1" },
	{ VK_RBUTTON, "mouse_right", "M2" },
	{ VK_MBUTTON, "mouse_middle", "M3" },
	{ VK_XBUTTON1, "mouse_x1", "M4" },
	{ VK_XBUTTON2, "mouse_x2", "M5" },
};
/* clang-format on */

const size_t jkps_keynames_count = sizeof(jkps_keynames) / sizeof(jkps_keynames[0]);

const char *jkps_keyname_default_label(int vk)
{
	for (size_t i = 0; i < jkps_keynames_count; i++) {
		if (jkps_keynames[i].vk == vk)
			return jkps_keynames[i].label;
	}

	static char fallback[16];
	snprintf(fallback, sizeof(fallback), "VK%d", vk);
	return fallback;
}

#else /* !_WIN32 : keep the symbols available so the file still links */

const struct jkps_keyname_entry jkps_keynames[] = {{0, "unknown", "?"}};
const size_t jkps_keynames_count = 1;

const char *jkps_keyname_default_label(int vk)
{
	(void)vk;
	return "?";
}

#endif
