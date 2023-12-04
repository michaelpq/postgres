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
#include "nodes/parsenodes.h"

/* This is private in commands/copyto.c */
typedef struct CopyToStateData *CopyToState;

typedef bool (*CopyToProcessOption_function) (CopyToState cstate, DefElem *defel);
typedef int16 (*CopyToGetFormat_function) (CopyToState cstate);
typedef void (*CopyToStart_function) (CopyToState cstate, TupleDesc tupDesc);
typedef void (*CopyToOneRow_function) (CopyToState cstate, TupleTableSlot *slot);
typedef void (*CopyToEnd_function) (CopyToState cstate);

/* Routines for a COPY TO format implementation. */
typedef struct CopyToRoutine
{
	/*
	 * Called for processing one COPY TO option. This will return false when
	 * the given option is invalid.
	 */
	CopyToProcessOption_function CopyToProcessOption;

	/*
	 * Called when COPY TO is started. This will return a format as int16
	 * value. It's used for the CopyOutResponse message.
	 */
	CopyToGetFormat_function CopyToGetFormat;

	/* Called when COPY TO is started. This will send a header. */
	CopyToStart_function CopyToStart;

	/* Copy one row for COPY TO. */
	CopyToOneRow_function CopyToOneRow;

	/* Called when COPY TO is ended. This will send a trailer. */
	CopyToEnd_function CopyToEnd;
}			CopyToRoutine;

/* Built-in CopyToRoutine for "text", "csv" and "binary". */
extern CopyToRoutine CopyToRoutineText;
extern CopyToRoutine CopyToRoutineCSV;
extern CopyToRoutine CopyToRoutineBinary;

#endif							/* COPYAPI_H */
