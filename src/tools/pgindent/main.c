/*
 * main.c - CLI parsing and orchestration for pgindent.
 *
 * This is the main entry point for the consolidated pgindent tool.
 * It handles command-line argument parsing, file discovery, filtering,
 * and orchestrates the pre-process → indent → post-process pipeline
 * for each file.
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */

#include "c.h"

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "getopt_long.h"
#include "indent.h"
#include "indent_globs.h"
#include "pgindent.h"

#define INDENT_VERSION	"2.1.2"

/* Auto-loaded exclude file path within a PostgreSQL source tree */
#define PG_EXCLUDE_FILE	"src/tools/pgindent/exclude_file_patterns"

/* Exit status codes */
#define EXIT_SUCCESS_STATUS		0
#define EXIT_USAGE_ERROR		1
#define EXIT_CHECK_CHANGED		2
#define EXIT_INDENT_FAILURE		3

/* Signal handling */
static volatile sig_atomic_t interrupted = 0;

static void
signal_handler(int signo)
{
	interrupted = 1;
}

static void
setup_signal_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

/*
 * Dynamic string array for repeatable options (--excludes, --commit).
 */
typedef struct StringArray
{
	char	  **items;
	int			count;
	int			capacity;
}			StringArray;

static void
strarray_init(StringArray * sa)
{
	sa->items = NULL;
	sa->count = 0;
	sa->capacity = 0;
}

static void
strarray_add(StringArray * sa, const char *str)
{
	if (sa->count >= sa->capacity)
	{
		int			new_cap = (sa->capacity == 0) ? 8 : sa->capacity * 2;

		sa->items = realloc(sa->items, new_cap * sizeof(char *));
		if (!sa->items)
			err(1, NULL);
		sa->capacity = new_cap;
	}
	sa->items[sa->count] = strdup(str);
	if (!sa->items[sa->count])
		err(1, NULL);
	sa->count++;
}

static void
strarray_free(StringArray * sa)
{
	int			i;

	for (i = 0; i < sa->count; i++)
		free(sa->items[i]);
	free(sa->items);
	sa->items = NULL;
	sa->count = 0;
	sa->capacity = 0;
}

/*
 * read_file_to_buffer - read an entire file into a malloc'd buffer.
 * Returns the buffer and sets *lenp to the number of bytes read.
 */
static char *
read_file_to_buffer(const char *path, size_t *lenp)
{
	FILE	   *f;
	char	   *buf;
	size_t		alloc_size = 8192;
	size_t		len = 0;
	size_t		nread;

	f = fopen(path, "r");
	if (f == NULL)
		return NULL;

	buf = malloc(alloc_size);
	if (buf == NULL)
		err(1, NULL);

	while ((nread = fread(buf + len, 1, alloc_size - len, f)) > 0)
	{
		len += nread;
		if (len == alloc_size)
		{
			alloc_size *= 2;
			buf = realloc(buf, alloc_size);
			if (buf == NULL)
				err(1, NULL);
		}
	}
	fclose(f);

	buf[len] = '\0';
	*lenp = len;
	return buf;
}

/*
 * write_file_from_buffer - write a buffer to a file atomically.
 * Returns 0 on success, -1 on failure.
 */
static int
write_file_from_buffer(const char *path, const char *buf, size_t len)
{
	FILE	   *f;

	f = fopen(path, "wb");
	if (f == NULL)
		return -1;

	if (fwrite(buf, 1, len, f) != len)
	{
		fclose(f);
		return -1;
	}

	if (fclose(f) != 0)
		return -1;

	return 0;
}

/*
 * process_file_full - run the full pipeline on a single file.
 *
 * On success with changes, *out_buf and *out_len are set to the new content.
 * The caller must free *out_buf.
 *
 * Returns:
 *   0 = file unchanged
 *   1 = file changed (out_buf/out_len set)
 *  -1 = indent failure
 */
