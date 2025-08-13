/*
 * test_bitmapset.c
 *      Test module for bitmapset data structure
 *
 * This module tests the bitmapset implementation in PostgreSQL,
 * covering all public API functions, edge cases, and memory usage.
 *
 * src/test/modules/test_bitmapset/test_bitmapset.c
 */

#include "postgres.h"

#include <stddef.h>
#include "catalog/pg_type.h"
#include "common/pg_prng.h"
#include "utils/array.h"
#include "fmgr.h"
#include "nodes/bitmapset.h"
#include "nodes/nodes.h"
#include "nodes/pg_list.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"
#include "varatt.h"

PG_MODULE_MAGIC;

/* Utility functions */
PG_FUNCTION_INFO_V1(bms_values);
PG_FUNCTION_INFO_V1(test_bms_from_array);
PG_FUNCTION_INFO_V1(test_bms_to_array);

/* Bitmapset API functions in order of appearance in bitmapset.c */
PG_FUNCTION_INFO_V1(test_bms_make_singleton);
PG_FUNCTION_INFO_V1(test_bms_add_member);
PG_FUNCTION_INFO_V1(test_bms_del_member);
PG_FUNCTION_INFO_V1(test_bms_is_member);
PG_FUNCTION_INFO_V1(test_bms_num_members);
PG_FUNCTION_INFO_V1(test_bms_copy);
PG_FUNCTION_INFO_V1(test_bms_equal);
PG_FUNCTION_INFO_V1(test_bms_compare);
PG_FUNCTION_INFO_V1(test_bms_is_subset);
PG_FUNCTION_INFO_V1(test_bms_subset_compare);
PG_FUNCTION_INFO_V1(test_bms_union);
PG_FUNCTION_INFO_V1(test_bms_intersect);
PG_FUNCTION_INFO_V1(test_bms_difference);
PG_FUNCTION_INFO_V1(test_bms_is_empty);
PG_FUNCTION_INFO_V1(test_bms_membership);
PG_FUNCTION_INFO_V1(test_bms_singleton_member);
PG_FUNCTION_INFO_V1(test_bms_get_singleton_member);
PG_FUNCTION_INFO_V1(test_bms_next_member);
PG_FUNCTION_INFO_V1(test_bms_prev_member);
PG_FUNCTION_INFO_V1(test_bms_hash_value);
PG_FUNCTION_INFO_V1(test_bms_overlap);
PG_FUNCTION_INFO_V1(test_bms_overlap_list);
PG_FUNCTION_INFO_V1(test_bms_nonempty_difference);
PG_FUNCTION_INFO_V1(test_bms_member_index);
PG_FUNCTION_INFO_V1(test_bms_add_range);
PG_FUNCTION_INFO_V1(test_bms_add_members);
PG_FUNCTION_INFO_V1(test_bms_int_members);
PG_FUNCTION_INFO_V1(test_bms_replace_members);
PG_FUNCTION_INFO_V1(test_bms_join);
PG_FUNCTION_INFO_V1(test_bitmap_hash);
PG_FUNCTION_INFO_V1(test_bitmap_match);

/* Test utility functions */
PG_FUNCTION_INFO_V1(test_random_operations);

/* Convenient macros to test results */
#define EXPECT_TRUE(expr)	\
	do { \
		if (!(expr)) \
			elog(ERROR, \
				 "%s was unexpectedly false in file \"%s\" line %u", \
				 #expr, __FILE__, __LINE__); \
	} while (0)

#define EXPECT_NOT_NULL(expr)	\
	do { \
		if ((expr) == NULL) \
			elog(ERROR, \
				 "%s was unexpectedly true in file \"%s\" line %u", \
				 #expr, __FILE__, __LINE__); \
	} while (0)


/*
 * Utility functions to convert between Bitmapset and text
 * for passing data between SQL and C functions
 */

static text *
encode_bms_to_text(Bitmapset *bms)
{
	char	   *str;
	text	   *result;

	if (bms == NULL)
		return NULL;

	str = nodeToString(bms);
	result = cstring_to_text(str);
	pfree(str);

	return result;
}

static Bitmapset *
decode_text_to_bms(text *data)
{
	char	   *str;
	Bitmapset  *bms;

	if (data == NULL)
		return NULL;

	str = text_to_cstring(data);
	bms = (Bitmapset *) stringToNode(str);
	pfree(str);

	return bms;
}

Datum
bms_values(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms;
	char	   *str;
	text	   *result;

	if (PG_ARGISNULL(0))
	{
		result = cstring_to_text("(b)");
		PG_RETURN_TEXT_P(result);
	}

	bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));
	str = bmsToString(bms);

	if (bms)
		bms_free(bms);

	result = cstring_to_text(str);

	PG_RETURN_TEXT_P(result);
}

