/*-------------------------------------------------------------------------
 *
 * toast_external.c
 *	  Functions for the support of external on-disk TOAST pointers.
 *
 * This includes all the types of external on-disk TOAST pointers supported
 * by the backend, based on the callbacks and data defined in
 * toast_external.h.
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
#include "access/genam.h"
#include "access/heaptoast.h"
#include "access/toast_external.h"
#include "catalog/catalog.h"
#include "miscadmin.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"


/* Callbacks for VARTAG_ONDISK_OID8 */
static void ondisk_oid8_to_external_data(struct varlena *attr,
										 toast_external_data *data);
static struct varlena *ondisk_oid8_create_external_data(toast_external_data data);

/* Callbacks for VARTAG_ONDISK_OID */
static void ondisk_oid_to_external_data(struct varlena *attr,
										toast_external_data *data);
static struct varlena *ondisk_oid_create_external_data(toast_external_data data);

/*
 * Fetch the possibly-unaligned contents of an on-disk external TOAST with
 * OID or OID8 values into a local "varatt_external_*" pointer.
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

static inline void
varatt_external_oid8_get_pointer(varatt_external_oid8 *toast_pointer,
								 struct varlena *attr)
{
	varattrib_1b_e *attre = (varattrib_1b_e *) attr;

	Assert(VARATT_IS_EXTERNAL_ONDISK_OID8(attre));
	Assert(VARSIZE_EXTERNAL(attre) == sizeof(varatt_external_oid8) + VARHDRSZ_EXTERNAL);
	memcpy(toast_pointer, VARDATA_EXTERNAL(attre), sizeof(varatt_external_oid8));
}

/*
 * Decompressed size of an on-disk varlena; but note argument is a struct
 * varatt_external_oid or varatt_external_oid8.
 */
static inline Size
varatt_external_oid_get_extsize(varatt_external_oid toast_pointer)
{
	return toast_pointer.va_extinfo & VARLENA_EXTSIZE_MASK;
}

static inline Size
varatt_external_oid8_get_extsize(varatt_external_oid8 toast_pointer)
{
	return toast_pointer.va_extinfo & VARLENA_EXTSIZE_MASK;
}

/*
 * Compression method of an on-disk varlena; but note argument is a struct
 *  varatt_external_oid or varatt_external_oid8.
 */
static inline uint32
varatt_external_oid_get_compress_method(varatt_external_oid toast_pointer)
{
	return toast_pointer.va_extinfo >> VARLENA_EXTSIZE_BITS;
}

static inline uint32
varatt_external_oid8_get_compress_method(varatt_external_oid8 toast_pointer)
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

static inline bool
varatt_external_oid8_is_compressed(varatt_external_oid8 toast_pointer)
{
	return varatt_external_oid8_get_extsize(toast_pointer) <
		(Size) (toast_pointer.va_rawsize - VARHDRSZ);
}

/*
 * Size of an EXTERNAL datum that contains a standard TOAST pointer
 * (oid8 value).
 */
#define TOAST_POINTER_OID8_SIZE (VARHDRSZ_EXTERNAL + sizeof(varatt_external_oid8))

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
	[VARTAG_ONDISK_OID8] = {
		.toast_pointer_size = TOAST_POINTER_OID8_SIZE,
		.maximum_chunk_size = TOAST_OID_MAX_CHUNK_SIZE,
		.to_external_data = ondisk_oid8_to_external_data,
		.create_external_data = ondisk_oid8_create_external_data,
	},
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

	/* check tag for invalid range */
	if (tag >= TOAST_EXTERNAL_INFO_SIZE)
		elog(ERROR, "incorrect value %u for toast_external_info", tag);

	/* sanity check with tag in valid range */
	res = &toast_external_infos[tag];
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
	Oid		toast_typid;

	/*
	 * If dealing with a code path where a TOAST relation may not be assigned
	 * like heap_toast_insert_or_update(), just use the default with an OID
	 * type.
	 *
	 * In bootstrap mode, we should not do any kind of syscache lookups,
	 * so also rely on OID.
	 */
	if (!OidIsValid(toastrelid) || IsBootstrapProcessingMode())
		return VARTAG_ONDISK_OID;

	/*
	 * Two types of vartag_external are currently supported: OID and OID8,
	 * which depend on the type assigned to "chunk_id" for the TOAST table.
	 *
	 * XXX: Should we assign from the start an OID vartag if dealing with
	 * a TOAST relation with OID8 as value if the value assigned is less
	 * than UINT_MAX?  This just takes the "safe" approach of assigning
	 * the larger vartag in all cases, but this can be made cheaper
	 * depending on the OID consumption.
	 */
	toast_typid = get_atttype(toastrelid, 1);
	if (toast_typid == OID8OID)
		return VARTAG_ONDISK_OID8;

	return VARTAG_ONDISK_OID;
}

/*
 * Helper routines able to translate the various varatt_external_* from/to
 * the in-memory representation toast_external_data used in the backend.
 */

/* Callbacks for VARTAG_ONDISK_OID8 */
static void
ondisk_oid8_to_external_data(struct varlena *attr, toast_external_data *data)
{
	varatt_external_oid8	external;

	varatt_external_oid8_get_pointer(&external, attr);
	data->rawsize = external.va_rawsize;

	/* External size and compression methods are stored in the same field */
	if (varatt_external_oid8_is_compressed(external))
	{
		data->extsize = varatt_external_oid8_get_extsize(external);
		data->compression_method = varatt_external_oid8_get_compress_method(external);
	}
	else
	{
		data->extsize = external.va_extinfo;
		data->compression_method = TOAST_INVALID_COMPRESSION_ID;
	}

	data->valueid = (((uint64) external.va_valueid_hi) << 32) |
		external.va_valueid_lo;
	data->toastrelid = external.va_toastrelid;

}

static struct varlena *
ondisk_oid8_create_external_data(toast_external_data data)
{
	struct varlena *result = NULL;
	varatt_external_oid8 external;

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
	external.va_valueid_hi = (((uint64) data.valueid) >> 32);
	external.va_valueid_lo = (uint32) data.valueid;

	result = (struct varlena *) palloc(TOAST_POINTER_OID8_SIZE);
	SET_VARTAG_EXTERNAL(result, VARTAG_ONDISK_OID8);
	memcpy(VARDATA_EXTERNAL(result), &external, sizeof(external));

	return result;
}


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
