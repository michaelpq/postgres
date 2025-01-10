/*-------------------------------------------------------------------------
 *
 * pgstatdesc.c
 *	  rmgr descriptor routines for utils/activity/pgstat_xlog.c
 *
 * Portions Copyright (c) 2001-2025, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/rmgrdesc/pgstatdesc.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "utils/pgstat_xlog.h"

void
pgstat_desc(StringInfo buf, XLogReaderState *record)
{
	char	   *rec = XLogRecGetData(record);
	uint8		info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;

	if (info == XLOG_PGSTAT_DATA)
	{
		xl_pgstat_data *xlrec = (xl_pgstat_data *) rec;
		char	   *sep = "";

		appendStringInfo(buf, "stats kind \"%u\"; with data (%zu bytes): ",
						 xlrec->stats_kind, xlrec->data_size);

		/* Write stats data as a series of hex bytes */
		for (int cnt = 0; cnt < xlrec->data_size; cnt++)
		{
			appendStringInfo(buf, "%s%02X", sep, (unsigned char) xlrec->data[cnt]);
			sep = " ";
		}
	}
}

const char *
pgstat_identify(uint8 info)
{
	if ((info & ~XLR_INFO_MASK) == XLOG_PGSTAT_DATA)
		return "PGSTAT_DATA";

	return NULL;
}