static int
process_file_full(const char *path, char **out_buf, size_t *out_len)
{
	char	   *source;
	size_t		source_len;
	char	   *preprocessed;
	size_t		preprocessed_len;
	char	   *indented_buf = NULL;
	size_t		indented_len = 0;
	FILE	   *mem_in;
	FILE	   *mem_out;
	char	   *postprocessed;
	size_t		postprocessed_len;
	int			result;

	*out_buf = NULL;
	*out_len = 0;

	/* Read the source file */
	source = read_file_to_buffer(path, &source_len);
	if (source == NULL)
	{
		fprintf(stderr, "pgindent: cannot open \"%s\": %s\n",
				path, strerror(errno));
		return -1;
	}

	/* Step 1: Preprocess */
	preprocessed = preprocess(source, source_len, &preprocessed_len);
	if (preprocessed == NULL)
	{
		fprintf(stderr, "pgindent: preprocess failed for \"%s\"\n", path);
		free(source);
		return -1;
	}

	/* Step 2: Indent via fmemopen/open_memstream */
	mem_in = fmemopen(preprocessed, preprocessed_len, "r");
	if (mem_in == NULL)
	{
		fprintf(stderr, "pgindent: fmemopen failed for \"%s\": %s\n",
				path, strerror(errno));
		free(preprocessed);
		free(source);
		return -1;
	}
	mem_out = open_memstream(&indented_buf, &indented_len);
	if (mem_out == NULL)
	{
		fprintf(stderr, "pgindent: open_memstream failed for \"%s\": %s\n",
				path, strerror(errno));
		fclose(mem_in);
		free(preprocessed);
		free(source);
		return -1;
	}

	result = indent_file(mem_in, mem_out);
	fclose(mem_in);
	fclose(mem_out);
	free(preprocessed);

	if (result != 0 || indented_buf == NULL)
	{
		fprintf(stderr, "pgindent: indent failed for \"%s\"\n", path);
		free(indented_buf);
		free(source);
		return -1;
	}

	/* Step 3: Postprocess */
	postprocessed = postprocess(indented_buf, indented_len, &postprocessed_len);
	free(indented_buf);

	if (postprocessed == NULL)
	{
		fprintf(stderr, "pgindent: postprocess failed for \"%s\"\n", path);
		free(source);
		return -1;
	}

	/* Compare with original */
	if (source_len == postprocessed_len &&
		memcmp(source, postprocessed, source_len) == 0)
	{
		/* File unchanged */
		free(source);
		free(postprocessed);
		return 0;
	}

	/* File changed */
	free(source);
	*out_buf = postprocessed;
	*out_len = postprocessed_len;
	return 1;
}

/*
 * output_diff - produce unified diff output for a changed file.
 *
 * Writes the original file to a temp file, the new content to another temp
 * file, invokes "diff -upd" between them, and rewrites the header paths
 * to use a/path and b/path (git-style).  The diff output is written to
 * stdout.
 *
 * We use pipe-based sed substitution with '|' as the delimiter to avoid
 * issues with '/' in file paths.  The command buffer is dynamically
 * allocated to handle arbitrarily long paths.
 */
