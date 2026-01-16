/*-------------------------------------------------------------------------
 *
 * extended_stats_funcs.c
 *	  Functions for manipulating extended statistics.
 *
 * This file includes the set of facilities required to support the direct
 * manipulations of extended statistics objects.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/statistics/extended_stats_funcs.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/heapam.h"
#include "catalog/pg_collation_d.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_database.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_statistic_ext_data.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/optimizer.h"
#include "statistics/extended_stats_internal.h"
#include "statistics/stat_utils.h"
#include "utils/acl.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"


/*
 * Index of the arguments for the SQL functions.
 */
enum extended_stats_argnum
{
	RELSCHEMA_ARG = 0,
	RELNAME_ARG,
	STATSCHEMA_ARG,
	STATNAME_ARG,
	INHERITED_ARG,
	NDISTINCT_ARG,
	DEPENDENCIES_ARG,
	MOST_COMMON_VALS_ARG,
	MOST_COMMON_VAL_NULLS_ARG,
	MOST_COMMON_FREQS_ARG,
	MOST_COMMON_BASE_FREQS_ARG,
	EXPRESSIONS_ARG,
	NUM_EXTENDED_STATS_ARGS,
};

/*
 * The argument names and type OIDs of the arguments for the SQL
 * functions.
 */
static struct StatsArgInfo extarginfo[] =
{
	[RELSCHEMA_ARG] = {"schemaname", TEXTOID},
	[RELNAME_ARG] = {"relname", TEXTOID},
	[STATSCHEMA_ARG] = {"statistics_schemaname", TEXTOID},
	[STATNAME_ARG] = {"statistics_name", TEXTOID},
	[INHERITED_ARG] = {"inherited", BOOLOID},
	[NDISTINCT_ARG] = {"n_distinct", PG_NDISTINCTOID},
	[DEPENDENCIES_ARG] = {"dependencies", PG_DEPENDENCIESOID},
	[MOST_COMMON_VALS_ARG] = {"most_common_vals", TEXTARRAYOID},
	[MOST_COMMON_VAL_NULLS_ARG] = {"most_common_val_nulls", BOOLARRAYOID},
	[MOST_COMMON_FREQS_ARG] = {"most_common_freqs", FLOAT8ARRAYOID},
	[MOST_COMMON_BASE_FREQS_ARG] = {"most_common_base_freqs", FLOAT8ARRAYOID},
	[EXPRESSIONS_ARG] = {"exprs", TEXTARRAYOID},
	[NUM_EXTENDED_STATS_ARGS] = {0},
};

/*
 * An index of the elements of a stxdexprs Datum, which repeat for each
 * expression in the extended statistics object.
 *
 * NOTE: the RANGE_LENGTH & RANGE_BOUNDS stats are not yet reflected in any
 * version of pg_stat_ext_exprs.
 */
enum extended_stats_exprs_element
{
	NULL_FRAC_ELEM = 0,
	AVG_WIDTH_ELEM,
	N_DISTINCT_ELEM,
	MOST_COMMON_VALS_ELEM,
	MOST_COMMON_FREQS_ELEM,
	HISTOGRAM_BOUNDS_ELEM,
	CORRELATION_ELEM,
	MOST_COMMON_ELEMS_ELEM,
	MOST_COMMON_ELEM_FREQS_ELEM,
	ELEM_COUNT_HISTOGRAM_ELEM,
	NUM_ATTRIBUTE_STATS_ELEMS
};

/*
 * The argument names and typoids of the repeating arguments for stxdexprs.
 */
static struct StatsArgInfo extexprarginfo[] =
{
	[NULL_FRAC_ELEM] = {"null_frac", FLOAT4OID},
	[AVG_WIDTH_ELEM] = {"avg_width", INT4OID},
	[N_DISTINCT_ELEM] = {"n_distinct", FLOAT4OID},
	[MOST_COMMON_VALS_ELEM] = {"most_common_vals", TEXTOID},
	[MOST_COMMON_FREQS_ELEM] = {"most_common_freqs", FLOAT4ARRAYOID},
	[HISTOGRAM_BOUNDS_ELEM] = {"histogram_bounds", TEXTOID},
	[CORRELATION_ELEM] = {"correlation", FLOAT4OID},
	[MOST_COMMON_ELEMS_ELEM] = {"most_common_elems", TEXTOID},
	[MOST_COMMON_ELEM_FREQS_ELEM] = {"most_common_elem_freqs", FLOAT4ARRAYOID},
	[ELEM_COUNT_HISTOGRAM_ELEM] = {"elem_count_histogram", FLOAT4ARRAYOID},
	[NUM_ATTRIBUTE_STATS_ELEMS] = {0}
};

static bool extended_statistics_update(FunctionCallInfo fcinfo);

static HeapTuple get_pg_statistic_ext(Relation pg_stext, Oid nspoid,
									  const char *stxname);
static bool delete_pg_statistic_ext_data(Oid stxoid, bool inherited);


typedef struct
{
	bool		ndistinct;
	bool		dependencies;
	bool		mcv;
	bool		expressions;
} StakindFlags;

static void expand_stxkind(HeapTuple tup, StakindFlags *enabled);
static void upsert_pg_statistic_ext_data(const Datum *values,
										 const bool *nulls,
										 const bool *replaces);
static bool check_mcvlist_array(ArrayType *arr, int argindex,
								int required_ndims, int mcv_length);
static Datum import_expressions(Relation pgsd, int numexprs,
								Oid *atttypids, int32 *atttypmods,
								Oid *atttypcolls, ArrayType *exprs_arr);
static bool text_to_float4(Datum input, Datum *output);
static bool text_to_int4(Datum input, Datum *output);


