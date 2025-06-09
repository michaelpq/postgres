/*-------------------------------------------------------------------------
 *
 * toast_counter.h
 *	  Machinery for TOAST value counter.
 *
 * Copyright (c) 2000-2025, PostgreSQL Global Development Group
 *
 * src/include/access/toast_counter.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef TOAST_COUNTER_H
#define TOAST_COUNTER_H

#define InvalidToastId	0	/* Invalid TOAST value ID */
#define FirstToastId	1	/* First TOAST value ID assigned */

/*
 * Structure in shared memory to track TOAST value counter activity.
 * These are protected by ToastIdGenLock.
 */
typedef struct ToastCounterData
{
	uint64		nextId;			/* next TOAST value ID to assign */
	uint32		idCount;		/* IDs available before WAL work */
} ToastCounterData;

extern PGDLLIMPORT ToastCounterData *ToastCounter;

/* external declarations */
extern Size ToastCounterShmemSize(void);
extern void ToastCounterShmemInit(void);
extern uint64 GetNewToastId(void);

#endif							/* TOAST_TYPE_H */
