/*-------------------------------------------------------------------------
 *
 * localam.h
 *	  Local sequence access method.
 *
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/localam.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef LOCALAM_H
#define LOCALAM_H

#include "access/xlogreader.h"
#include "storage/relfilelocator.h"
#include "utils/rel.h"

/* XLOG stuff */
#define XLOG_LOCAL_SEQ_LOG			0x00

typedef struct xl_local_seq_rec
{
	RelFileLocator locator;
	/* SEQUENCE TUPLE DATA FOLLOWS AT THE END */
} xl_local_seq_rec;

extern void local_seq_redo(XLogReaderState *record);
extern void local_seq_desc(StringInfo buf, XLogReaderState *record);
extern const char *local_seq_identify(uint8 info);
extern void local_seq_mask(char *page, BlockNumber blkno);

/* access routines */
extern int64 local_seq_nextval(Relation rel, int64 incby, int64 maxv,
							   int64 minv, int64 cache, bool cycle,
							   int64 *last);
extern const char *local_seq_get_table_am(void);
extern void local_seq_init(Relation rel, int64 last_value, bool is_called);
extern void local_seq_setval(Relation rel, int64 next, bool iscalled);
extern void local_seq_reset(Relation rel, int64 startv, bool is_called,
							bool reset_state);
extern void local_seq_get_state(Relation rel, int64 *last_value,
								bool *is_called);
extern void local_seq_change_persistence(Relation rel,
										 char newrelpersistence);

#endif							/* LOCALAM_H */