static void
output_diff(const char *path, const char *new_content, size_t new_len)
{
	char		tmp_orig[256];
	char		tmp_new[256];
	char	   *cmd;
	size_t		cmd_len;
	FILE	   *f;
	int			fd_orig;
	int			fd_new;
	ssize_t		written;
	size_t		total_written;

	snprintf(tmp_orig, sizeof(tmp_orig), "/tmp/pgindent_orig_XXXXXX");
	snprintf(tmp_new, sizeof(tmp_new), "/tmp/pgindent_new_XXXXXX");

	fd_orig = mkstemp(tmp_orig);
	fd_new = mkstemp(tmp_new);

	if (fd_orig < 0 || fd_new < 0)
	{
		fprintf(stderr, "pgindent: cannot create temp files: %s\n",
				strerror(errno));
		if (fd_orig >= 0)
		{
			close(fd_orig);
			unlink(tmp_orig);
		}
		if (fd_new >= 0)
		{
			close(fd_new);
			unlink(tmp_new);
		}
		return;
	}

	/* Copy original file to temp */
	f = fopen(path, "r");
	if (f != NULL)
	{
		char		buf[8192];
		size_t		n;

		while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		{
			total_written = 0;
			while (total_written < n)
			{
				written = write(fd_orig, buf + total_written,
								n - total_written);
				if (written < 0)
				{
					fprintf(stderr,
							"pgindent: write to temp file failed: %s\n",
							strerror(errno));
					fclose(f);
					close(fd_orig);
					close(fd_new);
					unlink(tmp_orig);
					unlink(tmp_new);
					return;
				}
				total_written += written;
			}
		}
		fclose(f);
	}
	else
	{
		fprintf(stderr, "pgindent: cannot open \"%s\" for diff: %s\n",
				path, strerror(errno));
		close(fd_orig);
		close(fd_new);
		unlink(tmp_orig);
		unlink(tmp_new);
		return;
	}
	close(fd_orig);

	/* Write new content to temp */
	total_written = 0;
	while (total_written < new_len)
	{
		written = write(fd_new, new_content + total_written,
						new_len - total_written);
		if (written < 0)
		{
			fprintf(stderr, "pgindent: write to temp file failed: %s\n",
					strerror(errno));
			close(fd_new);
			unlink(tmp_orig);
			unlink(tmp_new);
			return;
		}
		total_written += written;
	}
	close(fd_new);

	/*
	 * Run diff -upd and use sed to rewrite the temp file paths in the header
	 * to a/path and b/path format.  We use '|' as the sed delimiter to avoid
	 * conflicts with '/' in file paths.
	 */
	cmd_len = strlen(tmp_orig) + strlen(tmp_new) + strlen(path) * 2 + 256;
	cmd = malloc(cmd_len);
	if (cmd == NULL)
		err(1, NULL);

	snprintf(cmd, cmd_len,
			 "diff -upd %s %s | sed 's|%s|a/%s|;s|%s|b/%s|'",
			 tmp_orig, tmp_new, tmp_orig, path, tmp_new, path);
	(void) system(cmd);

	free(cmd);

	/* Clean up temp files */
	unlink(tmp_orig);
	unlink(tmp_new);
}

static void
usage(void)
{
	printf("pgindent (PostgreSQL) %s\n", INDENT_VERSION);
	printf("Usage: pgindent [OPTION]... [FILE|DIR]...\n\n");
	printf("Options:\n");
	printf("  --typedefs=FILE         load typedef names from FILE\n");
	printf("  --list-of-typedefs=STR  add typedef names (comma/space separated)\n");
	printf("  --excludes=FILE         exclude files matching patterns in FILE (repeatable)\n");
	printf("  --commit=REF            indent files changed in git commit REF (repeatable)\n");
	printf("  --diff                  output unified diff instead of modifying files\n");
	printf("  --check                 exit with status 2 if any file would change\n");
	printf("  --help                  show this help and exit\n");
	printf("  --version               show version and exit\n");
	printf("  -T NAME                 add NAME as a typedef\n");
	printf("  -U FILE                 load typedefs from FILE\n");
}

