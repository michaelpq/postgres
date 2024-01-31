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

/* private in commands/copyfrom.c */
typedef struct CopyFromStateData *CopyFromState;
/* private in commands/copyto.c */
typedef struct CopyToStateData *CopyToState;

/* Routines for a COPY FROM format implementation. */
typedef struct CopyFromRoutine
{
	/*
	 * Called when COPY FROM is started.
	 * //document tupDesc.
	 */
	void		(*CopyFromStart) (CopyFromState cstate, TupleDesc tupDesc);

	/* Copy one row.   Returns false if no more tuples.
	   //document arguments..
	 */
	bool		(*CopyFromOneRow) (CopyFromState cstate, ExprContext *econtext,
								   Datum *values, bool *nulls);

	/* Called when COPY FROM has ended.  This will finalize something. */
	void		(*CopyFromEnd) (CopyFromState cstate);
} CopyFromRoutine;


/* Routines for a COPY TO format implementation. */
typedef struct CopyToRoutine
{
	/*
	 * Called when COPY TO is started.
	 * This will send a header.
	 * //document arguments
	 */
	void		(*CopyToStart) (CopyToState cstate, TupleDesc tupDesc);

	/* Copy one row for COPY TO. */
	void		(*CopyToOneRow) (CopyToState cstate, TupleTableSlot *slot);

	/* Called when COPY TO is ended.  This will send a trailer. */
	void		(*CopyToEnd) (CopyToState cstate);
} CopyToRoutine;

#endif							/* COPYAPI_H */
