/*
 * pgindent.h - declarations for the pgindent consolidated tool.
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */
#ifndef PGINDENT_H
#define PGINDENT_H

#include <stddef.h>

/* preprocess.c */
extern char *preprocess(const char *source, size_t len, size_t *out_len);

/* postprocess.c */
extern char *postprocess(const char *source, size_t len, size_t *out_len);

#endif							/* PGINDENT_H */
