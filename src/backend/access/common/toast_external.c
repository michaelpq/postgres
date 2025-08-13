/*-------------------------------------------------------------------------
 *
 * toast_external.c
 *	  Functions for the support of external on-disk TOAST pointers.
 *
 * This includes all the types of external on-disk TOAST pointers supported
 * by the backend, based on the callbacks and data defined in
 * external_toast.h.
 *
 * Copyright (c) 2025, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/common/toast_external.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/detoast.h"
#include "access/heaptoast.h"
#include "access/toast_external.h"

/* Callbacks for VARTAG_ONDISK_OID */
static void ondisk_oid_to_external_data(struct varlena *attr,
										toast_external_data *data);
static struct varlena *ondisk_oid_create_external_data(toast_external_data data);

/*
 * Fetch the possibly-unaligned contents of an on-disk external TOAST with
 * OID values into a local "varatt_external_oid" pointer.
 *
 * This should be just a memcpy, but some versions of gcc seem to produce
 * broken code that assumes the datum contents are aligned.  Introducing
 * an explicit intermediate "varattrib_1b_e *" variable seems to fix it.
 */
static inline void
varatt_external_oid_get_pointer(varatt_external_oid *toast_pointer,
								struct varlena *attr)
{
	varattrib_1b_e *attre = (varattrib_1b_e *) attr;

	Assert(VARATT_IS_EXTERNAL_ONDISK_OID(attre));
	Assert(VARSIZE_EXTERNAL(attre) == sizeof(varatt_external_oid) + VARHDRSZ_EXTERNAL);
	memcpy(toast_pointer, VARDATA_EXTERNAL(attre), sizeof(varatt_external_oid));
}

/*
 * Decompressed size of an on-disk varlena; but note argument is a struct
 * varatt_external_oid.
 */
static inline Size
varatt_external_oid_get_extsize(varatt_external_oid toast_pointer)
{
	return toast_pointer.va_extinfo & VARLENA_EXTSIZE_MASK;
}

/*
 * Compression method of an on-disk varlena; but note argument is a struct
 *  varatt_external_oid.
 */
static inline uint32
varatt_external_oid_get_compress_method(varatt_external_oid toast_pointer)
{
	return toast_pointer.va_extinfo >> VARLENA_EXTSIZE_BITS;
}

/*
 * Testing whether an externally-stored TOAST value is compressed now requires
 * comparing size stored in va_extinfo (the actual length of the external data)
 * to rawsize (the original uncompressed datum's size).  The latter includes
 * VARHDRSZ overhead, the former doesn't.  We never use compression unless it
 * actually saves space, so we expect either equality or less-than.
 */
static inline bool
varatt_external_oid_is_compressed(varatt_external_oid toast_pointer)
{
	return varatt_external_oid_get_extsize(toast_pointer) <
		(Size) (toast_pointer.va_rawsize - VARHDRSZ);
}

/*
 * Size of an EXTERNAL datum that contains a standard TOAST pointer (OID
 * value).
 */
#define TOAST_OID_POINTER_SIZE (VARHDRSZ_EXTERNAL + sizeof(varatt_external_oid))

/*
 * For now there are only two types, all defined in this file.  For now this
 * is the maximum value of vartag_external, which is a historical choice.
 */
#define TOAST_EXTERNAL_INFO_SIZE	(VARTAG_ONDISK_OID + 1)

/*
 * The different kinds of on-disk external TOAST pointers, divided by
 * vartag_external.
 *
 * See comments for struct toast_external_info about the details of the
 * individual fields.
 */
static const toast_external_info toast_external_infos[TOAST_EXTERNAL_INFO_SIZE] = {
	[VARTAG_ONDISK_OID] = {
		.toast_pointer_size = TOAST_OID_POINTER_SIZE,
		.maximum_chunk_size = TOAST_OID_MAX_CHUNK_SIZE,
		.to_external_data = ondisk_oid_to_external_data,
		.create_external_data = ondisk_oid_create_external_data,
	},
};

