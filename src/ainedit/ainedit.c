/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <iconv.h>
#include "ainedit.h"
#include "system4.h"
#include "system4/ain.h"
#include "alice.h"
#include "jaf.h"

extern int text_parse(void);

enum {
	LOPT_PROJECT = 256,
	LOPT_CODE,
	LOPT_JAF,
	LOPT_JSON,
	LOPT_TEXT,
	LOPT_TRANSCODE,
	LOPT_OUTPUT,
	LOPT_RAW,
	LOPT_AIN_VERSION,
	LOPT_SILENT,
};

static bool parse_version(const char *str, int *major, int *minor)
{
	char major_str[3];
	char minor_str[3];
	const char *dot = strchr(str, '.');

	if (dot) {
		if (dot - str > 2)
			return false;
		if (strlen(dot+1) > 2)
			return false;
		strncpy(major_str, str, dot - str);
		major_str[dot-str] = 0;
		strcpy(minor_str, dot+1);
	} else {
		if (strlen(str) > 2)
			return false;
		strcpy(major_str, str);
		strcpy(minor_str, "0");
	}

	*major = atoi(major_str);
	*minor = atoi(minor_str);
	return true;
}

enum input_type {
	IN_CODE,
	IN_JAF,
	IN_TEXT,
	IN_DECL
};

struct input {
	enum input_type type;
	const char *filename;
};

static struct input inputs[256];
static int nr_inputs = 0;

static void push_input(enum input_type type, const char *filename)
{
	if (nr_inputs >= 256)
		ALICE_ERROR("Too many inputs");
	inputs[nr_inputs++] = (struct input) {
		.type = type,
		.filename = filename
	};
}

int command_ain_edit(int argc, char *argv[])
{
	initialize_instructions();
	struct ain *ain;
	int err = AIN_SUCCESS;
	const char *project_file = NULL;
	const char *output_file = NULL;
	int major_version = 4;
	int minor_version = 0;
	bool transcode = false;
	uint32_t flags = 0;

	set_input_encoding("UTF-8");
	set_output_encoding("CP932");

	while (1) {
		int c = alice_getopt(argc, argv, &cmd_ain_edit);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
		case LOPT_PROJECT:
			project_file = optarg;
			break;
		case 'c':
		case LOPT_CODE:
			push_input(IN_CODE, optarg);
			break;
		case LOPT_JAF:
			push_input(IN_JAF, optarg);
			break;
		case 'j':
		case LOPT_JSON:
			push_input(IN_DECL, optarg);
			break;
		case 't':
		case LOPT_TEXT:
			push_input(IN_TEXT, optarg);
			break;
		case LOPT_TRANSCODE:
			transcode = true;
			set_input_encoding("CP932");
			set_output_encoding(optarg);
			break;
		case 'o':
		case LOPT_OUTPUT:
			output_file = optarg;
			break;
		case LOPT_RAW:
			flags |= ASM_RAW;
			break;
		case LOPT_AIN_VERSION:
			if (!parse_version(optarg, &major_version, &minor_version)) {
				ALICE_ERROR("Invalid AIN version (parse error)");
			}
			if (major_version < 4 || major_version > 14) {
				ALICE_ERROR("Invalid AIN version (4-14 supported)");
			}
			break;
		case LOPT_SILENT:
			sys_silent = true;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1) {
		USAGE_ERROR(&cmd_ain_edit, "Too many arguments.");
	}

	if (project_file) {
		// FIXME: this should be a separate command
		if (nr_inputs > 0) {
			WARNING("Input files specified on the command line are ignored in --project mode");
		}
		pje_build(project_file, major_version, minor_version);
		return 0;
	}

	if (!output_file) {
		output_file = "out.ain";
	}

	if (!argc) {
		ain = ain_new(major_version, minor_version);
	} else {
		if (!(ain = ain_open(argv[0], &err))) {
			ALICE_ERROR("Failed to open ain file: %s", ain_strerror(err));
		}
	}
	ain_init_member_functions(ain, conv_output_utf8);

	if (transcode) {
		// FIXME: this should be a separate command
		if (nr_inputs > 0) {
			WARNING("Input files specified on the command line are ignored in --transcode mode");
		}
		ain_transcode(ain);
		goto write_ain_file;
	}

	for (int i = 0; i < nr_inputs; i++) {
		switch (inputs[i].type) {
		case IN_CODE:
			asm_assemble_jam(inputs[i].filename, ain, flags);
			break;
		case IN_JAF:
			jaf_build(ain, &inputs[i].filename, 1, NULL, 0);
			break;
		case IN_TEXT:
			read_text(inputs[i].filename, ain);
			break;
		case IN_DECL:
			read_declarations(inputs[i].filename, ain);
			break;
		}
	}

write_ain_file:
	NOTICE("Writing AIN file...");
	ain_write(output_file, ain);
	ain_free(ain);
	return 0;
}

struct command cmd_ain_edit = {
	.name = "edit",
	.usage = "[options...] <input-file>",
	.description = "Edit a .ain file",
	.parent = &cmd_ain,
	.fun = command_ain_edit,
	.options = {
		{ "output",      'o', "Set the output file path",                     required_argument, LOPT_OUTPUT },
		{ "code",        'c', "Update the CODE section (assemble .jam file)", required_argument, LOPT_CODE },
		{ "jaf",         0,   "Update .ain file from .jaf source code",       required_argument, LOPT_JAF },
		{ "json",        'j', "Update .ain file from json data",              required_argument, LOPT_JSON },
		{ "project",     'p', "Build .ain from project file",                 required_argument, LOPT_PROJECT },
		{ "text",        't', "Update strings/messages",                      required_argument, LOPT_TEXT },
		{ "ain-version", 0,   "Specify the .ain version",                     required_argument, LOPT_AIN_VERSION },
		{ "raw",         0,   "Read code in raw mode",                        no_argument,       LOPT_RAW },
		{ "silent",      0,   "Don't write messages to stdout",               no_argument,       LOPT_SILENT },
		{ "transcode",   0,   "Change the .ain file's text encoding",         required_argument, LOPT_TRANSCODE },
		{ 0 }
	}
};
