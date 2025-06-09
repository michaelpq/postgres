/*-------------------------------------------------------------------------
 *
 * toast_counter.c
 *	  Functions for TOAST value counter.
 *
 * Copyright (c) 2025, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/common/toast_counter.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/toast_counter.h"
#include "access/xlog.h"
#include "miscadmin.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"

/* Number of TOAST values to preallocate before WAL work */
#define TOAST_ID_PREFETCH		8192

/* pointer to variables struct in shared memory */
ToastCounterData *ToastCounter = NULL;

/*
 * Initialization of shared memory for ToastCounter.
 */
Size
ToastCounterShmemSize(void)
{
	return sizeof(ToastCounterData);
}

void
ToastCounterShmemInit(void)
{
	bool		found;

	/* Initialize shared state struct */
	ToastCounter = ShmemInitStruct("ToastCounter",
									   sizeof(ToastCounterData),
									   &found);
	if (!IsUnderPostmaster)
	{
		Assert(!found);
		memset(ToastCounter, 0, sizeof(ToastCounterData));
	}
	else
		Assert(found);
}

/*
 * GetNewToastId
 *
 * Toast IDs are generated as a cluster-wide counter.  They are 64 bits
 * wide, hence wraparound will unlikely happen.
 */
uint64
GetNewToastId(void)
{
	uint64		result;

	if (RecoveryInProgress())
		elog(ERROR, "cannot assign TOAST IDs during recovery");

	LWLockAcquire(ToastIdGenLock, LW_EXCLUSIVE);

	/*
	 * Check for initialization or wraparound of the toast counter ID.
	 * InvalidToastId (0) should never be returned.  We are 64 bit-wide,
	 * hence wraparound is unlikely going to happen, but this check is
	 * cheap so let's play it safe.
	 */
	if (ToastCounter->nextId < ((uint64) FirstToastId))
	{
		/* Most-likely first bootstrap or initdb assignment */
		ToastCounter->nextId = FirstToastId;
		ToastCounter->idCount = 0;
	}

	/* If running out of logged for TOAST IDs, log more */
	if (ToastCounter->idCount == 0)
	{
		XLogPutNextToastId(ToastCounter->nextId + TOAST_ID_PREFETCH);
		ToastCounter->idCount = TOAST_ID_PREFETCH;
	}

	result = ToastCounter->nextId;
	(ToastCounter->nextId)++;
	(ToastCounter->idCount)--;

	LWLockRelease(ToastIdGenLock);

	return result;
}