int
main(int argc, char **argv)
{
	/* Option variables */
	char	   *typedefs_file = NULL;
	char	   *typedef_str = NULL;
	StringArray excludes;
	StringArray commits;
	bool		diff_mode = false;
	bool		check_mode = false;
	int			c;
	int			optindex = 0;

	/* File processing state */
	FileList	files;
	int			i;
	int			exit_status = EXIT_SUCCESS_STATUS;
	bool		any_changed = false;
	bool		any_failed = false;

	static struct option long_options[] = {
		{"typedefs", required_argument, NULL, 1},
		{"list-of-typedefs", required_argument, NULL, 2},
		{"excludes", required_argument, NULL, 3},
		{"commit", required_argument, NULL, 4},
		{"diff", no_argument, NULL, 5},
		{"check", no_argument, NULL, 6},
		{"help", no_argument, NULL, 7},
		{"version", no_argument, NULL, 8},
		{NULL, 0, NULL, 0}
	};

	strarray_init(&excludes);
	strarray_init(&commits);

	/* Set up signal handlers for clean interruption */
	setup_signal_handlers();

	/* Initialize the indent engine's typename storage */
	alloc_typenames();

	/* Parse command-line options */
	while ((c = getopt_long(argc, argv, "T:U:", long_options, &optindex)) != -1)
	{
		switch (c)
		{
			case 1:				/* --typedefs */
				typedefs_file = optarg;
				break;
			case 2:				/* --list-of-typedefs */
				typedef_str = optarg;
				break;
			case 3:				/* --excludes */
				strarray_add(&excludes, optarg);
				break;
			case 4:				/* --commit */
				strarray_add(&commits, optarg);
				break;
			case 5:				/* --diff */
				diff_mode = true;
				break;
			case 6:				/* --check */
				check_mode = true;
				break;
			case 7:				/* --help */
				usage();
				exit(0);
			case 8:				/* --version */
				printf("pgindent (PostgreSQL) %s\n", INDENT_VERSION);
				exit(0);
			case 'T':			/* -T NAME */
				add_typename(optarg);
				break;
			case 'U':			/* -U FILE */
				add_typedefs_from_file(optarg);
				break;
			default:
				/* getopt_long already printed an error */
				fprintf(stderr, "Try \"pgindent --help\" for more information.\n");
				exit(EXIT_USAGE_ERROR);
		}
	}

	/* Validate: --commit + file/dir args is an error */
	if (commits.count > 0 && optind < argc)
	{
		fprintf(stderr, "pgindent: cannot specify both --commit and file arguments\n");
		exit(EXIT_USAGE_ERROR);
	}

	/* Load typedefs */
	load_typedefs(typedefs_file, typedef_str);

	/*
	 * Auto-detect PostgreSQL source tree and load default exclude file. We
	 * check if the well-known exclude file exists relative to cwd.
	 */
	if (access(PG_EXCLUDE_FILE, R_OK) == 0)
	{
		/* Add it only if not already specified by the user */
		bool		already_specified = false;

		for (i = 0; i < excludes.count; i++)
		{
			if (strcmp(excludes.items[i], PG_EXCLUDE_FILE) == 0)
			{
				already_specified = true;
				break;
			}
		}
		if (!already_specified)
			strarray_add(&excludes, PG_EXCLUDE_FILE);
	}

	/* Discover files */
	filelist_init(&files);

	if (commits.count > 0)
	{
		/* Commit mode: get files from git */
		discover_files_from_commits(&files, commits.items, commits.count);
	}
	else if (optind < argc)
	{
		/* File/directory arguments */
		discover_files_from_paths(&files, &argv[optind], argc - optind);
	}
	else
	{
		/* No files specified and no --commit */
		fprintf(stderr, "pgindent: no files specified\n");
		/* Not a fatal error - just warn and exit successfully */
	}

	/* Apply exclude patterns */
	if (excludes.count > 0 && files.count > 0)
		apply_exclude_patterns(&files, excludes.items, excludes.count);

	/* Deduplicate files */
	if (files.count > 0)
		deduplicate_files(&files);

	/* Process each file */
	for (i = 0; i < files.count; i++)
	{
		const char *path = files.files[i];
		char	   *new_content = NULL;
		size_t		new_len = 0;
		int			result;

		/* Check for interruption between files */
		if (interrupted)
		{
			fprintf(stderr, "pgindent: interrupted, exiting\n");
			break;
		}

		/* Skip derived files (.y/.l siblings) */
		if (is_derived_file(path))
			continue;

		/* Run the pipeline */
		result = process_file_full(path, &new_content, &new_len);

		if (result < 0)
		{
			/* Indent failure - continue with next file */
			any_failed = true;
			continue;
		}

		if (result == 0)
		{
			/* File unchanged - nothing to do */
			continue;
		}

		/* File changed */
		any_changed = true;

		if (diff_mode)
		{
			/* Output unified diff */
			output_diff(path, new_content, new_len);
		}

		if (!diff_mode && !check_mode)
		{
			/* Normal mode: write the file back */
			if (write_file_from_buffer(path, new_content, new_len) != 0)
			{
				fprintf(stderr, "pgindent: cannot write \"%s\": %s\n",
						path, strerror(errno));
				free(new_content);
				filelist_free(&files);
				strarray_free(&excludes);
				strarray_free(&commits);
				exit(EXIT_USAGE_ERROR);
			}
		}

		free(new_content);
	}

	/* Determine exit status */
	if (any_failed)
		exit_status = EXIT_INDENT_FAILURE;
	else if (check_mode && any_changed)
		exit_status = EXIT_CHECK_CHANGED;

	/* Clean up */
	filelist_free(&files);
	strarray_free(&excludes);
	strarray_free(&commits);

	return exit_status;
}
