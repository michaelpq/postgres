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
#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"

/* This is private in commands/copyfrom.c */
typedef struct CopyFromStateData *CopyFromState;

/* Routines for a COPY FROM format implementation. */
typedef struct CopyFromRoutine
{
	/*
	 * Called for processing one COPY FROM option. This will return false when
	 * the given option is invalid.
	 */
	bool		(*CopyFromProcessOption) (CopyFromState cstate, DefElem *defel);

	/*
	 * Called when COPY FROM is started. This will return a format as int16
	 * value. It's used for the CopyInResponse message.
	 */
	int16		(*CopyFromGetFormat) (CopyFromState cstate);

	/*
	 * Called when COPY FROM is started. This will initialize something and
	 * receive a header.
	 */
	void		(*CopyFromStart) (CopyFromState cstate, TupleDesc tupDesc);

	/* Copy one row. It returns false if no more tuples. */
	bool		(*CopyFromOneRow) (CopyFromState cstate, ExprContext *econtext, Datum *values, bool *nulls);

	/* Called when COPY FROM is ended. This will finalize something. */
	void		(*CopyFromEnd) (CopyFromState cstate);
}			CopyFromRoutine;

/* This is private in commands/copyto.c */
typedef struct CopyToStateData *CopyToState;

/* Routines for a COPY TO format implementation. */
typedef struct CopyToRoutine
{
	/*
	 * Called for processing one COPY TO option. This will return false when
	 * the given option is invalid.
	 */
	bool		(*CopyToProcessOption) (CopyToState cstate, DefElem *defel);

	/*
	 * Called when COPY TO is started. This will return a format as int16
	 * value. It's used for the CopyOutResponse message.
	 */
	int16		(*CopyToGetFormat) (CopyToState cstate);

	/* Called when COPY TO is started. This will send a header. */
	void		(*CopyToStart) (CopyToState cstate, TupleDesc tupDesc);

	/* Copy one row for COPY TO. */
	void		(*CopyToOneRow) (CopyToState cstate, TupleTableSlot *slot);

	/* Called when COPY TO is ended. This will send a trailer. */
	void		(*CopyToEnd) (CopyToState cstate);
}			CopyToRoutine;

#endif							/* COPYAPI_H */
