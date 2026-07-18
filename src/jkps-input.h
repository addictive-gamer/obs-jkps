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

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JKPS_MAX_KEYS 8
#define JKPS_PRESS_RING_CAP 512

/* Runtime state for a single tracked key/button. */
struct jkps_key_state {
	int vk;
	bool down;
	bool prev_down;
	uint64_t total_presses;
};

/* Aggregated statistics engine: tracks global press timestamps to derive
 * an instantaneous "keys per second" value, a running total, and an
 * approximate BPM figure.
 *
 * The BPM figure is a simplified equivalent of the algorithm used by the
 * original JKPS project: it assumes a 1/4 note rhythm-game stream
 * (4 notes per beat => bpm = kps * 60 / 4 = kps * 15) applied to a
 * smoothed KPS value instead of JKPS's fixed 60-tick accumulator, since
 * this plugin is not driven by a fixed simulation step. */
struct jkps_stats {
	uint64_t press_times_ms[JKPS_PRESS_RING_CAP];
	int ring_head;
	int ring_count;

	uint64_t total_presses;
	float kps;      /* presses within the trailing 1000 ms window */
	float kps_ema;  /* smoothed KPS, used as the basis for the BPM estimate */
	float bpm;
};

/* Poll the state of every configured key/button using the Windows input
 * APIs (GetAsyncKeyState). Must be called periodically (e.g. every
 * video_tick). Updates `keys[i].down` and feeds newly detected key-down
 * edges into `stats`. No-op on non-Windows platforms. */
void jkps_poll_keys(struct jkps_key_state *keys, int num_keys, struct jkps_stats *stats, uint64_t now_ms);

/* Recomputes stats->kps / kps_ema / bpm based on the current time, dropping
 * timestamps that fell out of the trailing 1-second window. Called on every
 * tick regardless of whether a new press happened, so the KPS value decays
 * back down to 0 correctly. */
void jkps_stats_update(struct jkps_stats *stats, uint64_t now_ms, float delta_seconds);

/* Resets all counters (used by the "Reset statistics" hotkey/button). */
void jkps_stats_reset(struct jkps_stats *stats, struct jkps_key_state *keys, int num_keys);

/* Returns the current time in milliseconds, monotonic, suitable for the
 * `now_ms` parameters above. Thin wrapper over os_gettime_ns(). */
uint64_t jkps_now_ms(void);

#ifdef __cplusplus
}
#endif
