/*
 * pgindent.h - declarations for the pgindent consolidated tool.
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */
#ifndef PGINDENT_H
#define PGINDENT_H

#include <stdbool.h>
#include <stddef.h>

/* preprocess.c */
extern char *preprocess(const char *source, size_t len, size_t *out_len);

/* postprocess.c */
extern char *postprocess(const char *source, size_t len, size_t *out_len);

/* typedefs.c */
extern void load_typedefs(const char *file, const char *extra_str);

/* discover.c */
typedef struct FileList
{
	char	  **files;
	int			count;
	int			capacity;
}			FileList;

extern void filelist_init(FileList * fl);
extern void filelist_free(FileList * fl);
extern void discover_files_from_paths(FileList * fl, char **paths, int npaths);
extern void discover_files_from_commits(FileList * fl, char **commits, int ncommits);

/* filter.c */
extern void apply_exclude_patterns(FileList * fl, char **exclude_files, int nexcludes);
extern bool is_derived_file(const char *path);
extern void deduplicate_files(FileList * fl);

#endif							/* PGINDENT_H */
