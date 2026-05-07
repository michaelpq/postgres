/*
 * preprocess.c - Pre-indentation source transforms for pgindent.
 *
 * These transforms protect special constructs from being mangled by the
 * indentation engine, matching the behavior of the Perl pgindent script.
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */
#include "c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pgindent.h"

/*
 * preprocess - apply pre-indentation transforms to source code.
 *
 * Transforms applied:
 * 1. Ensure trailing newline
 * 2. Convert C++ style comments to C style comments
 * 3. Protect dash-block comments with sentinel
 * 4. Protect extern "C" braces with comment sentinels
 * 5. Protect CATALOG(...) lines with comment markers
 *
 * Returns a malloc'd buffer that the caller must free.
 * *out_len is set to the length of the result.
 */
char *
preprocess(const char *source, size_t len, size_t *out_len)
{
	char	   *result;
	size_t		result_size;
	size_t		result_len;
	const char *s;
	const char *end;
	const char *line_start;
	int			at_line_start;
	int			prev_was_extern_c;

	/* Allocate result buffer with some headroom */
	result_size = len * 2 + 64;
	result = malloc(result_size);
	if (result == NULL)
		return NULL;
	result_len = 0;

#define APPEND_CHAR(c) do { \
	if (result_len >= result_size - 1) { \
		result_size *= 2; \
		result = realloc(result, result_size); \
		if (result == NULL) return NULL; \
	} \
	result[result_len++] = (c); \
} while (0)

