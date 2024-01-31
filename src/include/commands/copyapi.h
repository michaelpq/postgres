/*-------------------------------------------------------------------------
 *
 * copyapi.h
 *	  API for COPY TO/FROM handlers
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/copyapi.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef COPYAPI_H
#define COPYAPI_H

#include "executor/tuptable.h"

/* This is private in commands/copyto.c */
typedef struct CopyToStateData *CopyToState;

/* Routines for a COPY TO format implementation. */
typedef struct CopyToRoutine
{
	/* Called when COPY TO is started. This will send a header. */
	void		(*CopyToStart) (CopyToState cstate, TupleDesc tupDesc);

	/* Copy one row for COPY TO. */
	void		(*CopyToOneRow) (CopyToState cstate, TupleTableSlot *slot);

	/* Called when COPY TO is ended. This will send a trailer. */
	void		(*CopyToEnd) (CopyToState cstate);
}			CopyToRoutine;

#endif							/* COPYAPI_H */