/*
 * Safe conversion of text to float4.
 */
static bool
text_to_float4(Datum input, Datum *output)
{
	ErrorSaveContext escontext = {T_ErrorSaveContext};

	char	   *s;
	bool		ok;

	s = TextDatumGetCString(input);
	ok = DirectInputFunctionCallSafe(float4in, s, InvalidOid, -1,
									 (Node *) &escontext, output);

	pfree(s);
	return ok;
}

/*
 * Safe conversion of text to int4.
 */
static bool
text_to_int4(Datum input, Datum *output)
{
	ErrorSaveContext escontext = {T_ErrorSaveContext};

	char	   *s;
	bool		ok;

	s = TextDatumGetCString(input);
	ok = DirectInputFunctionCallSafe(int4in, s, InvalidOid, -1,
									 (Node *) &escontext, output);

	pfree(s);
	return ok;
}

/*
 * Fetch a pg_statistic_ext row by name and namespace OID.
 */
static HeapTuple
get_pg_statistic_ext(Relation pg_stext, Oid nspoid, const char *stxname)
{
	ScanKeyData key[2];
	SysScanDesc scan;
	HeapTuple	tup;
	Oid			stxoid = InvalidOid;

	ScanKeyInit(&key[0],
				Anum_pg_statistic_ext_stxname,
				BTEqualStrategyNumber,
				F_NAMEEQ,
				CStringGetDatum(stxname));
	ScanKeyInit(&key[1],
				Anum_pg_statistic_ext_stxnamespace,
				BTEqualStrategyNumber,
				F_OIDEQ,
				ObjectIdGetDatum(nspoid));

	/*
	 * Try to find matching pg_statistic_ext row.
	 */
	scan = systable_beginscan(pg_stext,
							  StatisticExtNameIndexId,
							  true,
							  NULL,
							  2,
							  key);

	/* Lookup is based on a unique index, so we get either 0 or 1 tuple. */
	tup = systable_getnext(scan);

	if (HeapTupleIsValid(tup))
		stxoid = ((Form_pg_statistic_ext) GETSTRUCT(tup))->oid;

	systable_endscan(scan);

	if (!OidIsValid(stxoid))
		return NULL;

	return SearchSysCacheCopy1(STATEXTOID, ObjectIdGetDatum(stxoid));
}

/*
 * Decode the stxkind column so that we know which stats types to expect.
 */
static void
expand_stxkind(HeapTuple tup, StakindFlags *enabled)
{
	Datum		datum;
	ArrayType  *arr;
	char	   *kinds;

	datum = SysCacheGetAttrNotNull(STATEXTOID,
								   tup,
								   Anum_pg_statistic_ext_stxkind);
	arr = DatumGetArrayTypeP(datum);
	if (ARR_NDIM(arr) != 1 || ARR_HASNULL(arr) || ARR_ELEMTYPE(arr) != CHAROID)
		elog(ERROR, "stxkind is not a one-dimension char array");

	kinds = (char *) ARR_DATA_PTR(arr);

	for (int i = 0; i < ARR_DIMS(arr)[0]; i++)
	{
		if (kinds[i] == STATS_EXT_NDISTINCT)
			enabled->ndistinct = true;
		else if (kinds[i] == STATS_EXT_DEPENDENCIES)
			enabled->dependencies = true;
		else if (kinds[i] == STATS_EXT_MCV)
			enabled->mcv = true;
		else if (kinds[i] == STATS_EXT_EXPRESSIONS)
			enabled->expressions = true;
	}
}

/*
 * Perform the actual storage of a pg_statistic_ext_data tuple.
 */
static void
upsert_pg_statistic_ext_data(const Datum *values, const bool *nulls,
							 const bool *replaces)
{
	Relation	pg_stextdata;
	HeapTuple	stxdtup;
	HeapTuple	newtup;

	pg_stextdata = table_open(StatisticExtDataRelationId, RowExclusiveLock);

	stxdtup = SearchSysCache2(STATEXTDATASTXOID,
							  values[Anum_pg_statistic_ext_data_stxoid - 1],
							  values[Anum_pg_statistic_ext_data_stxdinherit - 1]);

	if (HeapTupleIsValid(stxdtup))
	{
		newtup = heap_modify_tuple(stxdtup,
								   RelationGetDescr(pg_stextdata),
								   values,
								   nulls,
								   replaces);
		CatalogTupleUpdate(pg_stextdata, &newtup->t_self, newtup);
		ReleaseSysCache(stxdtup);
	}
	else
	{
		newtup = heap_form_tuple(RelationGetDescr(pg_stextdata), values, nulls);
		CatalogTupleInsert(pg_stextdata, newtup);
	}

	heap_freetuple(newtup);

	CommandCounterIncrement();

	table_close(pg_stextdata, RowExclusiveLock);
}

/*
 * Insert or Update Extended Statistics
 *
 * Major errors, such as the table not existing, the statistics object not
 * existing, or a permissions failure are always reported at ERROR. Other
 * errors, such as a conversion failure on one statistic kind, are reported
 * as WARNINGs, and other statistic kinds may still be updated.
 */
