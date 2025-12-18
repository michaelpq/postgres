/*-------------------------------------------------------------------------
 *
 * seqlocalam.h
 *	  Local sequence access method.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/seqlocalam.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SEQLOCALAM_H
#define SEQLOCALAM_H

#include "utils/rel.h"

/* access routines */
extern int64 seq_local_nextval(Relation rel, int64 incby, int64 maxv,
							   int64 minv, int64 cache, bool cycle,
							   int64 *last);
extern const char *seq_local_get_table_am(void);
extern void seq_local_init(Relation rel, int64 last_value, bool is_called);
extern void seq_local_setval(Relation rel, int64 next, bool iscalled);
extern void seq_local_reset(Relation rel, int64 startv, bool is_called,
							bool reset_state);
extern void seq_local_get_state(Relation rel, int64 *last_value,
								bool *is_called, XLogRecPtr *page_lsn);
extern void seq_local_change_persistence(Relation rel,
										 char newrelpersistence);

#endif							/* SEQLOCALAM_H */
