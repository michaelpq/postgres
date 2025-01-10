/* ----------
 * pgstat_xlog.c
 *	   WAL replay logic for cumulative statistics.
 *
 *
 * Copyright (c) 2001-2025, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/utils/activity/pgstat_xlog.c
 * ----------
 */

#include "postgres.h"

#include <unistd.h>

#include "access/xlog.h"
#include "access/xloginsert.h"
#include "utils/pgstat_internal.h"
#include "utils/pgstat_xlog.h"

/*
 * Write pgstats record data into WAL.
 */
XLogRecPtr
pgstat_xlog_data(PgStat_Kind stats_kind, const void *data,
				 size_t data_size)
{
	xl_pgstat_data xlrec;
	XLogRecPtr	lsn;

	xlrec.stats_kind = stats_kind;
	xlrec.data_size = data_size;

	XLogBeginInsert();
	XLogRegisterData((char *) &xlrec, SizeOfPgStatData);
	XLogRegisterData(data, data_size);

	lsn = XLogInsert(RM_PGSTAT_ID, XLOG_PGSTAT_DATA);
	return lsn;
}

/*
 * Redo just passes down to the stats kind expected by this record
 * data included in it.
 */
void
pgstat_redo(XLogReaderState *record)
{
	uint8		info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;
	xl_pgstat_data *xlrec;
	PgStat_Kind stats_kind;
	const PgStat_KindInfo *kind_info;

	if (info != XLOG_PGSTAT_DATA)
		elog(PANIC, "pgstat_redo: unknown op code %u", info);

	xlrec = (xl_pgstat_data *) XLogRecGetData(record);

	stats_kind = xlrec->stats_kind;
	kind_info = pgstat_get_kind_info(stats_kind);

	if (kind_info == NULL)
		elog(FATAL, "pgstat_redo: invalid stats kind found: kind=%u",
			 stats_kind);

	if (kind_info->redo_cb == NULL)
		elog(FATAL, "pgstat_redo: no redo callback found: kind=%s",
			 kind_info->name);

	kind_info->redo_cb(xlrec->data, xlrec->data_size);
}