static bool
extended_statistics_update(FunctionCallInfo fcinfo)
{
	char	   *relnspname;
	char	   *relname;
	Oid			nspoid;
	char	   *nspname;
	char	   *stxname;
	bool		inherited;
	Relation	pg_stext;
	HeapTuple	tup = NULL;

	StakindFlags enabled;
	StakindFlags has;

	Form_pg_statistic_ext stxform;

	Datum		values[Natts_pg_statistic_ext_data];
	bool		nulls[Natts_pg_statistic_ext_data];
	bool		replaces[Natts_pg_statistic_ext_data];
	bool		success = true;
	Datum		exprdatum;
	bool		isnull;
	List	   *exprs = NIL;
	int			numattnums = 0;
	int			numexprs = 0;
	int			numattrs = 0;

	/* arrays of type info, if we need them */
	Oid		   *atttypids = NULL;
	int32	   *atttypmods = NULL;
	Oid		   *atttypcolls = NULL;
	Oid			relid;
	Oid			locked_table = InvalidOid;

	memset(nulls, false, sizeof(nulls));
	memset(values, 0, sizeof(values));
	memset(replaces, 0, sizeof(replaces));
	memset(&enabled, 0, sizeof(enabled));

	has.mcv = (!PG_ARGISNULL(MOST_COMMON_VALS_ARG) &&
			   !PG_ARGISNULL(MOST_COMMON_VAL_NULLS_ARG) &&
			   !PG_ARGISNULL(MOST_COMMON_FREQS_ARG) &&
			   !PG_ARGISNULL(MOST_COMMON_BASE_FREQS_ARG));
	has.ndistinct = !PG_ARGISNULL(NDISTINCT_ARG);
	has.dependencies = !PG_ARGISNULL(DEPENDENCIES_ARG);
	has.expressions = !PG_ARGISNULL(EXPRESSIONS_ARG);

	if (RecoveryInProgress())
	{
		ereport(WARNING,
				errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("recovery is in progress"),
				errhint("Statistics cannot be modified during recovery."));
		PG_RETURN_BOOL(false);
	}

	/* relation arguments */
	stats_check_required_arg(fcinfo, extarginfo, RELSCHEMA_ARG);
	relnspname = TextDatumGetCString(PG_GETARG_DATUM(RELSCHEMA_ARG));
	stats_check_required_arg(fcinfo, extarginfo, RELNAME_ARG);
	relname = TextDatumGetCString(PG_GETARG_DATUM(RELNAME_ARG));

	/* extended statistics arguments */
	stats_check_required_arg(fcinfo, extarginfo, STATSCHEMA_ARG);
	nspname = TextDatumGetCString(PG_GETARG_DATUM(STATSCHEMA_ARG));
	stats_check_required_arg(fcinfo, extarginfo, STATNAME_ARG);
	stxname = TextDatumGetCString(PG_GETARG_DATUM(STATNAME_ARG));
	stats_check_required_arg(fcinfo, extarginfo, INHERITED_ARG);
	inherited = PG_GETARG_BOOL(INHERITED_ARG);

	/*
	 * First open the relation where we expect to find the statistics.  This
	 * is similar to relation and attribute statistics, so as ACL checks are
	 * done before any locks are taken, even before any attempts related to
	 * the extended stats object.
	 */
	relid = RangeVarGetRelidExtended(makeRangeVar(relnspname, relname, -1),
									 ShareUpdateExclusiveLock, 0,
									 RangeVarCallbackForStats, &locked_table);

	nspoid = get_namespace_oid(nspname, true);
	if (nspoid == InvalidOid)
	{
		ereport(WARNING,
				errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("could not find schema \"%s\"", stxname));
		PG_RETURN_BOOL(false);
	}

	pg_stext = table_open(StatisticExtRelationId, RowExclusiveLock);
	tup = get_pg_statistic_ext(pg_stext, nspoid, stxname);

	if (!HeapTupleIsValid(tup))
	{
		table_close(pg_stext, RowExclusiveLock);
		ereport(WARNING,
				errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("could not find extended statistics object \"%s\".\"%s\"",
					   get_namespace_name(nspoid), stxname));
		PG_RETURN_BOOL(false);
	}

	stxform = (Form_pg_statistic_ext) GETSTRUCT(tup);

	/*
	 * The relation tracked by the stats object has to match with the relation
	 * we have already locked.
	 */
	if (stxform->stxrelid != relid)
	{
		table_close(pg_stext, RowExclusiveLock);
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not restore extended statistics object \"%s\".\"%s\": incorrect relation \"%s\".\"%s\" specified",
					   get_namespace_name(nspoid), stxname,
					   relnspname, relname));
		PG_RETURN_BOOL(false);
	}

	expand_stxkind(tup, &enabled);
	numattnums = stxform->stxkeys.dim1;

	/* decode expression (if any) */
	exprdatum = SysCacheGetAttr(STATEXTOID,
								tup,
								Anum_pg_statistic_ext_stxexprs,
								&isnull);

	if (!isnull)
	{
		char	   *s;

		s = TextDatumGetCString(exprdatum);
		exprs = (List *) stringToNode(s);
		pfree(s);

		/*
		 * Run the expressions through eval_const_expressions. This is not
		 * just an optimization, but is necessary, because the planner will be
		 * comparing them to similarly-processed qual clauses, and may fail to
		 * detect valid matches without this.  We must not use
		 * canonicalize_qual, however, since these aren't qual expressions.
		 */
		exprs = (List *) eval_const_expressions(NULL, (Node *) exprs);

		/* May as well fix opfuncids too */
		fix_opfuncids((Node *) exprs);
	}
	numexprs = list_length(exprs);
	numattrs = numattnums + numexprs;

	if (has.mcv)
	{
		if (!enabled.mcv)
		{
			ereport(WARNING,
					errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("cannot specify parameters \"%s\", \"%s\", \"%s\", and \"%s\" for extended statistics object",
						   extarginfo[MOST_COMMON_VALS_ARG].argname,
						   extarginfo[MOST_COMMON_VAL_NULLS_ARG].argname,
						   extarginfo[MOST_COMMON_FREQS_ARG].argname,
						   extarginfo[MOST_COMMON_BASE_FREQS_ARG].argname));
			has.mcv = false;
			success = false;
		}
	}
	else
	{
		/* The MCV args must all be NULL. */
		if (!PG_ARGISNULL(MOST_COMMON_VALS_ARG) ||
			!PG_ARGISNULL(MOST_COMMON_VAL_NULLS_ARG) ||
			!PG_ARGISNULL(MOST_COMMON_FREQS_ARG) ||
			!PG_ARGISNULL(MOST_COMMON_BASE_FREQS_ARG))
		{
			ereport(WARNING,
					errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("must specify parameters \"%s\", \"%s\", \"%s\", and \"%s\" for extended statistics object",
						   extarginfo[MOST_COMMON_VALS_ARG].argname,
						   extarginfo[MOST_COMMON_VAL_NULLS_ARG].argname,
						   extarginfo[MOST_COMMON_FREQS_ARG].argname,
						   extarginfo[MOST_COMMON_BASE_FREQS_ARG].argname));
			success = false;
		}
	}

	if (has.ndistinct && !enabled.ndistinct)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("cannot specify parameter \"%s\" for extended statistics object",
					   extarginfo[NDISTINCT_ARG].argname));
		has.ndistinct = false;
		success = false;
	}

	if (has.dependencies && !enabled.dependencies)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("cannot specify parameter \"%s\" for extended statistics object",
					   extarginfo[DEPENDENCIES_ARG].argname));
		has.dependencies = false;
		success = false;
	}

	if (has.expressions && !enabled.expressions)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("cannot specify parameter \"%s\" for extended statistics object",
					   extarginfo[EXPRESSIONS_ARG].argname));
		has.expressions = false;
		success = false;
	}

	/*
	 * Either of these statistic types requires that we supply a semi-filled
	 * VacAttrStatP array.
	 *
	 * It is not possible to use the existing lookup_var_attr_stats() and
	 * examine_attribute() because these functions will skip attributes where
	 * attstattarget is 0, and we may have statistics data to import for those
	 * attributes.
	 */
	if (has.mcv || has.expressions)
	{
		atttypids = palloc0_array(Oid, numattrs);
		atttypmods = palloc0_array(int32, numattrs);
		atttypcolls = palloc0_array(Oid, numattrs);

		for (int i = 0; i < numattnums; i++)
		{
			AttrNumber	attnum = stxform->stxkeys.values[i];

			Oid			lt_opr;
			Oid			eq_opr;
			char		typetype;

			/*
			 * Fetch attribute entries the same way as what is done for
			 * attribute statistics.
			 */
			statatt_get_type(stxform->stxrelid,
							 attnum,
							 &atttypids[i],
							 &atttypmods[i],
							 &typetype,
							 &atttypcolls[i],
							 &lt_opr,
							 &eq_opr);
		}

		for (int i = numattnums; i < numattrs; i++)
		{
			Node	   *expr = list_nth(exprs, i - numattnums);

			atttypids[i] = exprType(expr);
			atttypmods[i] = exprTypmod(expr);
			atttypcolls[i] = exprCollation(expr);

			/*
			 * If it's a multirange, step down to the range type, as is done
			 * by multirange_typanalyze().
			 */
			if (type_is_multirange(atttypids[i]))
				atttypids[i] = get_multirange_range(atttypids[i]);

			/*
			 * Special case: collation for tsvector is DEFAULT_COLLATION_OID.
			 * See compute_tsvector_stats().
			 */
			if (atttypids[i] == TSVECTOROID)
				atttypcolls[i] = DEFAULT_COLLATION_OID;

		}
	}

	/* Primary Key: cannot be NULL or replaced. */
	values[Anum_pg_statistic_ext_data_stxoid - 1] = ObjectIdGetDatum(stxform->oid);
	values[Anum_pg_statistic_ext_data_stxdinherit - 1] = BoolGetDatum(inherited);

	/*
	 * For each stats kind, deserialize the data at hand and perform a round
	 * of validation.  The resulting tuple is filled with a set of updated
	 * values.
	 */

	if (has.ndistinct)
	{
		Datum		ndistinct_datum = PG_GETARG_DATUM(NDISTINCT_ARG);
		bytea	   *data = DatumGetByteaPP(ndistinct_datum);
		MVNDistinct *ndistinct = statext_ndistinct_deserialize(data);

		if (statext_ndistinct_validate(ndistinct, &stxform->stxkeys, numexprs, WARNING))
		{
			values[Anum_pg_statistic_ext_data_stxdndistinct - 1] = ndistinct_datum;
			replaces[Anum_pg_statistic_ext_data_stxdndistinct - 1] = true;
		}
		else
		{
			nulls[Anum_pg_statistic_ext_data_stxdndistinct - 1] = true;
			success = false;
		}

		statext_ndistinct_free(ndistinct);
	}
	else
		nulls[Anum_pg_statistic_ext_data_stxdndistinct - 1] = true;

	if (has.dependencies)
	{
		Datum		dependencies_datum = PG_GETARG_DATUM(DEPENDENCIES_ARG);
		bytea	   *data = DatumGetByteaPP(dependencies_datum);
		MVDependencies *dependencies = statext_dependencies_deserialize(data);

		if (statext_dependencies_validate(dependencies, &stxform->stxkeys, numexprs, WARNING))
		{
			values[Anum_pg_statistic_ext_data_stxddependencies - 1] = dependencies_datum;
			replaces[Anum_pg_statistic_ext_data_stxddependencies - 1] = true;
		}
		else
		{
			nulls[Anum_pg_statistic_ext_data_stxddependencies - 1] = true;
			success = false;
		}

		statext_dependencies_free(dependencies);
	}
	else
		nulls[Anum_pg_statistic_ext_data_stxddependencies - 1] = true;

	if (has.mcv)
	{
		Datum		datum;
		ArrayType  *mcv_arr = PG_GETARG_ARRAYTYPE_P(MOST_COMMON_VALS_ARG);
		ArrayType  *nulls_arr = PG_GETARG_ARRAYTYPE_P(MOST_COMMON_VAL_NULLS_ARG);
		ArrayType  *freqs_arr = PG_GETARG_ARRAYTYPE_P(MOST_COMMON_FREQS_ARG);
		ArrayType  *base_freqs_arr = PG_GETARG_ARRAYTYPE_P(MOST_COMMON_BASE_FREQS_ARG);
		int			nitems;
		Datum	   *mcv_elems;
		bool	   *mcv_nulls;
		int			check_nummcv;

		/*
		 * The mcv_arr is an array of arrays of text.  We use it as the
		 * reference array for checking the lengths of the other 3 arrays.
		 */
		if (ARR_NDIM(mcv_arr) != 2)
		{
			ereport(WARNING,
					errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("could not use parameter \"%s\": expected text array of 2 dimensions",
						   extarginfo[MOST_COMMON_VALS_ARG].argname));
			return (Datum) 0;
		}

		nitems = ARR_DIMS(mcv_arr)[0];

		/* Fixed length arrays that cannot contain NULLs. */
		if (!check_mcvlist_array(nulls_arr, MOST_COMMON_VAL_NULLS_ARG,
								 2, nitems) ||
			!check_mcvlist_array(freqs_arr, MOST_COMMON_FREQS_ARG,
								 1, nitems) ||
			!check_mcvlist_array(base_freqs_arr, MOST_COMMON_BASE_FREQS_ARG,
								 1, nitems))
			return (Datum) 0;


		deconstruct_array_builtin(mcv_arr, TEXTOID, &mcv_elems,
								  &mcv_nulls, &check_nummcv);

		Assert(check_nummcv == (nitems * numattrs));

		datum = import_mcvlist(tup, WARNING, numattrs,
							   atttypids, atttypmods, atttypcolls,
							   nitems, mcv_elems, mcv_nulls,
							   (bool *) ARR_DATA_PTR(nulls_arr),
							   (float8 *) ARR_DATA_PTR(freqs_arr),
							   (float8 *) ARR_DATA_PTR(base_freqs_arr));

		values[Anum_pg_statistic_ext_data_stxdmcv - 1] = datum;
		replaces[Anum_pg_statistic_ext_data_stxdmcv - 1] = true;
	}
	else
		nulls[Anum_pg_statistic_ext_data_stxdmcv - 1] = true;

	if (has.expressions)
	{
		Datum		datum;
		Relation	pgsd;

		pgsd = table_open(StatisticRelationId, RowExclusiveLock);

		/*
		 * Generate the expressions array.
		 *
		 * The attytypids, attytypmods, and atttypcols arrays have all the
		 * regular attributes listed first, so we can pass those arrays with a
		 * start point after the last regular attribute, and there should be
		 * numexprs elements remaining.
		 */
		datum = import_expressions(pgsd, numexprs,
								   &atttypids[numattnums], &atttypmods[numattnums],
								   &atttypcolls[numattnums],
								   PG_GETARG_ARRAYTYPE_P(EXPRESSIONS_ARG));

		table_close(pgsd, RowExclusiveLock);

		values[Anum_pg_statistic_ext_data_stxdexpr - 1] = datum;
		replaces[Anum_pg_statistic_ext_data_stxdexpr - 1] = true;
	}
	else
		nulls[Anum_pg_statistic_ext_data_stxdexpr - 1] = true;

	upsert_pg_statistic_ext_data(values, nulls, replaces);

	heap_freetuple(tup);
	table_close(pg_stext, RowExclusiveLock);

	if (atttypids != NULL)
		pfree(atttypids);
	if (atttypmods != NULL)
		pfree(atttypmods);
	if (atttypcolls != NULL)
		pfree(atttypcolls);
	return success;
}

