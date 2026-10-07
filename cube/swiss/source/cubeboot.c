/*
 * This file is part of Swiss.
 *
 * Swiss is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Swiss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * with Swiss.  If not, see <https://www.gnu.org/licenses/>.
 */

// Installs/uninstalls cubeboot (https://github.com/OffBroadway/cubeboot) in front
// of Swiss using the iplboot layout from cubeboot's SD boot guide:
//   disabled: /ipl.dol = Swiss, /cubeboot.dol = cubeboot
//   enabled:  /ipl.dol = cubeboot, /boot.dol = Swiss, /cubeboot.ini default_program = /boot.dol
// cubeboot passes /boot.cli to Swiss as arguments, which carries CUBEBOOT_INTRO_ARG
// so that Swiss doesn't autoboot /ipl.dol (cubeboot) again.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "cubeboot.h"
#include "deviceHandler.h"
#include "files.h"
#include "swiss.h"

#define CUBEBOOT_DOL "cubeboot.dol"
#define CUBEBOOT_INI "cubeboot.ini"
#define IPL_DOL      "ipl.dol"
#define BOOT_DOL     "boot.dol"
#define BOOT_CLI     "boot.cli"

char *cubebootColorStr[CUBEBOOT_COLOR_MAX] = {"Default", "Random", "Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Black", "White"};
static const char *cubebootColorValue[CUBEBOOT_COLOR_MAX] = {NULL, "random", "ff0000", "ff8000", "ffd000", "00c000", "0040ff", "6a5acd", "202020", "f0f0f0"};

static const char *iniKeys[] = {"default_program", "cube_color"};
static const char *cliKeys[] = {"CubebootIntro"};

bool cubeboot_intro_launched(int argc, char *argv[]) {
	for(int i = 1; i < argc; i++) {
		if(argv[i] != NULL && !strcmp(argv[i], CUBEBOOT_INTRO_ARG)) {
			return true;
		}
	}
	return false;
}

static bool root_exists(const char *name) {
	file_handle *file = calloc(1, sizeof(file_handle));
	if(!file) return false;
	concat_path(file->name, devices[DEVICE_CONFIG]->initial->name, name);
	bool ret = !devices[DEVICE_CONFIG]->statFile(file);
	devices[DEVICE_CONFIG]->closeFile(file);
	free(file);
	return ret;
}

static bool root_rename(const char *from, const char *to) {
	file_handle *file = calloc(1, sizeof(file_handle));
	char *newName = calloc(1, PATHNAME_MAX);
	bool ret = false;
	if(file && newName) {
		concat_path(file->name, devices[DEVICE_CONFIG]->initial->name, from);
		concat_path(newName, devices[DEVICE_CONFIG]->initial->name, to);
		s32 res = devices[DEVICE_CONFIG]->renameFile(file, newName);
		print_debug("cubeboot: rename %s -> %s = %d\n", from, to, res);
		ret = !res;
	}
	free(newName);
	free(file);
	return ret;
}

// Copies the lines of src to fp, dropping any line that sets one of keys
static void copy_without_keys(FILE *fp, char *src, const char **keys, int numKeys) {
	char *line, *linectx = NULL;
	for(line = strtok_r(src, "\r\n", &linectx); line != NULL; line = strtok_r(NULL, "\r\n", &linectx)) {
		char *start = line + strspn(line, " \t");
		bool drop = false;
		for(int i = 0; i < numKeys; i++) {
			size_t len = strlen(keys[i]);
			if(!strncmp(start, keys[i], len) && strchr(" \t=", start[len]) && start[len] != '\0') {
				drop = true;
				break;
			}
		}
		if(!drop) {
			fprintf(fp, "%s\n", line);
		}
	}
}

// Rewrites the keys we manage in cubeboot.ini, keeping any other user settings
static bool write_ini(int color) {
	char *old = config_file_read(CUBEBOOT_INI);
	char *contents = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&contents, &len);
	if(!fp) {
		free(old);
		return false;
	}
	if(old) {
		copy_without_keys(fp, old, iniKeys, sizeof(iniKeys) / sizeof(*iniKeys));
	}
	fprintf(fp, "default_program = /%s\n", BOOT_DOL);
	if(color > 0 && color < CUBEBOOT_COLOR_MAX) {
		fprintf(fp, "cube_color = %s\n", cubebootColorValue[color]);
	}
	fclose(fp);
	bool ret = config_file_write(CUBEBOOT_INI, contents);
	free(contents);
	free(old);
	return ret;
}

// Adds or removes CUBEBOOT_INTRO_ARG in boot.cli, keeping any other arguments
static bool write_cli(bool add) {
	char *old = config_file_read(BOOT_CLI);
	char *contents = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&contents, &len);
	if(!fp) {
		free(old);
		return false;
	}
	if(old) {
		copy_without_keys(fp, old, cliKeys, sizeof(cliKeys) / sizeof(*cliKeys));
	}
	if(add) {
		fprintf(fp, "%s\n", CUBEBOOT_INTRO_ARG);
	}
	fclose(fp);
	bool ret = true;
	if(len == 0) {
		config_file_delete(BOOT_CLI);
	}
	else {
		ret = config_file_write(BOOT_CLI, contents);
	}
	free(contents);
	free(old);
	return ret;
}

// Returns NULL on success, or an error message
const char *cubeboot_intro_apply(bool enable, int color) {
	if(!config_set_device()) {
		return "The configuration device is not available.";
	}
	// The file browser keeps handles open on the current directory's entries,
	// and FatFs refuses to rename open files, so release them and rescan later
	if(devices[DEVICE_CONFIG] == devices[DEVICE_CUR]) {
		freeFiles();
		needsRefresh = 1;
	}
	const char *err = NULL;
	bool installed = root_exists(IPL_DOL) && root_exists(BOOT_DOL) && !root_exists(CUBEBOOT_DOL) && root_exists(CUBEBOOT_INI);
	if(enable) {
		if(!installed) {
			if(!root_exists(CUBEBOOT_DOL)) {
				err = "Copy cubeboot.dol to the root of the\nconfiguration device first.";
			}
			else if(!root_exists(IPL_DOL)) {
				err = "Swiss was not found at /ipl.dol.\nOnly the iplboot layout is supported.";
			}
			else if(root_exists(BOOT_DOL)) {
				err = "/boot.dol already exists.\nRemove or rename it first.";
			}
			else if(!root_rename(IPL_DOL, BOOT_DOL)) {
				err = "Failed to rename /ipl.dol.";
			}
			else if(!root_rename(CUBEBOOT_DOL, IPL_DOL)) {
				root_rename(BOOT_DOL, IPL_DOL);
				err = "Failed to rename /cubeboot.dol.";
			}
		}
		if(!err && !(write_ini(color) && write_cli(true))) {
			err = "Failed to write cubeboot.ini or boot.cli.";
			if(!installed) {
				root_rename(IPL_DOL, CUBEBOOT_DOL);
				root_rename(BOOT_DOL, IPL_DOL);
			}
		}
	}
	else if(installed) {
		if(!root_rename(IPL_DOL, CUBEBOOT_DOL)) {
			err = "Failed to rename /ipl.dol.";
		}
		else if(!root_rename(BOOT_DOL, IPL_DOL)) {
			root_rename(CUBEBOOT_DOL, IPL_DOL);
			err = "Failed to rename /boot.dol.";
		}
		else {
			write_cli(false);
		}
	}
	config_unset_device();
	return err;
}
