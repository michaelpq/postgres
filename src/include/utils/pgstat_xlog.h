/* ----------
 * pgstat_xlog.h
 *	  Exports from utils/activity/pgstat_xlog.c
 *
 * Copyright (c) 2001-2025, PostgreSQL Global Development Group
 *
 * src/include/utils/pgstat_xlog.h
 * ----------
 */
#ifndef PGSTAT_XLOG_H
#define PGSTAT_XLOG_H

#include "access/xlog.h"
#include "access/xlogdefs.h"
#include "access/xlogreader.h"
#include "utils/pgstat_kind.h"

/*
 * Generic WAL record for pgstat data.
 */
typedef struct xl_pgstat_data
{
	PgStat_Kind stats_kind;
	size_t		data_size;		/* size of the data */
	/* stats data, worth data_size */
	char		data[FLEXIBLE_ARRAY_MEMBER];
} xl_pgstat_data;

#define SizeOfPgStatData    (offsetof(xl_pgstat_data, data))

extern XLogRecPtr pgstat_xlog_data(PgStat_Kind stats_kind,
								   const void *data,
								   size_t data_size);

/* RMGR API */
#define XLOG_PGSTAT_DATA	0x00
extern void pgstat_redo(XLogReaderState *record);
extern void pgstat_desc(StringInfo buf, XLogReaderState *record);
extern const char *pgstat_identify(uint8 info);

#endif							/* PGSTAT_XLOG_H */
