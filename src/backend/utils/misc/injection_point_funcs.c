/*-------------------------------------------------------------------------
 *
 * injection_point_funcs.c
 *
 * SQL commands and SQL-accessible functions related to injection points.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/injection_point_funcs.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/injection_point.h"

/*
 * pg_get_injection_points
 *
 * Return a table of all the injection points currently attached to the
 * system.
 */
Datum
pg_get_injection_points(PG_FUNCTION_ARGS)
{
#define NUM_PG_GET_INJECTION_POINTS 3
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	List *inj_points;
	ListCell *lc;

	/* Build a tuplestore to return our results in */
	InitMaterializedSRF(fcinfo, 0);

	inj_points = InjectionPointList();

	foreach(lc, inj_points)
	{
		Datum		values[NUM_PG_GET_INJECTION_POINTS];
		bool		nulls[NUM_PG_GET_INJECTION_POINTS];
		InjectionPointData *inj_point = lfirst(lc);

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[0] = PointerGetDatum(cstring_to_text(inj_point->name));
		values[1] = PointerGetDatum(cstring_to_text(inj_point->library));
		values[2] = PointerGetDatum(cstring_to_text(inj_point->function));

		/* shove row into tuplestore */
		tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc, values, nulls);
	}

	return (Datum) 0;
#undef NUM_PG_GET_INJECTION_POINTS
}