/*
 * Consistency checks to ensure that other mcvlist arrays are in alignment
 * with the mcv array.
 */
static bool
check_mcvlist_array(ArrayType *arr, int argindex, int required_ndims,
					int mcv_length)
{
	if (ARR_NDIM(arr) != required_ndims)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not use parameter \"%s\": expected array of %d dimensions",
					   extarginfo[argindex].argname, required_ndims));
		return false;
	}

	if (array_contains_nulls(arr))
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not use array \"%s\": NULL value found",
					   extarginfo[argindex].argname));
		return false;
	}

	if (ARR_DIMS(arr)[0] != mcv_length)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not use parameter \"%s\": incorrect number of elements (same as \"%s\" required)",
					   extarginfo[argindex].argname,
					   extarginfo[MOST_COMMON_VALS_ARG].argname));
		return false;
	}

	return true;
}

/*
 * Warn of type mismatch. Common pattern.
 */
static Datum
warn_type_mismatch(Datum d, const char *argname)
{
	char	   *s = TextDatumGetCString(d);

	ereport(WARNING,
			errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("could not match expression %s element \"%s\" with input type",
				   argname, s));
	return (Datum) 0;
}

/*
 * Create the stxdexprs datum using the user input in an array of array of
 * text, referenced against the datatypes for the expressions.
 *
 * This datum is needed to fill out a complete pg_statistic_ext_data tuple.
 *
 * The input arrays should each have "numexprs" elements in them and they
 * should be in the order that the expressions appear in the statistics
 * object.
 */
