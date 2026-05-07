/*
 * typedefs.c - typedef loading and filtering for pgindent.
 *
 * This module loads typedef names from a file, applies hardcoded additions
 * and exclusions matching the behavior of the original Perl pgindent script,
 * and registers them into the indent engine via add_typename().
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */

#include "c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "indent.h"
#include "pgindent.h"

/*
 * Hardcoded typedef additions.
 *
 * These names should always be treated as typedefs even if they don't appear
 * in the typedefs file (e.g., "bool" is a macro per <stdbool.h>).
 */
static const char *const hardcoded_additions[] = {
	"bool",
	"regex_t",
	"regmatch_t",
	"regoff"
};

/*
 * Hardcoded typedef exclusions.
 *
 * These names should never be treated as typedefs, even if they appear in
 * the typedefs file.  Various headers might define them as typedefs in some
 * builds, but treating them as such produces incorrect indentation.
 */
static const char *const hardcoded_exclusions[] = {
	"FD_SET",
	"LookupSet",
	"boolean",
	"date",
	"duration",
	"element_type",
	"inquiry",
	"iterator",
	"other",
	"pointer",
	"reference",
	"rep",
	"string",
	"timestamp",
	"type",
	"wrap"
};

/*
 * is_excluded_typedef - check if a name is in the exclusion list.
 */
static bool
is_excluded_typedef(const char *name)
{
	int			i;

	for (i = 0; i < (int) lengthof(hardcoded_exclusions); i++)
	{
		if (strcmp(name, hardcoded_exclusions[i]) == 0)
			return true;
	}
	return false;
}

/*
 * load_typedefs - load typedef names and register them with the indent engine.
 *
 * If 'file' is non-NULL, typedefs are loaded from that path.
 * If 'file' is NULL, we search for typedefs.list in:
 *   1. current directory ("./typedefs.list")
 *   2. source tools directory ("src/tools/pgindent/typedefs.list")
 *   3. /usr/local/etc/typedefs.list
 *
 * After loading from the file, hardcoded additions are added and hardcoded
 * exclusions are removed.  If 'extra_str' is non-NULL, it is parsed as a
 * comma/space separated list of additional typedef names.
 *
 * Each valid typedef name is registered via add_typename().
 * Exits with status 1 if no typedefs file can be found.
 */
void
load_typedefs(const char *file, const char *extra_str)
{
	const char *typedefs_file = file;
	FILE	   *fp;
	char		line[BUFSIZ];
	int			i;

	/* Search for typedefs file if not explicitly specified */
	if (typedefs_file == NULL)
	{
		static const char *const search_paths[] = {
			"./typedefs.list",
			"src/tools/pgindent/typedefs.list",
			"/usr/local/etc/typedefs.list"
		};

		for (i = 0; i < (int) lengthof(search_paths); i++)
		{
			if (access(search_paths[i], R_OK) == 0)
			{
				typedefs_file = search_paths[i];
				break;
			}
		}
	}

	if (typedefs_file == NULL)
	{
		fprintf(stderr, "pgindent: cannot locate typedefs file\n");
		exit(1);
	}

	fp = fopen(typedefs_file, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "pgindent: cannot open typedefs file \"%s\"\n",
				typedefs_file);
		exit(1);
	}

	/* Read typedefs from file, one per line */
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		/* Remove trailing whitespace (newline, spaces, tabs, CR) */
		size_t		len = strlen(line);

		while (len > 0 &&
			   (line[len - 1] == '\n' || line[len - 1] == '\r' ||
				line[len - 1] == ' ' || line[len - 1] == '\t'))
			len--;
		line[len] = '\0';

		/* Skip empty lines */
		if (len == 0)
			continue;

		/* Skip excluded names */
		if (is_excluded_typedef(line))
			continue;

		add_typename(line);
	}

	fclose(fp);

	/* Add hardcoded additions (these are never in the exclusion list) */
	for (i = 0; i < (int) lengthof(hardcoded_additions); i++)
		add_typename(hardcoded_additions[i]);

	/* Parse extra typedefs from --list-of-typedefs string */
	if (extra_str != NULL)
	{
		char	   *copy = strdup(extra_str);
		char	   *token;
		char	   *saveptr = NULL;

		if (copy == NULL)
		{
			fprintf(stderr, "pgindent: out of memory\n");
			exit(1);
		}

		/* Split on commas, spaces, tabs, and newlines */
		for (token = strtok_r(copy, ", \t\n", &saveptr);
			 token != NULL;
			 token = strtok_r(NULL, ", \t\n", &saveptr))
		{
			if (token[0] == '\0')
				continue;

			if (!is_excluded_typedef(token))
				add_typename(token);
		}

		free(copy);
	}
}
