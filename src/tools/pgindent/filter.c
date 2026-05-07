/*
 * filter.c - file filtering for pgindent.
 *
 * Implements exclude pattern matching, derived file detection (.y/.l siblings),
 * and file list deduplication.
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */
#include "c.h"

#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pgindent.h"

/*
 * ExcludePatterns - compiled POSIX regexes from exclude files.
 */
typedef struct ExcludePatterns
{
	regex_t    *patterns;
	int			count;
	int			capacity;
}			ExcludePatterns;

#define EXCLUDE_INITIAL_CAPACITY 32

/*
 * exclude_patterns_init - initialize an ExcludePatterns structure.
 */
static void
exclude_patterns_init(ExcludePatterns * ep)
{
	ep->patterns = NULL;
	ep->count = 0;
	ep->capacity = 0;
}

/*
 * exclude_patterns_free - free all compiled patterns.
 */
static void
exclude_patterns_free(ExcludePatterns * ep)
{
	int			i;

	for (i = 0; i < ep->count; i++)
		regfree(&ep->patterns[i]);
	free(ep->patterns);
	ep->patterns = NULL;
	ep->count = 0;
	ep->capacity = 0;
}

/*
 * exclude_patterns_add - compile and add a regex pattern.
 *
 * Returns 0 on success, -1 on compilation failure.
 */
static int
exclude_patterns_add(ExcludePatterns * ep, const char *pattern)
{
	int			ret;

	if (ep->count >= ep->capacity)
	{
		int			new_capacity;

		new_capacity = (ep->capacity == 0) ? EXCLUDE_INITIAL_CAPACITY
			: ep->capacity * 2;
		ep->patterns = realloc(ep->patterns, new_capacity * sizeof(regex_t));
		if (!ep->patterns)
		{
			fprintf(stderr, "pgindent: out of memory\n");
			exit(1);
		}
		ep->capacity = new_capacity;
	}

	ret = regcomp(&ep->patterns[ep->count], pattern,
				  REG_EXTENDED | REG_NOSUB);
	if (ret != 0)
	{
		char		errbuf[256];

		regerror(ret, &ep->patterns[ep->count], errbuf, sizeof(errbuf));
		fprintf(stderr, "pgindent: invalid exclude pattern \"%s\": %s\n",
				pattern, errbuf);
		return -1;
	}

	ep->count++;
	return 0;
}

/*
 * load_exclude_file - read an exclude file and add patterns to the list.
 *
 * Lines starting with '#' are comments and are skipped.
 * Empty lines are skipped.
 */
static void
load_exclude_file(ExcludePatterns * ep, const char *filename)
{
	FILE	   *fp;
	char		line[4096];

	fp = fopen(filename, "r");
	if (!fp)
	{
		fprintf(stderr, "pgindent: cannot open exclude file \"%s\": %s\n",
				filename, strerror(errno));
		exit(1);
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		size_t		len;

		/* Strip trailing newline */
		len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[--len] = '\0';
		/* Strip trailing carriage return */
		if (len > 0 && line[len - 1] == '\r')
			line[--len] = '\0';

		/* Skip empty lines */
		if (len == 0)
			continue;

		/* Skip comment lines */
		if (line[0] == '#')
			continue;

		exclude_patterns_add(ep, line);
	}

	fclose(fp);
}

/*
 * file_matches_pattern - check if a file path matches any exclude pattern.
 */
static int
file_matches_pattern(const ExcludePatterns * ep, const char *path)
{
	int			i;

	for (i = 0; i < ep->count; i++)
	{
		if (regexec(&ep->patterns[i], path, 0, NULL, 0) == 0)
			return 1;
	}
	return 0;
}

/*
 * apply_exclude_patterns - filter a FileList by removing files that match
 * any pattern from the given exclude files.
 *
 * For each exclude file, reads lines (skipping comments and empty lines),
 * compiles them as POSIX extended regexes, and removes matching files from
 * the list.
 */
void
apply_exclude_patterns(FileList * fl, char **exclude_files, int nexcludes)
{
	ExcludePatterns ep;
	int			i;
	int			dst;

	if (nexcludes == 0)
		return;

	exclude_patterns_init(&ep);

	/* Load all exclude files */
	for (i = 0; i < nexcludes; i++)
		load_exclude_file(&ep, exclude_files[i]);

	/* Filter the file list in place */
	dst = 0;
	for (i = 0; i < fl->count; i++)
	{
		if (file_matches_pattern(&ep, fl->files[i]))
		{
			free(fl->files[i]);
		}
		else
		{
			fl->files[dst] = fl->files[i];
			dst++;
		}
	}
	fl->count = dst;

	exclude_patterns_free(&ep);
}

/*
 * is_derived_file - check if a .c or .h file has a .y or .l sibling.
 *
 * For example, "src/foo.c" is derived if "src/foo.y" or "src/foo.l" exists.
 * Similarly for .h files.
 */
bool
is_derived_file(const char *path)
{
	size_t		len;
	char	   *probe;

	len = strlen(path);
	if (len < 3)
		return false;

	/* Only check .c and .h files */
	if (path[len - 2] != '.' ||
		(path[len - 1] != 'c' && path[len - 1] != 'h'))
		return false;

	/* Allocate a copy to modify the extension */
	probe = strdup(path);
	if (!probe)
	{
		fprintf(stderr, "pgindent: out of memory\n");
		exit(1);
	}

	/* Check for .y sibling */
	probe[len - 1] = 'y';
	if (access(probe, F_OK) == 0)
	{
		free(probe);
		return true;
	}

	/* Check for .l sibling */
	probe[len - 1] = 'l';
	if (access(probe, F_OK) == 0)
	{
		free(probe);
		return true;
	}

	free(probe);
	return false;
}

/*
 * path_compare - comparison function for qsort on string pointers.
 */
static int
path_compare(const void *a, const void *b)
{
	return strcmp(*(const char **) a, *(const char **) b);
}

/*
 * deduplicate_files - eliminate duplicate paths from a FileList.
 *
 * Uses qsort + linear scan to remove consecutive duplicates.
 */
void
deduplicate_files(FileList * fl)
{
	int			i;
	int			dst;

	if (fl->count <= 1)
		return;

	/* Sort the file list */
	qsort(fl->files, fl->count, sizeof(char *), path_compare);

	/* Remove consecutive duplicates */
	dst = 1;
	for (i = 1; i < fl->count; i++)
	{
		if (strcmp(fl->files[i], fl->files[dst - 1]) == 0)
		{
			free(fl->files[i]);
		}
		else
		{
			fl->files[dst] = fl->files[i];
			dst++;
		}
	}
	fl->count = dst;
}