#define APPEND_STR(str) do { \
	const char *_s = (str); \
	while (*_s) APPEND_CHAR(*_s++); \
} while (0)

	end = source + len;
	s = source;
	prev_was_extern_c = 0;

	while (s < end)
	{
		const char *line_end;
		size_t		line_len;

		/* Find the current line */
		line_start = s;
		line_end = memchr(s, '\n', end - s);

		if (line_end == NULL)
			line_end = end;
		else
			line_end++;		/* include the newline */

		line_len = line_end - line_start;

		/*
		 * Transform 5: Protect CATALOG(...) lines.
		 * Pattern: ^CATALOG(...)$ -> wraps in comment markers
		 */
		if (line_len >= 8 && strncmp(line_start, "CATALOG(", 8) == 0)
		{
			APPEND_STR("/*");
			for (const char *p = line_start; p < line_end; p++)
				APPEND_CHAR(*p);
			/* If line didn't end with newline, close comment before it */
			if (result_len > 0 && result[result_len - 1] == '\n')
			{
				result_len--;	/* back up over newline */
				APPEND_STR("*/\n");
			}
			else
				APPEND_STR("*/");
			s = line_end;
			prev_was_extern_c = 0;
			continue;
		}

		/*
		 * Transform 4: Protect extern "C" braces.
		 *
		 * Pattern for opening brace:
		 *   #ifdef __cplusplus\nextern "C"\n{ -> replace { with sentinel
		 *
		 * Pattern for closing brace:
		 *   #ifdef __cplusplus\n} -> replace } with sentinel
		 *
		 * We track whether the previous line was "extern "C"" preceded by
		 * #ifdef __cplusplus, and if the current line is just "{", replace it.
		 */
		if (prev_was_extern_c == 2)
		{
			/* Previous lines were #ifdef __cplusplus + extern "C" */
			/* Check if this line is just "{" (possibly with whitespace) */
			const char *p = line_start;

			while (p < line_end && (*p == ' ' || *p == '\t'))
				p++;
			if (p < line_end && *p == '{')
			{
				p++;
				while (p < line_end && (*p == ' ' || *p == '\t'))
					p++;
				if (p == line_end || *p == '\n')
				{
					APPEND_STR("/* Open extern \"C\" */\n");
					s = line_end;
					prev_was_extern_c = 0;
					continue;
				}
			}
			prev_was_extern_c = 0;
		}

		/*
		 * Check for closing brace after #ifdef __cplusplus (for the
		 * closing of extern "C" block).
		 */
		if (prev_was_extern_c == 3)
		{
			const char *p = line_start;

			while (p < line_end && (*p == ' ' || *p == '\t'))
				p++;
			if (p < line_end && *p == '}')
			{
				p++;
				while (p < line_end && (*p == ' ' || *p == '\t'))
					p++;
				if (p == line_end || *p == '\n')
				{
					APPEND_STR("/* Close extern \"C\" */\n");
					s = line_end;
					prev_was_extern_c = 0;
					continue;
				}
			}
			prev_was_extern_c = 0;
		}

		/* Detect #ifdef __cplusplus lines */
		if (line_len >= 18 && strncmp(line_start, "#ifdef", 6) == 0)
		{
			const char *p = line_start + 6;

			while (p < line_end && (*p == ' ' || *p == '\t'))
				p++;
			if (strncmp(p, "__cplusplus", 11) == 0)
			{
				/* Check if next line is extern "C" or just } */
				/* Copy this line as-is and set state */
				for (const char *q = line_start; q < line_end; q++)
					APPEND_CHAR(*q);
				s = line_end;
				prev_was_extern_c = 1;
				continue;
			}
		}

		/* Detect extern "C" line after #ifdef __cplusplus */
		if (prev_was_extern_c == 1)
		{
			const char *p = line_start;

			while (p < line_end && (*p == ' ' || *p == '\t'))
				p++;
			if (strncmp(p, "extern", 6) == 0)
			{
				p += 6;
				while (p < line_end && (*p == ' ' || *p == '\t'))
					p++;
				if (*p == '"' && *(p + 1) == 'C' && *(p + 2) == '"')
				{
					for (const char *q = line_start; q < line_end; q++)
						APPEND_CHAR(*q);
					s = line_end;
					prev_was_extern_c = 2;
					continue;
				}
			}
			/* Not extern "C", check if it's a closing } */
			p = line_start;
			while (p < line_end && (*p == ' ' || *p == '\t'))
				p++;
			if (*p == '}')
			{
				/* This is the closing brace pattern */
				prev_was_extern_c = 3;
				/* Re-process this line with state 3 */
				/* Actually we need to handle it now */
				p++;
				while (p < line_end && (*p == ' ' || *p == '\t'))
					p++;
				if (p == line_end || *p == '\n')
				{
					APPEND_STR("/* Close extern \"C\" */\n");
					s = line_end;
					prev_was_extern_c = 0;
					continue;
				}
			}
			prev_was_extern_c = 0;
		}

		/*
		 * Process the line character by character for transforms 2 and 3.
		 */
		at_line_start = 1;
		for (const char *p = line_start; p < line_end; p++)
		{
			/*
			 * Transform 3: Protect dash-block comments.
			 * Converts the "slash-star space+ dashes" pattern to use
			 * the X_X sentinel so indent won't reflow them.
			 */
			if (*p == '/' && (p + 1) < line_end && *(p + 1) == '*')
			{
				const char *q = p + 2;

				/* Check for " +---" after the opening comment */
				if (q < line_end && *q == ' ')
				{
					while (q < line_end && *q == ' ')
						q++;
					if ((q + 2) < line_end && q[0] == '-' && q[1] == '-' && q[2] == '-')
					{
						APPEND_STR("/*---X_X");
						p = q + 2;	/* skip past "---", loop will advance past last '-' */
						at_line_start = 0;
						continue;
					}
				}
			}

			/*
			 * Transform 2: Convert C++ style comments to C style.
			 * Only at the start of meaningful content on a line (after
			 * optional whitespace).
			 */
			if (*p == '/' && (p + 1) < line_end && *(p + 1) == '/' && at_line_start)
			{
				/* Convert the C++ comment to C style */
				APPEND_STR("/* ");
				p += 2;		/* skip the double-slash */
				/* Copy rest of line (excluding newline) */
				while (p < line_end && *p != '\n')
				{
					APPEND_CHAR(*p);
					p++;
				}
				APPEND_STR(" */");
				if (p < line_end && *p == '\n')
					APPEND_CHAR('\n');
				goto next_line;
			}

			if (*p != ' ' && *p != '\t')
				at_line_start = 0;

			APPEND_CHAR(*p);
		}

next_line:
		s = line_end;
	}

	/* Transform 1: Ensure trailing newline */
	if (result_len == 0 || result[result_len - 1] != '\n')
		APPEND_CHAR('\n');

	result[result_len] = '\0';
	*out_len = result_len;

#undef APPEND_CHAR
#undef APPEND_STR

	return result;
}
