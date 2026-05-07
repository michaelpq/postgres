/*
 * discover.c - file discovery for pgindent.
 *
 * Implements recursive directory traversal to find *.c and *.h files,
 * and commit-mode file listing via git diff.
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#include "pgindent.h"

#define FILELIST_INITIAL_CAPACITY 64

/*
 * filelist_init - initialize a FileList with default capacity.
 */
void
filelist_init(FileList * fl)
{
	fl->files = NULL;
	fl->count = 0;
	fl->capacity = 0;
}

/*
 * filelist_free - free all memory associated with a FileList.
 */
void
filelist_free(FileList * fl)
{
	int			i;

	for (i = 0; i < fl->count; i++)
		free(fl->files[i]);
	free(fl->files);
	fl->files = NULL;
	fl->count = 0;
	fl->capacity = 0;
}

/*
 * filelist_add - add a path to the FileList, growing if necessary.
 */
static void
filelist_add(FileList * fl, const char *path)
{
	if (fl->count >= fl->capacity)
	{
		int			new_capacity;

		new_capacity = (fl->capacity == 0) ? FILELIST_INITIAL_CAPACITY
			: fl->capacity * 2;
		fl->files = realloc(fl->files, new_capacity * sizeof(char *));
		if (!fl->files)
		{
			fprintf(stderr, "pgindent: out of memory\n");
			exit(1);
		}
		fl->capacity = new_capacity;
	}
	fl->files[fl->count] = strdup(path);
	if (!fl->files[fl->count])
	{
		fprintf(stderr, "pgindent: out of memory\n");
		exit(1);
	}
	fl->count++;
}

/*
 * has_c_extension - check if a filename ends with .c or .h
 */
static int
has_c_extension(const char *name)
{
	size_t		len;

	len = strlen(name);
	if (len < 3)
		return 0;
	if (name[len - 2] == '.' &&
		(name[len - 1] == 'c' || name[len - 1] == 'h'))
		return 1;
	return 0;
}

/*
 * discover_recurse - recursively traverse a directory, adding *.c and *.h
 * files to the FileList.
 */
static void
discover_recurse(FileList * fl, const char *dirpath)
{
	DIR		   *dir;
	struct dirent *entry;

	dir = opendir(dirpath);
	if (!dir)
	{
		fprintf(stderr, "pgindent: cannot open directory \"%s\": %s\n",
				dirpath, strerror(errno));
		return;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		char		path[4096];
		struct stat st;

		/* Skip . and .. */
		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);

		if (stat(path, &st) != 0)
			continue;

		if (S_ISDIR(st.st_mode))
		{
			discover_recurse(fl, path);
		}
		else if (S_ISREG(st.st_mode))
		{
			if (has_c_extension(entry->d_name))
				filelist_add(fl, path);
		}
	}

	closedir(dir);
}

/*
 * strip_trailing_slashes - remove trailing '/' characters from a path string.
 * Modifies the string in place.  Never strips the leading '/' from a
 * root-only path.
 */
static void
strip_trailing_slashes(char *path)
{
	size_t		len = strlen(path);

	while (len > 1 && path[len - 1] == '/')
		path[--len] = '\0';
}

/*
 * discover_files_from_paths - discover files from the given paths.
 *
 * For each path: if it's a directory, recursively collect *.c and *.h files.
 * If it's a regular file, add it directly.
 *
 * Trailing slashes on directory paths are stripped to avoid constructing
 * paths with double slashes (e.g., "src//backend/...") which would break
 * exclude pattern matching.
 */
void
discover_files_from_paths(FileList * fl, char **paths, int npaths)
{
	int			i;

	for (i = 0; i < npaths; i++)
	{
		struct stat st;
		char	   *normalized;

		normalized = strdup(paths[i]);
		if (!normalized)
		{
			fprintf(stderr, "pgindent: out of memory\n");
			exit(1);
		}
		strip_trailing_slashes(normalized);

		if (stat(normalized, &st) != 0)
		{
			fprintf(stderr, "pgindent: cannot stat \"%s\": %s\n",
					normalized, strerror(errno));
			free(normalized);
			continue;
		}

		if (S_ISDIR(st.st_mode))
		{
			discover_recurse(fl, normalized);
		}
		else if (S_ISREG(st.st_mode))
		{
			filelist_add(fl, normalized);
		}

		free(normalized);
	}
}

/*
 * discover_files_from_commits - discover files changed in git commits.
 *
 * For each commit, runs:
 *   git diff --diff-filter=ACMR --name-only <commit>^ <commit>
 *
 * Collects *.c and *.h files from the output.
 */
void
discover_files_from_commits(FileList * fl, char **commits, int ncommits)
{
	int			i;

	for (i = 0; i < ncommits; i++)
	{
		char		cmd[1024];
		FILE	   *fp;
		char		line[4096];

		snprintf(cmd, sizeof(cmd),
				 "git diff --diff-filter=ACMR --name-only %s^ %s",
				 commits[i], commits[i]);

		fp = popen(cmd, "r");
		if (!fp)
		{
			fprintf(stderr, "pgindent: cannot execute \"%s\": %s\n",
					cmd, strerror(errno));
			exit(1);
		}

		while (fgets(line, sizeof(line), fp) != NULL)
		{
			size_t		len;

			/* Strip trailing newline */
			len = strlen(line);
			if (len > 0 && line[len - 1] == '\n')
				line[--len] = '\0';

			if (len == 0)
				continue;

			/* Only collect .c and .h files */
			if (has_c_extension(line))
				filelist_add(fl, line);
		}

		if (pclose(fp) != 0)
		{
			fprintf(stderr, "pgindent: git diff failed for commit \"%s\"\n",
					commits[i]);
			exit(1);
		}
	}
}