static Datum
import_expressions(Relation pgsd, int numexprs,
				   Oid *atttypids, int32 *atttypmods,
				   Oid *atttypcolls, ArrayType *exprs_arr)
{
	Datum	   *exprs_elems;
	bool	   *exprs_nulls;
	int			check_numexprs;
	int			offset = 0;

	FmgrInfo	array_in_fn;

	Oid			pgstypoid = get_rel_type_id(StatisticRelationId);

	ArrayBuildState *astate = NULL;

	/*
	 * Verify that the exprs_array is something that matches the expectations
	 * set by stxdexprs generally and the specific statistics object
	 * definition.
	 */
	if (ARR_NDIM(exprs_arr) != 2)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not use parameter \"%s\" due to incorrect dimension (2 required)",
					   extarginfo[EXPRESSIONS_ARG].argname));
		return (Datum) 0;
	}

	if (ARR_DIMS(exprs_arr)[0] != numexprs)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not use parameter \"%s\" due to incorrect outer dimension with %d elements",
					   extarginfo[EXPRESSIONS_ARG].argname, numexprs));
		return (Datum) 0;
	}
	if (ARR_DIMS(exprs_arr)[1] != NUM_ATTRIBUTE_STATS_ELEMS)
	{
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not use parameter \"%s\" due to incorrect inner dimension with %d elements",
					   extarginfo[EXPRESSIONS_ARG].argname,
					   NUM_ATTRIBUTE_STATS_ELEMS));
		return (Datum) 0;
	}

	fmgr_info(F_ARRAY_IN, &array_in_fn);

	deconstruct_array_builtin(exprs_arr, TEXTOID, &exprs_elems,
							  &exprs_nulls, &check_numexprs);

	/*
	 * Iterate over each expected expression.
	 *
	 * The values/nulls/replaces arrays are deconstructed into a 1-D arrays,
	 * so we have to advance an offset by NUM_ATTRIBUTE_STATS_ELEMS to get to
	 * the next row of the 2-D array.
	 */
	for (int i = 0; i < numexprs; i++)
	{
		Oid			typid = atttypids[i];
		int32		typmod = atttypmods[i];
		Oid			stacoll = atttypcolls[i];
		TypeCacheEntry *typcache;

		Oid			elemtypid = InvalidOid;
		Oid			elem_eq_opr = InvalidOid;

		bool		ok;

		Datum		values[Natts_pg_statistic];
		bool		nulls[Natts_pg_statistic];
		bool		replaces[Natts_pg_statistic];

		HeapTuple	pgstup;
		Datum		pgstdat;

		/* Advance the indexes to the next offset. */
		const int	null_frac_idx = offset + NULL_FRAC_ELEM;
		const int	avg_width_idx = offset + AVG_WIDTH_ELEM;
		const int	n_distinct_idx = offset + N_DISTINCT_ELEM;
		const int	most_common_vals_idx = offset + MOST_COMMON_VALS_ELEM;
		const int	most_common_freqs_idx = offset + MOST_COMMON_FREQS_ELEM;
		const int	histogram_bounds_idx = offset + HISTOGRAM_BOUNDS_ELEM;
		const int	correlation_idx = offset + CORRELATION_ELEM;
		const int	most_common_elems_idx = offset + MOST_COMMON_ELEMS_ELEM;
		const int	most_common_elems_freqs_idx = offset + MOST_COMMON_ELEM_FREQS_ELEM;
		const int	elem_count_histogram_idx = offset + ELEM_COUNT_HISTOGRAM_ELEM;

		/* This finds the right operators even if atttypid is a domain */
		typcache = lookup_type_cache(typid, TYPECACHE_LT_OPR | TYPECACHE_EQ_OPR);

		statatt_init_empty_tuple(InvalidOid, InvalidAttrNumber, false,
								 values, nulls, replaces);

		/*
		 * Check each of the fixed attributes to see if they have values set.
		 * If not set, then just let them stay with the default values set in
		 * statatt_init_empty_tuple().
		 */
		if (!exprs_nulls[null_frac_idx])
		{
			ok = text_to_float4(exprs_elems[null_frac_idx],
								&values[Anum_pg_statistic_stanullfrac - 1]);

			if (!ok)
				return warn_type_mismatch(exprs_elems[null_frac_idx],
										  extexprarginfo[NULL_FRAC_ELEM].argname);
		}

		if (!exprs_nulls[avg_width_idx])
		{
			ok = text_to_int4(exprs_elems[avg_width_idx],
							  &values[Anum_pg_statistic_stawidth - 1]);

			if (!ok)
				return warn_type_mismatch(exprs_elems[avg_width_idx],
										  extexprarginfo[AVG_WIDTH_ELEM].argname);
		}

		if (!exprs_nulls[n_distinct_idx])
		{
			ok = text_to_float4(exprs_elems[n_distinct_idx],
								&values[Anum_pg_statistic_stadistinct - 1]);

			if (!ok)
				return warn_type_mismatch(exprs_elems[n_distinct_idx],
										  extexprarginfo[N_DISTINCT_ELEM].argname);
		}

		/*
		 * The STAKIND statistics are the same as the ones found in attribute
		 * stats.  However, these are all derived from text columns, whereas
		 * the ones derived for attribute stats are a mix of datatypes. This
		 * limits the opportunities for code sharing between the two.
		 *
		 * Some statistic kinds have both a stanumbers and a stavalues
		 * components. In those cases, both values must either be NOT NULL or
		 * both NULL, and if they aren't then we need to reject that stakind
		 * completely. Currently we go a step further and reject the
		 * expression array completely.
		 *
		 * Once it is established that the pairs are in NULL/NOT-NULL
		 * alignment, we can test either expr_nulls[] value to see if the
		 * stakind has value(s) that we can set or not.
		 */

		/* STATISTIC_KIND_MCV */
		if (exprs_nulls[most_common_vals_idx] !=
			exprs_nulls[most_common_freqs_idx])
		{
			ereport(WARNING,
					errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("could not use expressions %s and %s: conflicting NULL/NOT NULL",
						   extexprarginfo[MOST_COMMON_VALS_ELEM].argname,
						   extexprarginfo[MOST_COMMON_FREQS_ELEM].argname));
			return (Datum) 0;
		}

		if (!exprs_nulls[most_common_vals_idx])
		{
			Datum		stavalues;
			Datum		stanumbers;

			stavalues = statatt_build_stavalues(extexprarginfo[MOST_COMMON_VALS_ELEM].argname,
												&array_in_fn, exprs_elems[most_common_vals_idx],
												typid, typmod, &ok);

			if (!ok)
				return (Datum) 0;

			stanumbers = statatt_build_stavalues(extexprarginfo[MOST_COMMON_FREQS_ELEM].argname,
												 &array_in_fn, exprs_elems[most_common_freqs_idx],
												 FLOAT4OID, -1, &ok);

			if (!ok)
				return (Datum) 0;

			statatt_set_slot(values, nulls, replaces,
							 STATISTIC_KIND_MCV,
							 typcache->eq_opr, stacoll,
							 stanumbers, false, stavalues, false);
		}

		/* STATISTIC_KIND_HISTOGRAM */
		if (!exprs_nulls[histogram_bounds_idx])
		{
			Datum		stavalues;

			stavalues = statatt_build_stavalues(extexprarginfo[HISTOGRAM_BOUNDS_ELEM].argname,
												&array_in_fn, exprs_elems[histogram_bounds_idx],
												typid, typmod, &ok);

			if (!ok)
				return (Datum) 0;

			statatt_set_slot(values, nulls, replaces,
							 STATISTIC_KIND_HISTOGRAM,
							 typcache->lt_opr, stacoll,
							 0, true, stavalues, false);
		}

		/* STATISTIC_KIND_CORRELATION */
		if (!exprs_nulls[correlation_idx])
		{
			Datum		corr[] = {(Datum) 0};
			ArrayType  *arry;
			Datum		stanumbers;

			ok = text_to_float4(exprs_elems[correlation_idx], &corr[0]);

			if (!ok)
			{
				char	   *s = TextDatumGetCString(exprs_elems[correlation_idx]);

				ereport(WARNING,
						errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("could not match expression %s of element \"%s\" with input type",
							   extexprarginfo[CORRELATION_ELEM].argname, s));
				return (Datum) 0;
			}

			arry = construct_array_builtin(corr, 1, FLOAT4OID);

			stanumbers = PointerGetDatum(arry);

			statatt_set_slot(values, nulls, replaces,
							 STATISTIC_KIND_CORRELATION,
							 typcache->lt_opr, stacoll,
							 stanumbers, false, 0, true);
		}

		/* STATISTIC_KIND_MCELEM */
		if (exprs_nulls[most_common_elems_idx] !=
			exprs_nulls[most_common_elems_freqs_idx])
		{
			ereport(WARNING,
					errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("could not use expressions %s and %s: conflicting NULL and NOT NULL",
						   extexprarginfo[MOST_COMMON_ELEMS_ELEM].argname,
						   extexprarginfo[MOST_COMMON_ELEM_FREQS_ELEM].argname));
			return (Datum) 0;
		}

		/*
		 * We only need to fetch element type and eq operator if we have a
		 * stat of type MCELEM or DECHIST, otherwise the values are
		 * unnecessary and not meaningful.
		 */
		if (!exprs_nulls[most_common_elems_idx] ||
			!exprs_nulls[elem_count_histogram_idx])
		{
			if (!statatt_get_elem_type(typid, typcache->typtype,
									   &elemtypid, &elem_eq_opr))
			{
				ereport(WARNING,
						errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("could not determine element type of expression"));
				return (Datum) 0;
			}
		}

		if (!exprs_nulls[most_common_elems_idx])
		{
			Datum		stavalues;
			Datum		stanumbers;

			stavalues = statatt_build_stavalues(extexprarginfo[MOST_COMMON_ELEMS_ELEM].argname,
												&array_in_fn,
												exprs_elems[most_common_elems_idx],
												elemtypid, typmod, &ok);

			if (!ok)
				return (Datum) 0;

			stanumbers = statatt_build_stavalues(extexprarginfo[MOST_COMMON_ELEM_FREQS_ELEM].argname,
												 &array_in_fn,
												 exprs_elems[most_common_elems_freqs_idx],
												 FLOAT4OID, -1, &ok);

			if (!ok)
				return (Datum) 0;

			statatt_set_slot(values, nulls, replaces,
							 STATISTIC_KIND_MCELEM,
							 elem_eq_opr, stacoll,
							 stanumbers, false, stavalues, false);
		}

		if (!exprs_nulls[elem_count_histogram_idx])
		{
			Datum		stanumbers;

			stanumbers = statatt_build_stavalues(extexprarginfo[ELEM_COUNT_HISTOGRAM_ELEM].argname,
												 &array_in_fn,
												 exprs_elems[elem_count_histogram_idx],
												 FLOAT4OID, -1, &ok);

			if (!ok)
				return (Datum) 0;

			statatt_set_slot(values, nulls, replaces, STATISTIC_KIND_DECHIST,
							 elem_eq_opr, stacoll,
							 stanumbers, false, 0, true);
		}

		/*
		 * Currently there is no extended stats export of the statistic kinds
		 * BOUNDS_HISTOGRAM or RANGE_LENGTH_HISTOGRAM so these cannot be
		 * imported. These may be added in the future.
		 */

		pgstup = heap_form_tuple(RelationGetDescr(pgsd), values, nulls);
		pgstdat = heap_copy_tuple_as_datum(pgstup, RelationGetDescr(pgsd));
		astate = accumArrayResult(astate, pgstdat, false, pgstypoid,
								  CurrentMemoryContext);

		offset += NUM_ATTRIBUTE_STATS_ELEMS;
	}

	pfree(exprs_elems);
	pfree(exprs_nulls);

	return makeArrayResult(astate, CurrentMemoryContext);
}