/*
 * toast_external_get_info
 *
 * Get toast_external_info of the defined vartag_external, central set of
 * callbacks, based on a "tag", which is a vartag_external value for an
 * on-disk external varlena.
 */
const toast_external_info *
toast_external_get_info(uint8 tag)
{
	const toast_external_info *res = &toast_external_infos[tag];

	/* sanity check, as it could be possible that corrupted data is read */
	if (res == NULL)
		elog(ERROR, "incorrect value %u for toast_external_info", tag);
	return res;
}

/*
 * toast_external_info_get_pointer_size
 *
 * Get external TOAST pointer size based on the attribute type of a TOAST
 * value.  "tag" is a vartag_external value.
 */
int32
toast_external_info_get_pointer_size(uint8 tag)
{
	return toast_external_infos[tag].toast_pointer_size;
}

/*
 * toast_external_assign_vartag
 *
 * Assign the vartag_external of a TOAST tuple, based on the TOAST relation
 * it uses and its value.
 *
 * An invalid value can be given by the caller of this routine, in which
 * case a default vartag should be provided based on only the toast relation
 * used.
 */
uint8
toast_external_assign_vartag(Oid toastrelid, Oid8 valueid)
{
	/*
	 * If dealing with a code path where a TOAST relation may not be assigned,
	 * like heap_toast_insert_or_update(), just use the legacy
	 * vartag_external.
	 */
	if (!OidIsValid(toastrelid))
		return VARTAG_ONDISK_OID;

	/*
	 * Currently there is only one type of vartag_external supported: 4-byte
	 * value with OID for the chunk_id type.
	 *
	 * Note: This routine will be extended to be able to use multiple
	 * vartag_external within a single TOAST relation type, that may change
	 * depending on the value used.
	 */
	return VARTAG_ONDISK_OID;
}

/*
 * Helper routines able to translate the various varatt_external_* from/to
 * the in-memory representation toast_external_data used in the backend.
 */

/* Callbacks for VARTAG_ONDISK_OID */

/*
 * ondisk_oid_to_external_data
 *
 * Translate a varlena to its toast_external_data representation, to be used
 * by the backend code.
 */
static void
ondisk_oid_to_external_data(struct varlena *attr, toast_external_data *data)
{
	varatt_external_oid external;

	varatt_external_oid_get_pointer(&external, attr);
	data->rawsize = external.va_rawsize;

	/*
	 * External size and compression methods are stored in the same field,
	 * extract.
	 */
	if (varatt_external_oid_is_compressed(external))
	{
		data->extsize = varatt_external_oid_get_extsize(external);
		data->compression_method = varatt_external_oid_get_compress_method(external);
	}
	else
	{
		data->extsize = external.va_extinfo;
		data->compression_method = TOAST_INVALID_COMPRESSION_ID;
	}

	data->valueid = (Oid8) external.va_valueid;
	data->toastrelid = external.va_toastrelid;
}

/*
 * ondisk_oid_create_external_data
 *
 * Create a new varlena based on the input toast_external_data, to be used
 * when saving a new TOAST value.
 */
static struct varlena *
ondisk_oid_create_external_data(toast_external_data data)
{
	struct varlena *result = NULL;
	varatt_external_oid external;

	external.va_rawsize = data.rawsize;

	if (data.compression_method != TOAST_INVALID_COMPRESSION_ID)
	{
		/* Set size and compression method, in a single field. */
		VARATT_EXTERNAL_SET_SIZE_AND_COMPRESS_METHOD(external,
													 data.extsize,
													 data.compression_method);
	}
	else
		external.va_extinfo = data.extsize;

	external.va_toastrelid = data.toastrelid;
	external.va_valueid = (Oid) data.valueid;

	result = (struct varlena *) palloc(TOAST_OID_POINTER_SIZE);
	SET_VARTAG_EXTERNAL(result, VARTAG_ONDISK_OID);
	memcpy(VARDATA_EXTERNAL(result), &external, sizeof(external));

	return result;
}
