/*-------------------------------------------------------------------------
 *
 * toast_internals.h
 *	  Internal definitions for the TOAST system.
 *
 * Copyright (c) 2000-2026, PostgreSQL Global Development Group
 *
 * src/include/access/toast_internals.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef TOAST_INTERNALS_H
#define TOAST_INTERNALS_H

#include "access/skey.h"
#include "access/toast_compression.h"
#include "storage/lockdefs.h"
#include "utils/relcache.h"
#include "utils/snapshot.h"
#include "varatt.h"

/*
 * Fill in the header of a compressed-in-line datum: the original data size
 * (excluding header) and the compression method.
 *
 * The compression routine must already have laid out the datum with
 * VARHDRSZ_COMPRESSED bytes of header, since the compressed data starts
 * right after it.  The varlena length word is not touched here.
 */
static inline void
toast_compress_set_size_and_method(varlena *ptr, uint32 rawsize,
								   ToastCompressionId cmid)
{
	varattrib_4b *va = (varattrib_4b *) ptr;

	Assert(rawsize > 0 && rawsize <= VARLENA_EXTSIZE_MASK);
	Assert(cmid == TOAST_PGLZ_COMPRESSION_ID ||
		   cmid == TOAST_LZ4_COMPRESSION_ID);

	va->va_compressed.va_tcinfo =
		rawsize | ((uint32) cmid << VARLENA_EXTSIZE_BITS);
}

extern Datum toast_compress_datum(Datum value, char cmethod);
extern Oid	toast_get_valid_index(Oid toastoid, LOCKMODE lock);

extern void toast_delete_datum(Relation rel, Datum value, bool is_speculative);
extern Datum toast_save_datum(Relation rel, Datum value,
							  varlena *oldexternal, uint32 options);

extern void toast_valueid_scankey_init(ScanKey entry, Oid toast_typid,
									   Oid8 valueid);

extern int	toast_open_indexes(Relation toastrel,
							   LOCKMODE lock,
							   Relation **toastidxs,
							   int *num_indexes);
extern void toast_close_indexes(Relation *toastidxs, int num_indexes,
								LOCKMODE lock);
extern Snapshot get_toast_snapshot(void);

#endif							/* TOAST_INTERNALS_H */
