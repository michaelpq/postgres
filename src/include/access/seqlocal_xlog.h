/*-------------------------------------------------------------------------
 *
 * seqlocal_xlog.h
 *	  Local sequence WAL definitions.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/seqlocal_xlog.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef SEQLOCAL_XLOG_H
#define SEQLOCAL_XLOG_H

#include "access/xlogreader.h"
#include "lib/stringinfo.h"

/* Record identifier */
#define XLOG_SEQ_LOCAL_LOG			0x00

/*
 * The "special area" of a local sequence's buffer page looks like this.
 */
#define SEQ_LOCAL_MAGIC	  0x1717

typedef struct seq_local_magic
{
	uint32		magic;
} seq_local_magic;

/* Sequence WAL record */
typedef struct xl_seq_local_rec
{
	RelFileLocator locator;
	/* SEQUENCE TUPLE DATA FOLLOWS AT THE END */
} xl_seq_local_rec;

extern void seq_local_redo(XLogReaderState *record);
extern void seq_local_desc(StringInfo buf, XLogReaderState *record);
extern const char *seq_local_identify(uint8 info);
extern void seq_local_mask(char *page, BlockNumber blkno);

#endif							/* SEQLOCAL_XLOG_H */