/*
 * Remove an existing pg_statistic_ext_data row for a given pg_statistic_ext
 * row and "inherited" pair.
 */
static bool
delete_pg_statistic_ext_data(Oid stxoid, bool inherited)
{
	Relation	sed = table_open(StatisticExtDataRelationId, RowExclusiveLock);
	HeapTuple	oldtup;
	bool		result = false;

	/* Is there already a pg_statistic tuple for this attribute? */
	oldtup = SearchSysCache2(STATEXTDATASTXOID,
							 ObjectIdGetDatum(stxoid),
							 BoolGetDatum(inherited));

	if (HeapTupleIsValid(oldtup))
	{
		CatalogTupleDelete(sed, &oldtup->t_self);
		ReleaseSysCache(oldtup);
		result = true;
	}

	table_close(sed, RowExclusiveLock);

	CommandCounterIncrement();

	return result;
}

/*
 * Restore (insert or replace) statistics for the given statistics object.
 */
Datum
pg_restore_extended_stats(PG_FUNCTION_ARGS)
{
	LOCAL_FCINFO(positional_fcinfo, NUM_EXTENDED_STATS_ARGS);
	bool		result = true;

	InitFunctionCallInfoData(*positional_fcinfo, NULL, NUM_EXTENDED_STATS_ARGS,
							 InvalidOid, NULL, NULL);

	if (!stats_fill_fcinfo_from_arg_pairs(fcinfo, positional_fcinfo, extarginfo))
		result = false;

	if (!extended_statistics_update(positional_fcinfo))
		result = false;

	PG_RETURN_BOOL(result);
}