/*
 * Individual test functions for each bitmapset API function
 */

Datum
test_bms_add_member(PG_FUNCTION_ARGS)
{
	int			member;
	Bitmapset  *bms = NULL;
	text	   *result;

	if (PG_ARGISNULL(1))
		PG_RETURN_NULL();

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	member = PG_GETARG_INT32(1);
	bms = bms_add_member(bms, member);
	result = encode_bms_to_text(bms);

	if (bms)
		bms_free(bms);

	if (result == NULL)
		PG_RETURN_NULL();

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_add_members(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	text	   *result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	/* IMPORTANT: bms_add_members modifies/frees the first argument */
	bms1 = bms_add_members(bms1, bms2);

	if (bms2)
		bms_free(bms2);

	if (bms1 == NULL)
		PG_RETURN_NULL();

	result = encode_bms_to_text(bms1);
	bms_free(bms1);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_del_member(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	int32		member;
	text	   *result;

	if (PG_ARGISNULL(1))
		PG_RETURN_NULL();

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	member = PG_GETARG_INT32(1);
	bms = bms_del_member(bms, member);

	if (bms == NULL || bms_is_empty(bms))
	{
		if (bms)
			bms_free(bms);
		PG_RETURN_NULL();
	}

	result = encode_bms_to_text(bms);
	bms_free(bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_is_member(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	int32		member;
	bool		result;

	if (PG_ARGISNULL(1))
		PG_RETURN_BOOL(false);

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	member = PG_GETARG_INT32(1);
	result = bms_is_member(member, bms);

	if (bms)
		bms_free(bms);

	PG_RETURN_BOOL(result);
}

Datum
test_bms_num_members(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	int			result = 0;

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	result = bms_num_members(bms);

	if (bms)
		bms_free(bms);

	PG_RETURN_INT32(result);
}

Datum
test_bms_make_singleton(PG_FUNCTION_ARGS)
{
	int32		member;
	Bitmapset  *bms;
	text	   *result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	member = PG_GETARG_INT32(0);
	bms = bms_make_singleton(member);

	result = encode_bms_to_text(bms);
	bms_free(bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_copy(PG_FUNCTION_ARGS)
{
	text	   *bms_data;
	Bitmapset  *bms = NULL;
	Bitmapset  *copy_bms;
	text	   *result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	bms_data = PG_GETARG_TEXT_PP(0);
	bms = decode_text_to_bms(bms_data);
	copy_bms = bms_copy(bms);
	result = encode_bms_to_text(copy_bms);

	if (bms)
		bms_free(bms);
	if (copy_bms)
		bms_free(copy_bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_equal(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	bool		result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result = bms_equal(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	PG_RETURN_BOOL(result);
}

Datum
test_bms_union(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	Bitmapset  *result_bms;
	text	   *result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result_bms = bms_union(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	if (result_bms == NULL)
		PG_RETURN_NULL();

	result = encode_bms_to_text(result_bms);
	bms_free(result_bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_membership(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	BMS_Membership result;

	if (PG_ARGISNULL(0))
		PG_RETURN_INT32(BMS_EMPTY_SET);

	bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));
	result = bms_membership(bms);

	if (bms)
		bms_free(bms);

	PG_RETURN_INT32((int32) result);
}

Datum
test_bms_next_member(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	int32		prevmember;
	int			result;

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		PG_RETURN_INT32(-2);

	bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));
	prevmember = PG_GETARG_INT32(1);
	result = bms_next_member(bms, prevmember);

	if (bms)
		bms_free(bms);

	PG_RETURN_INT32(result);
}

Datum
test_bms_from_array(PG_FUNCTION_ARGS)
{
	ArrayType  *array;
	int		   *members;
	int			nmembers;
	Bitmapset  *bms = NULL;
	text	   *result;
	int			i;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	array = PG_GETARG_ARRAYTYPE_P(0);

	/* Check element type first */
	if (ARR_ELEMTYPE(array) != INT4OID)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("integer array expected")));

	/* Handle empty arrays - ARR_NDIM can be 0 for empty arrays */
	if (ARR_NDIM(array) == 0)
		PG_RETURN_NULL();

	/* Now check dimensions */
	if (ARR_NDIM(array) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("integer array expected")));

	nmembers = ARR_DIMS(array)[0];

	/* Double-check for empty */
	if (nmembers == 0)
		PG_RETURN_NULL();

	if (ARR_HASNULL(array))
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("integer array expected")));

	members = (int *) ARR_DATA_PTR(array);

	for (i = 0; i < nmembers; i++)
		bms = bms_add_member(bms, members[i]);

	result = encode_bms_to_text(bms);
	bms_free(bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_to_array(PG_FUNCTION_ARGS)
{
	text	   *bms_data;
	Bitmapset  *bms = NULL;
	ArrayType  *result;
	Datum	   *datums;
	int			nmembers;
	int			member = -1;
	int			i = 0;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	bms_data = PG_GETARG_TEXT_PP(0);
	bms = decode_text_to_bms(bms_data);

	if (bms == NULL || bms_is_empty(bms))
	{
		if (bms)
			bms_free(bms);
		PG_RETURN_NULL();
	}

	nmembers = bms_num_members(bms);
	datums = (Datum *) palloc(nmembers * sizeof(Datum));

	while ((member = bms_next_member(bms, member)) >= 0)
		datums[i++] = Int32GetDatum(member);

	bms_free(bms);

	result = construct_array(datums, nmembers,
							 INT4OID, sizeof(int32), true, 'i');

	pfree(datums);
	PG_RETURN_ARRAYTYPE_P(result);
}

Datum
test_bms_intersect(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	Bitmapset  *result_bms;
	text	   *result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result_bms = bms_intersect(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	if (result_bms == NULL)
		PG_RETURN_NULL();

	result = encode_bms_to_text(result_bms);
	bms_free(result_bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_difference(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	Bitmapset  *result_bms;
	text	   *result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result_bms = bms_difference(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	if (result_bms == NULL)
		PG_RETURN_NULL();

	result = encode_bms_to_text(result_bms);
	bms_free(result_bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_compare(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	int			result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result = bms_compare(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	PG_RETURN_INT32(result);
}

Datum
test_bms_is_empty(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	bool		result;

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	result = bms_is_empty(bms);

	if (bms)
		bms_free(bms);

	PG_RETURN_BOOL(result);
}

Datum
test_bms_is_subset(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	bool		result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result = bms_is_subset(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	PG_RETURN_BOOL(result);
}

Datum
test_bms_subset_compare(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	BMS_Comparison result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result = bms_subset_compare(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	PG_RETURN_INT32((int32) result);
}

Datum
test_bms_singleton_member(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	int			result;

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	result = bms_singleton_member(bms);

	if (bms)
		bms_free(bms);

	PG_RETURN_INT32(result);
}

Datum
test_bms_get_singleton_member(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	int32		default_member = PG_GETARG_INT32(1);
	int			member;
	bool		success;

	if (PG_ARGISNULL(0))
		PG_RETURN_INT32(default_member);

	bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	/*
	 * bms_get_singleton_member returns bool and stores result in member
	 * pointer
	 */
	success = bms_get_singleton_member(bms, &member);
	bms_free(bms);

	if (success)
		PG_RETURN_INT32(member);
	else
		PG_RETURN_INT32(default_member);
}

Datum
test_bms_prev_member(PG_FUNCTION_ARGS)
{
	text	   *bms_data;
	Bitmapset  *bms = NULL;
	int32		prevmember;
	int			result;

	if (PG_ARGISNULL(0))
		PG_RETURN_INT32(-2);

	bms_data = PG_GETARG_TEXT_PP(0);
	prevmember = PG_GETARG_INT32(1);

	if (VARSIZE_ANY_EXHDR(bms_data) == 0)
		PG_RETURN_INT32(-2);

	bms = decode_text_to_bms(bms_data);
	result = bms_prev_member(bms, prevmember);
	bms_free(bms);

	PG_RETURN_INT32(result);
}

Datum
test_bms_overlap(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	bool		result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result = bms_overlap(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	PG_RETURN_BOOL(result);
}

Datum
test_bms_overlap_list(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	ArrayType  *array;
	List	   *bitmapset_list = NIL;
	ListCell   *lc;
	bool		result;
	Datum	   *elem_datums;
	bool	   *elem_nulls;
	int			elem_count;
	int			i;

	/* Handle first argument (the bitmapset) */
	if (PG_ARGISNULL(0))
		PG_RETURN_BOOL(false);

	bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	/* Handle second argument (array of bitmapsets) */
	if (PG_ARGISNULL(1))
	{
		if (bms)
			bms_free(bms);
		PG_RETURN_BOOL(false);
	}

	array = PG_GETARG_ARRAYTYPE_P(1);

	if (ARR_ELEMTYPE(array) != TEXTOID)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("text array expected")));

	deconstruct_array(array,
					  TEXTOID, -1, false, 'i',
					  &elem_datums, &elem_nulls, &elem_count);

	/* Convert each text element to a Bitmapset and add to list */
	for (i = 0; i < elem_count; i++)
	{
		Bitmapset  *list_bms = NULL;

		if (!elem_nulls[i])
		{
			text	   *elem = DatumGetTextP(elem_datums[i]);

			list_bms = decode_text_to_bms(elem);
		}

		/* Add to list (even if NULL) */
		bitmapset_list = lappend(bitmapset_list, list_bms);
	}

	/* Call the actual bms_overlap_list function */
	result = bms_overlap_list(bms, bitmapset_list);

	/* Clean up */
	if (bms)
		bms_free(bms);

	/* Free all bitmapsets in the list */
	foreach(lc, bitmapset_list)
	{
		Bitmapset  *list_bms = (Bitmapset *) lfirst(lc);

		if (list_bms)
			bms_free(list_bms);
	}
	list_free(bitmapset_list);

	/* Free array deconstruction results */
	pfree(elem_datums);
	pfree(elem_nulls);

	PG_RETURN_BOOL(result);
}

Datum
test_bms_nonempty_difference(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	bool		result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	result = bms_nonempty_difference(bms1, bms2);

	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	PG_RETURN_BOOL(result);
}

Datum
test_bms_member_index(PG_FUNCTION_ARGS)
{
	text	   *bms_data;
	Bitmapset  *bms = NULL;
	int32		member;
	int			result;

	if (PG_ARGISNULL(0))
		PG_RETURN_INT32(-1);

	bms_data = PG_GETARG_TEXT_PP(0);
	member = PG_GETARG_INT32(1);

	if (VARSIZE_ANY_EXHDR(bms_data) == 0)
		PG_RETURN_INT32(-1);

	bms = decode_text_to_bms(bms_data);

	result = bms_member_index(bms, member);
	bms_free(bms);

	PG_RETURN_INT32(result);
}

Datum
test_bms_add_range(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	int32		lower,
				upper;
	text	   *result;

	if (PG_ARGISNULL(1) || PG_ARGISNULL(2))
		PG_RETURN_NULL();

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	lower = PG_GETARG_INT32(1);
	upper = PG_GETARG_INT32(2);

	/* Check for invalid range */
	if (upper < lower)
	{
		if (bms)
			bms_free(bms);
		PG_RETURN_NULL();
	}

	bms = bms_add_range(bms, lower, upper);

	result = encode_bms_to_text(bms);
	if (bms)
		bms_free(bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_int_members(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	text	   *result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	bms1 = bms_int_members(bms1, bms2);

	if (bms2)
		bms_free(bms2);

	if (bms1 == NULL)
		PG_RETURN_NULL();

	result = encode_bms_to_text(bms1);

	if (bms1)
		bms_free(bms1);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_replace_members(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	Bitmapset  *result_bms;
	text	   *result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	/* IMPORTANT: bms_replace_members modifies/frees the first argument */
	result_bms = bms_replace_members(bms1, bms2);
	/* bms1 is now invalid, don't free it */

	if (bms2)
		bms_free(bms2);

	if (result_bms == NULL)
		PG_RETURN_NULL();

	result = encode_bms_to_text(result_bms);
	bms_free(result_bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_join(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	Bitmapset  *result_bms;
	text	   *result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	/* IMPORTANT: bms_join modifies/frees the first argument */
	result_bms = bms_join(bms1, bms2);
	/* bms1 is now invalid! Don't free it */

	if (bms2)
		bms_free(bms2);

	if (result_bms == NULL)
		PG_RETURN_NULL();

	result = encode_bms_to_text(result_bms);
	bms_free(result_bms);

	PG_RETURN_TEXT_P(result);
}

Datum
test_bms_hash_value(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	uint32		hash_result;

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	hash_result = bms_hash_value(bms);

	if (bms)
		bms_free(bms);

	PG_RETURN_INT32(hash_result);
}

Datum
test_bitmap_hash(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms = NULL;
	Bitmapset  *bms_ptr;
	uint32		hash_result;

	if (!PG_ARGISNULL(0))
		bms = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	bms_ptr = bms;

	/* Call bitmap_hash */
	hash_result = bitmap_hash(&bms_ptr, sizeof(Bitmapset *));

	/* Clean up */
	if (!PG_ARGISNULL(0) && bms_ptr)
		bms_free(bms_ptr);

	PG_RETURN_INT32(hash_result);
}

Datum
test_bitmap_match(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL,
			   *bms2 = NULL;
	Bitmapset  *bms_ptr1,
			   *bms_ptr2;
	int			match_result;

	if (!PG_ARGISNULL(0))
		bms1 = decode_text_to_bms(PG_GETARG_TEXT_PP(0));

	if (!PG_ARGISNULL(1))
		bms2 = decode_text_to_bms(PG_GETARG_TEXT_PP(1));

	/* Set up pointers to the Bitmapsets */
	bms_ptr1 = bms1;
	bms_ptr2 = bms2;

	/* Call bitmap_match with addresses of the Bitmapset pointers */
	match_result = bitmap_match(&bms_ptr1, &bms_ptr2, sizeof(Bitmapset *));

	/* Clean up */
	if (bms1)
		bms_free(bms1);
	if (bms2)
		bms_free(bms2);

	PG_RETURN_INT32(match_result);
}

Datum
test_random_operations(PG_FUNCTION_ARGS)
{
	Bitmapset  *bms1 = NULL;
	Bitmapset  *bms2 = NULL;
	Bitmapset  *bms = NULL;
	Bitmapset  *result = NULL;
	pg_prng_state state;
	uint64		seed = GetCurrentTimestamp();
	int			num_ops = 5000;
	int			total_ops = 0;
	int			max_range = 2000;
	int			min_value = 0;
	int			member;
	int		   *members;
	int			num_members = 0;

	if (!PG_ARGISNULL(0) && PG_GETARG_INT32(0) > 0)
		seed = PG_GETARG_INT32(0);

	if (!PG_ARGISNULL(1))
		num_ops = PG_GETARG_INT32(1);

	if (!PG_ARGISNULL(2))
		max_range = PG_GETARG_INT32(2);

	if (!PG_ARGISNULL(3))
		min_value = PG_GETARG_INT32(3);

	pg_prng_seed(&state, seed);
	members = palloc(sizeof(int) * num_ops);

	/* Phase 1: Random insertions */
	for (int i = 0; i < num_ops / 2; i++)
	{
		member = pg_prng_uint32(&state) % max_range + min_value;

		if (!bms_is_member(member, bms1))
		{
			members[num_members++] = member;
			bms1 = bms_add_member(bms1, member);
		}
	}

	/* Phase 2: Random set operations */
	for (int i = 0; i < num_ops / 4; i++)
	{
		member = pg_prng_uint32(&state) % max_range + min_value;

		bms2 = bms_add_member(bms2, member);
	}

	/* Test union */
	result = bms_union(bms1, bms2);
	EXPECT_NOT_NULL(result);

	/* Verify union contains all members from first set */
	for (int i = 0; i < num_members; i++)
	{
		if (!bms_is_member(members[i], result))
			elog(ERROR, "union missing member %d", members[i]);
	}
	bms_free(result);

	/* Test intersection */
	result = bms_intersect(bms1, bms2);
	if (result != NULL)
	{
		member = -1;

		while ((member = bms_next_member(result, member)) >= 0)
		{
			if (!bms_is_member(member, bms1) || !bms_is_member(member, bms2))
				elog(ERROR, "intersection contains invalid member %d", member);
		}
		bms_free(result);
	}

	/* Phase 3: Test range operations */
	result = NULL;
	for (int i = 0; i < 10; i++)
	{
		int			lower = pg_prng_uint32(&state) % 100;
		int			upper = lower + (pg_prng_uint32(&state) % 20);

		result = bms_add_range(result, lower, upper);
	}
	if (result != NULL)
	{
		EXPECT_TRUE(bms_num_members(result) > 0);
		bms_free(result);
	}

	pfree(members);
	bms_free(bms1);
	bms_free(bms2);

	for (int i = 0; i < num_ops; i++)
	{
		member = pg_prng_uint32(&state) % max_range + min_value;
		switch (pg_prng_uint32(&state) % 3)
		{
			case 0:				/* add */
				bms = bms_add_member(bms, member);
				break;
			case 1:				/* delete */
				if (bms != NULL)
				{
					bms = bms_del_member(bms, member);
				}
				break;
			case 2:				/* test membership */
				if (bms != NULL)
				{
					bms_is_member(member, bms);
				}
				break;
		}
		total_ops++;
	}

	if (bms)
		bms_free(bms);

	PG_RETURN_INT32(total_ops);
}
