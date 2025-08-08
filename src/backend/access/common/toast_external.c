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
#include "access/genam.h"
#include "access/heaptoast.h"
#include "access/toast_counter.h"
#include "access/toast_external.h"
#include "access/toast_type.h"
#include "catalog/catalog.h"
#include "miscadmin.h"
#include "utils/fmgroids.h"
#include "utils/snapmgr.h"
#include "utils/lsyscache.h"


/* Callbacks for VARTAG_ONDISK_INT8 */
static void ondisk_int8_to_external_data(struct varlena *attr,
										 toast_external_data *data);
static struct varlena *ondisk_int8_create_external_data(toast_external_data data);
static uint64 ondisk_int8_get_new_value(Relation toastrel, Oid indexid,
										AttrNumber attnum);

/* Callbacks for VARTAG_ONDISK_OID */
static void ondisk_oid_to_external_data(struct varlena *attr,
										toast_external_data *data);
static struct varlena *ondisk_oid_create_external_data(toast_external_data data);
static uint64 ondisk_oid_get_new_value(Relation toastrel, Oid indexid,
									   AttrNumber attnum);

/*
 * Decompressed size of an on-disk varlena; but note argument is a struct
 * varatt_external_oid or varatt_external_int8.
 */
static inline Size
varatt_external_oid_get_extsize(struct varatt_external_oid toast_pointer)
{
	return toast_pointer.va_extinfo & VARLENA_EXTSIZE_MASK;
}

static inline Size
varatt_external_int8_get_extsize(struct varatt_external_int8 toast_pointer)
{
	return toast_pointer.va_extinfo & VARLENA_EXTSIZE_MASK;
}

/*
 * Compression method of an on-disk varlena; but note argument is a struct
 *  varatt_external_oid or varatt_external_int8.
 */
static inline uint32
varatt_external_oid_get_compress_method(struct varatt_external_oid toast_pointer)
{
	return toast_pointer.va_extinfo >> VARLENA_EXTSIZE_BITS;
}

static inline uint32
varatt_external_int8_get_compress_method(struct varatt_external_int8 toast_pointer)
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
varatt_external_oid_is_compressed(struct varatt_external_oid toast_pointer)
{
	return varatt_external_oid_get_extsize(toast_pointer) <
		(Size) (toast_pointer.va_rawsize - VARHDRSZ);
}

static inline bool
varatt_external_int8_is_compressed(struct varatt_external_int8 toast_pointer)
{
	return varatt_external_int8_get_extsize(toast_pointer) <
		(Size) (toast_pointer.va_rawsize - VARHDRSZ);
}

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
 * The different kinds of on-disk external TOAST pointers, divided by
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
		.get_new_value = ondisk_int8_get_new_value,
	},
	[VARTAG_ONDISK_OID] = {
		.toast_pointer_size = TOAST_POINTER_OID_SIZE,
		.maximum_chunk_size = TOAST_MAX_CHUNK_SIZE_OID,
		.to_external_data = ondisk_oid_to_external_data,
		.create_external_data = ondisk_oid_create_external_data,
		.get_new_value = ondisk_oid_get_new_value,
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
 * Assign the vartag_external of a TOAST tuple, based on the TOAST relation
 * it uses and its value.
 *
 * An invalid value can be given by the caller of this routine, in which
 * case a default vartag should be provided based on only the toast relation
 * used.
 */
uint8
toast_external_assign_vartag(Oid toastrelid, uint64 value)
{
	Oid		toast_typid;

	/*
	 * If dealing with a code path where a TOAST relation may not be assigned
	 * like heap_toast_insert_or_update(), just use the vartag_external that
	 * can be guessed based on the GUC default_toast_type.
	 *
	 * In bootstrap mode, we should not do any kind of syscache lookups,
	 * so do the same and rely on the value of default_toast_type.
	 */
	if (!OidIsValid(toastrelid) || IsBootstrapProcessingMode())
	{
		if (default_toast_type == TOAST_TYPE_INT8)
			return VARTAG_ONDISK_INT8;
		return VARTAG_ONDISK_OID;
	}

	/*
	 * Two types of vartag_external are currently supported: OID and int8,
	 * which depend on the type assigned to "chunk_id" for the TOAST table.
	 */
	toast_typid = get_atttype(toastrelid, 1);
	if (toast_typid == INT8OID)
		return VARTAG_ONDISK_INT8;

	return VARTAG_ONDISK_OID;
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
	if (varatt_external_int8_is_compressed(external))
	{
		data->extsize = varatt_external_int8_get_extsize(external);
		data->compression_method = varatt_external_int8_get_compress_method(external);
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

static uint64
ondisk_int8_get_new_value(Relation toastrel, Oid indexid,
						  AttrNumber attnum)
{
	uint64		new_value;
	SysScanDesc	scan;
	ScanKeyData	key;
	bool		collides = false;

retry:
	new_value = GetNewToastId();

	/* No indexes in bootstrap mode, so leave */
	if (IsBootstrapProcessingMode())
		return new_value;

	Assert(IsSystemRelation(toastrel));

	CHECK_FOR_INTERRUPTS();

	/*
	 * Check if the new value picked already exists in the toast relation.
	 * If there is a conflict, retry.
	 */
	ScanKeyInit(&key,
				attnum,
				BTEqualStrategyNumber, F_INT8EQ,
				Int64GetDatum(new_value));

	/* see notes in GetNewOidWithIndex() above about using SnapshotAny */
	scan = systable_beginscan(toastrel, indexid, true,
							  SnapshotAny, 1, &key);
	collides = HeapTupleIsValid(systable_getnext(scan));
	systable_endscan(scan);

	if (collides)
		goto retry;

	return new_value;
}


/* Callbacks for VARTAG_ONDISK_OID */
static void
ondisk_oid_to_external_data(struct varlena *attr, toast_external_data *data)
{
	varatt_external_oid external;

	VARATT_EXTERNAL_GET_POINTER(external, attr);
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

static uint64
ondisk_oid_get_new_value(Relation toastrel, Oid indexid,
						 AttrNumber attnum)
{
	return GetNewOidWithIndex(toastrel, indexid, attnum);
}
