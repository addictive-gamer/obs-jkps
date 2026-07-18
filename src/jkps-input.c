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

#include "jkps-input.h"
#include <util/platform.h>
#include <string.h>
#include <math.h>

uint64_t jkps_now_ms(void)
{
	return os_gettime_ns() / 1000000ULL;
}

static void push_press(struct jkps_stats *stats, uint64_t now_ms)
{
	stats->press_times_ms[stats->ring_head] = now_ms;
	stats->ring_head = (stats->ring_head + 1) % JKPS_PRESS_RING_CAP;
	if (stats->ring_count < JKPS_PRESS_RING_CAP)
		stats->ring_count++;
	stats->total_presses++;
}

#if defined(_WIN32)
#include <windows.h>

void jkps_poll_keys(struct jkps_key_state *keys, int num_keys, struct jkps_stats *stats, uint64_t now_ms)
{
	for (int i = 0; i < num_keys; i++) {
		struct jkps_key_state *k = &keys[i];
		if (k->vk <= 0)
			continue;

		k->prev_down = k->down;
		k->down = (GetAsyncKeyState(k->vk) & 0x8000) != 0;

		if (k->down && !k->prev_down) {
			k->total_presses++;
			push_press(stats, now_ms);
		}
	}
}

#else /* Non-Windows: no global input polling available (stub) */

void jkps_poll_keys(struct jkps_key_state *keys, int num_keys, struct jkps_stats *stats, uint64_t now_ms)
{
	(void)keys;
	(void)num_keys;
	(void)stats;
	(void)now_ms;
}

#endif

void jkps_stats_update(struct jkps_stats *stats, uint64_t now_ms, float delta_seconds)
{
	/* Drop timestamps older than 1000 ms from the ring buffer window. */
	while (stats->ring_count > 0) {
		int oldest_idx = (stats->ring_head - stats->ring_count + JKPS_PRESS_RING_CAP) % JKPS_PRESS_RING_CAP;
		uint64_t oldest = stats->press_times_ms[oldest_idx];
		if (now_ms - oldest > 1000) {
			stats->ring_count--;
		} else {
			break;
		}
	}

	stats->kps = (float)stats->ring_count;

	/* Exponential moving average smooths short-term jitter so the BPM
	 * readout does not swing wildly between individual key presses. */
	const float tau = 0.6f;
	float alpha = delta_seconds > 0.0f ? (1.0f - expf(-delta_seconds / tau)) : 1.0f;
	stats->kps_ema += (stats->kps - stats->kps_ema) * alpha;

	/* 15 = 60 (sec/min) / 4 (1/4 time signature commonly used for streams),
	 * matching the convention used by the original JKPS project. */
	stats->bpm = stats->kps_ema * 15.0f;
}

void jkps_stats_reset(struct jkps_stats *stats, struct jkps_key_state *keys, int num_keys)
{
	memset(stats, 0, sizeof(*stats));
	for (int i = 0; i < num_keys; i++)
		keys[i].total_presses = 0;
}
