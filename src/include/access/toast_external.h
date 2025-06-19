/*-------------------------------------------------------------------------
 *
 * toast_external.h
 *	  Support for on-disk external TOAST pointers
 *
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1995, Regents of the University of California
 *
 * src/include/access/toast_external.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef TOAST_EXTERNAL_H
#define TOAST_EXTERNAL_H

#include "access/toast_compression.h"
#include "varatt.h"

/*
 * Intermediate in-memory structure used when creating on-disk
 * varatt_external_* or when deserializing varlena contents.
 */
typedef struct toast_external_data
{
	/* Original data size (includes header) */
	int32       rawsize;
	/* External saved size (without header) */
	uint32      extsize;
	/* compression method */
	ToastCompressionId compression_method;
	/* Relation OID of TOAST table containing the value */
	Oid			toastrelid;
	/*
	 * Unique ID of value within TOAST table.  This could be an OID or an
	 * int8 value.  This field is large enough to be able to store any of
	 * them.
	 */
	uint64		value;
} toast_external_data;

/*
 * Metadata for external TOAST pointer kinds, separated based on their
 * vartag_external.
 */
typedef struct toast_external_info
{
	/*
	 * Maximum chunk of data authorized for this type of external TOAST
	 * pointer, when dividing an entry by chunks.  Sized depending on
	 * the size of its varatt_external_* structure.
	 */
	int32		maximum_chunk_size;

	/*
	 * Size of an external TOAST pointer of this type, typically
	 * (VARHDRSZ_EXTERNAL + sizeof(varatt_external_struct)).
	 */
	int32		toast_pointer_size;

	/*
	 * Map an input varlena to a toast_external_data, for consumption
	 * in the backend code.  "data" is an input/output result.
	 */
	void		(*to_external_data) (struct varlena *attr,
									 toast_external_data *data);

	/*
	 * Create a varlena that will be used on-disk for the given TOAST
	 * type, based on the given input data.
	 *
	 * The result is the varlena created, for on-disk insertion.
	 */
	struct varlena  *(*create_external_data) (toast_external_data data);

} toast_external_info;

/* Retrieve a toast_external_info from a vartag */
extern const toast_external_info *toast_external_get_info(uint8 tag);

/* Retrieve toast_pointer_size using a TOAST attribute type */
extern int32 toast_external_info_get_pointer_size(Oid toast_typid);

/*
 * Testing whether an externally-stored value is compressed now requires
 * comparing size stored in extsize (the actual length of the external data)
 * to rawsize (the original uncompressed datum's size).  The latter includes
 * VARHDRSZ overhead, the former doesn't.  We never use compression unless it
 * actually saves space, so we expect either equality or less-than.
 */
#define TOAST_EXTERNAL_IS_COMPRESSED(data) \
	((data).extsize < (data).rawsize - VARHDRSZ)

/* Full data structure */
static inline void
toast_external_info_get_data(struct varlena *attr, toast_external_data *data)
{
	uint8 tag = VARTAG_EXTERNAL(attr);
	const toast_external_info *info = toast_external_get_info(tag);

	info->to_external_data(attr, data);
}

/*
 * Helper routines to recover specific fields in toast_external_data.  Most
 * code paths doing work with on-disk external TOAST pointers care about
 * these.
 */

/* Detoasted "raw" size */
static inline Size
toast_external_info_get_rawsize(struct varlena *attr)
{
	uint8 tag = VARTAG_EXTERNAL(attr);
	const toast_external_info *info = toast_external_get_info(tag);
	toast_external_data data;

	info->to_external_data(attr, &data);

	return data.rawsize;
}

/* External saved size */
static inline Size
toast_external_info_get_extsize(struct varlena *attr)
{
	uint8 tag = VARTAG_EXTERNAL(attr);
	const toast_external_info *info = toast_external_get_info(tag);
	toast_external_data data;

	info->to_external_data(attr, &data);

	return data.extsize;
}

/* Compression method ID */
static inline ToastCompressionId
toast_external_info_get_compression_method(struct varlena *attr)
{
	uint8 tag = VARTAG_EXTERNAL(attr);
	const toast_external_info *info = toast_external_get_info(tag);
	toast_external_data data;

	info->to_external_data(attr, &data);

	return data.compression_method;
}

/* Value ID */
static inline Size
toast_external_info_get_value(struct varlena *attr)
{
	uint8 tag = VARTAG_EXTERNAL(attr);
	const toast_external_info *info = toast_external_get_info(tag);
	toast_external_data data;

	info->to_external_data(attr, &data);

	return data.value;
}

#endif			/* TOAST_EXTERNAL_H */
