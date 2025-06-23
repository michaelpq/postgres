/*-------------------------------------------------------------------------
 *
 * toast_external.c
 *	  Functions for the support of external on-disk TOAST pointers.
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

/* Callbacks for VARTAG_ONDISK_INT8 */
static void ondisk_int8_to_external_data(struct varlena *attr,
										 toast_external_data *data);
static struct varlena *ondisk_int8_create_external_data(toast_external_data data);

/* Callbacks for VARTAG_ONDISK_OID */
static void ondisk_oid_to_external_data(struct varlena *attr,
										toast_external_data *data);
static struct varlena *ondisk_oid_create_external_data(toast_external_data data);


/*
 * Size of an EXTERNAL datum that contains a standard TOAST pointer
 * (int8 value).
 */
#define TOAST_POINTER_INT8_SIZE (VARHDRSZ_EXTERNAL + sizeof(varatt_external_int8))

/*
 * Size of an EXTERNAL datum that contains a standard TOAST pointer (OID
 * value).
 */
#define TOAST_POINTER_OID_SIZE (VARHDRSZ_EXTERNAL + sizeof(varatt_external_oid))

/*
 * For now there are only two types, all defined in this file.  For now this
 * is the maximum value of vartag_external, which is a historical choice.
 */
#define TOAST_EXTERNAL_INFO_SIZE	(VARTAG_ONDISK_OID + 1)

/*
 * The different kinds of on-disk external TOAST pointers. divided by
 * vartag_external.
 *
 * See comments for struct toast_external_info about the details of the
 * individual fields.
 */
static const toast_external_info toast_external_infos[TOAST_EXTERNAL_INFO_SIZE] = {
	[VARTAG_ONDISK_INT8] = {
		.toast_pointer_size = TOAST_POINTER_INT8_SIZE,
		.maximum_chunk_size = TOAST_MAX_CHUNK_SIZE_INT8,
		.to_external_data = ondisk_int8_to_external_data,
		.create_external_data = ondisk_int8_create_external_data,
	},
	[VARTAG_ONDISK_OID] = {
		.toast_pointer_size = TOAST_POINTER_INT8_SIZE,
		.maximum_chunk_size = TOAST_MAX_CHUNK_SIZE_OID,
		.to_external_data = ondisk_oid_to_external_data,
		.create_external_data = ondisk_oid_create_external_data,
	},
};


/* Get toast_external_info of the defined vartag_external */
const toast_external_info *
toast_external_get_info(uint8 tag)
{
	return &toast_external_infos[tag];
}

/*
 * Get external TOAST pointer size based on the attribute type of a TOAST
 * value.
 */
int32
toast_external_info_get_pointer_size(uint8 tag)
{
	return toast_external_infos[tag].toast_pointer_size;
}

/*
 * Helper routines able to translate the various varatt_external_* from/to
 * the in-memory representation toast_external_data used in the backend.
 */

/* Callbacks for VARTAG_ONDISK_INT8 */
static void
ondisk_int8_to_external_data(struct varlena *attr, toast_external_data *data)
{
	varatt_external_int8	external;

	VARATT_EXTERNAL_GET_POINTER(external, attr);
	data->rawsize = external.va_rawsize;

	/* External size and compression methods are stored in the same field */
	if (VARATT_EXTERNAL_IS_COMPRESSED(external))
	{
		data->extsize = VARATT_EXTERNAL_GET_EXTSIZE(external);
		data->compression_method = VARATT_EXTERNAL_GET_COMPRESS_METHOD(external);
	}
	else
	{
		data->extsize = external.va_extinfo;
		data->compression_method = TOAST_INVALID_COMPRESSION_ID;
	}

	data->value = (((uint64) external.va_valueid_hi) << 32) |
		external.va_valueid_lo;
	data->toastrelid = external.va_toastrelid;

}

static struct varlena *
ondisk_int8_create_external_data(toast_external_data data)
{
	struct varlena *result = NULL;
	varatt_external_int8 external;

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
	external.va_valueid_hi = (((uint64) data.value) >> 32);
	external.va_valueid_lo = (uint32) data.value;

	result = (struct varlena *) palloc(TOAST_POINTER_INT8_SIZE);
	SET_VARTAG_EXTERNAL(result, VARTAG_ONDISK_INT8);
	memcpy(VARDATA_EXTERNAL(result), &external, sizeof(external));

	return result;
}

/* Callbacks for VARTAG_ONDISK_OID */
static void
ondisk_oid_to_external_data(struct varlena *attr, toast_external_data *data)
{
	varatt_external_oid		external;

	VARATT_EXTERNAL_GET_POINTER(external, attr);
	data->rawsize = external.va_rawsize;

	/*
	 * External size and compression methods are stored in the same field,
	 * extract.
	 */
	if (VARATT_EXTERNAL_IS_COMPRESSED(external))
	{
		data->extsize = VARATT_EXTERNAL_GET_EXTSIZE(external);
		data->compression_method = VARATT_EXTERNAL_GET_COMPRESS_METHOD(external);
	}
	else
	{
		data->extsize = external.va_extinfo;
		data->compression_method = TOAST_INVALID_COMPRESSION_ID;
	}

	data->value = (uint64) external.va_valueid;
	data->toastrelid = external.va_toastrelid;
}

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
	external.va_valueid = (Oid) data.value;

	result = (struct varlena *) palloc(TOAST_POINTER_OID_SIZE);
	SET_VARTAG_EXTERNAL(result, VARTAG_ONDISK_OID);
	memcpy(VARDATA_EXTERNAL(result), &external, sizeof(external));

	return result;
}