/*
 * Delete statistics for the given statistics object.
 */
Datum
pg_clear_extended_stats(PG_FUNCTION_ARGS)
{
	char	   *relnspname;
	char	   *relname;
	char	   *nspname;
	Oid			nspoid;
	Oid			relid;
	char	   *stxname;
	bool		inherited;
	Relation	pg_stext;
	HeapTuple	tup;
	Form_pg_statistic_ext stxform;
	Oid			locked_table = InvalidOid;

	/* relation arguments */
	stats_check_required_arg(fcinfo, extarginfo, RELSCHEMA_ARG);
	relnspname = TextDatumGetCString(PG_GETARG_DATUM(RELSCHEMA_ARG));
	stats_check_required_arg(fcinfo, extarginfo, RELNAME_ARG);
	relname = TextDatumGetCString(PG_GETARG_DATUM(RELNAME_ARG));

	/* extended statistics arguments */
	stats_check_required_arg(fcinfo, extarginfo, STATSCHEMA_ARG);
	nspname = TextDatumGetCString(PG_GETARG_DATUM(STATSCHEMA_ARG));
	stats_check_required_arg(fcinfo, extarginfo, STATNAME_ARG);
	stxname = TextDatumGetCString(PG_GETARG_DATUM(STATNAME_ARG));
	stats_check_required_arg(fcinfo, extarginfo, INHERITED_ARG);
	inherited = PG_GETARG_BOOL(INHERITED_ARG);

	if (RecoveryInProgress())
	{
		ereport(WARNING,
				errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("recovery is in progress"),
				errhint("Statistics cannot be modified during recovery."));
		PG_RETURN_VOID();
	}

	/*
	 * First open the relation where we expect to find the statistics.  This
	 * is similar to relation and attribute statistics, so as ACL checks are
	 * done before any locks are taken, even before any attempts related to
	 * the extended stats object.
	 */
	relid = RangeVarGetRelidExtended(makeRangeVar(relnspname, relname, -1),
									 ShareUpdateExclusiveLock, 0,
									 RangeVarCallbackForStats, &locked_table);

	/* Now check if the namespace of the stats object exists. */
	nspoid = get_namespace_oid(nspname, true);
	if (nspoid == InvalidOid)
	{
		ereport(WARNING,
				errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("could not find schema \"%s\"", nspname));
		PG_RETURN_VOID();
	}

	pg_stext = table_open(StatisticExtRelationId, RowExclusiveLock);
	tup = get_pg_statistic_ext(pg_stext, nspoid, stxname);

	if (!HeapTupleIsValid(tup))
	{
		table_close(pg_stext, RowExclusiveLock);
		ereport(WARNING,
				errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("could not find extended statistics object \"%s\".\"%s\"",
					   nspname, stxname));
		PG_RETURN_VOID();
	}

	stxform = (Form_pg_statistic_ext) GETSTRUCT(tup);

	/*
	 * This should be consistent, based on the lock taken on the table when we
	 * started.
	 */
	if (stxform->stxrelid != relid)
	{
		table_close(pg_stext, RowExclusiveLock);
		ereport(WARNING,
				errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("could not clear extended statistics object \"%s\".\"%s\": incorrect relation \"%s\".\"%s\" specified",
					   get_namespace_name(nspoid), stxname,
					   relnspname, relname));
		PG_RETURN_VOID();
	}

	delete_pg_statistic_ext_data(stxform->oid, inherited);
	heap_freetuple(tup);

	table_close(pg_stext, RowExclusiveLock);

	PG_RETURN_VOID();
}
