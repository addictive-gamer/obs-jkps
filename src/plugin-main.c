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

#include <obs-module.h>
#include <plugin-support.h>
#include "jkps-source.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

OBS_MODULE_AUTHOR("addictive-gamer")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "JKPS-style Keys Per Second, Total and BPM overlay source (based on the original "
	       "JKPS project by Tonetfal, rebuilt as a native OBS Studio plugin source).";
}

bool obs_module_load(void)
{
	obs_register_source(&jkps_source_info);
	obs_log(LOG_INFO, "obs-jkps loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "obs-jkps unloaded");
}
