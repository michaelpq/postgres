/*
 * postprocess.c - Post-indentation source transforms for pgindent.
 *
 * These transforms reverse the pre-processing protections and fix known
 * formatting artifacts from the indentation engine.
 *
 * Copyright (c) 2024-2026, PostgreSQL Global Development Group
 */
#include "c.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pgindent.h"

/*
 * postprocess - apply post-indentation transforms to source code.
 *
 * Transforms applied:
 * 1. Restore CATALOG(...) lines (remove protective comment markers)
 * 2. Restore extern "C" braces (replace sentinels with { and })
 * 3. Restore dash-protected block comments
 * 4. Fix run-together comments (insert tab between them)
 * 5. Normalize function return type pointer spacing
 *
 * Returns a malloc'd buffer that the caller must free.
 * *out_len is set to the length of the result.
 */
char *
postprocess(const char *source, size_t len, size_t *out_len)
{
	char	   *result;
	size_t		result_size;
	size_t		result_len;
	const char *s;
	const char *end;

	/* Allocate result buffer with some headroom */
	result_size = len + 256;
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

	while (s < end)
	{
		/* Find the current line */
		const char *line_start;
		const char *line_end;
		size_t		line_len;

		line_start = s;
		line_end = memchr(s, '\n', end - s);

		if (line_end == NULL)
			line_end = end;
		else
			line_end++;		/* include the newline */

		line_len = line_end - line_start;

		/*
		 * Transform 1: Restore CATALOG lines.
		 * Removes the comment wrapper added by preprocess.
		 */
		if (line_len >= 12 && line_start[0] == '/' && line_start[1] == '*' &&
			strncmp(line_start + 2, "CATALOG(", 8) == 0)
		{
			/* Find the closing comment delimiter at end of line */
			const char *close = line_end;

			if (close > line_start && *(close - 1) == '\n')
				close--;
			if (close >= line_start + 4 &&
				*(close - 1) == '/' && *(close - 2) == '*')
			{
				/* Copy content between the comment delimiters */
				const char *cstart = line_start + 2;
				const char *cend = close - 2;

				for (const char *p = cstart; p < cend; p++)
					APPEND_CHAR(*p);
				APPEND_CHAR('\n');
				s = line_end;
				continue;
			}
		}

		/*
		 * Transform 2: Restore extern "C" braces.
		 */
		{
			const char *p = line_start;

			/* Skip leading whitespace for matching */
			while (p < line_end && (*p == ' ' || *p == '\t'))
				p++;

			if ((line_end - p) >= 21 &&
				strncmp(p, "/* Open extern \"C\" */", 21) == 0)
			{
				APPEND_STR("{\n");
				s = line_end;
				continue;
			}
			if ((line_end - p) >= 22 &&
				strncmp(p, "/* Close extern \"C\" */", 22) == 0)
			{
				APPEND_STR("}\n");
				s = line_end;
				continue;
			}
		}

		/*
		 * Process the line character by character for transforms 3, 4, 5.
		 */
		for (const char *p = line_start; p < line_end; p++)
		{
			/*
			 * Transform 3: Restore dash-protected block comments.
			 * Reverses the X_X sentinel added by preprocess.
			 */
			if (*p == '/' && (p + 7) < line_end &&
				strncmp(p, "/*---X_X", 8) == 0)
			{
				APPEND_CHAR('/');
				APPEND_CHAR('*');
				APPEND_CHAR(' ');
				APPEND_CHAR('-');
				APPEND_CHAR('-');
				APPEND_CHAR('-');
				p += 7;		/* loop will advance past last char */
				continue;
			}

			/*
			 * Transform 4: Fix run-together comments.
			 * When a closing comment delimiter is immediately followed
			 * by an opening one at end of line, insert a tab.
			 */
			if (*p == '*' && (p + 1) < line_end && *(p + 1) == '/' &&
				(p + 2) < line_end && *(p + 2) == '/' && (p + 3) < line_end && *(p + 3) == '*')
			{
				/* Check if the second comment closes before end of line */
				const char *r = p + 4;

				while (r < line_end && *r != '\n')
				{
					if (*r == '*' && (r + 1) < line_end && *(r + 1) == '/')
					{
						/* Check this second comment end is at end of line */
						const char *t = r + 2;

						while (t < line_end && (*t == ' ' || *t == '\t'))
							t++;
						if (t == line_end || *t == '\n')
						{
							/* Insert tab between the two comments */
							APPEND_CHAR('*');
							APPEND_CHAR('/');
							APPEND_CHAR('\t');
							p += 1;	/* skip past the '/' of first close */
							goto done_char;
						}
						break;
					}
					r++;
				}
			}

			/*
			 * Transform 5: Normalize function return type pointer spacing.
			 * At start of line: "TypeName   *\n" -> "TypeName *\n"
			 */
			if (p == line_start &&
				(isalpha((unsigned char) *p) || *p == '_'))
			{
				/* Check if this line matches the pattern */
				const char *q = p;

				/* Skip the identifier (non-whitespace) */
				while (q < line_end && *q != ' ' && *q != '\t' && *q != '\n')
					q++;
				if (q < line_end && (*q == ' ' || *q == '\t'))
				{
					/* Skip whitespace */
					const char *ws_start = q;

					while (q < line_end && (*q == ' ' || *q == '\t'))
						q++;
					/* Check if remainder is just "*" followed by newline/end */
					if (q < line_end && *q == '*')
					{
						const char *after_star = q + 1;

						if (after_star == line_end ||
							*after_star == '\n')
						{
							/* Match! Output "typename *\n" */
							for (const char *r = p; r < ws_start; r++)
								APPEND_CHAR(*r);
							APPEND_CHAR(' ');
							APPEND_CHAR('*');
							if (after_star < line_end && *after_star == '\n')
								APPEND_CHAR('\n');
							s = line_end;
							goto next_line;
						}
					}
				}
			}

			APPEND_CHAR(*p);
done_char:
			;
		}

next_line:
		s = line_end;
	}

	result[result_len] = '\0';
	*out_len = result_len;

#undef APPEND_CHAR
#undef APPEND_STR

	return result;
}
