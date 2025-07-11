/*-------------------------------------------------------------------------
 *
 * paramassign.c
 *		Functions for assigning PARAM_EXEC slots during planning.
 *
 * This module is responsible for managing three planner data structures:
 *
 * root->glob->paramExecTypes: records actual assignments of PARAM_EXEC slots.
 * The i'th list element holds the data type OID of the i'th parameter slot.
 * (Elements can be InvalidOid if they represent slots that are needed for
 * chgParam signaling, but will never hold a value at runtime.)  This list is
 * global to the whole plan since the executor has only one PARAM_EXEC array.
 * Assignments are permanent for the plan: we never remove entries once added.
 *
 * root->plan_params: a list of PlannerParamItem nodes, recording Vars and
 * PlaceHolderVars that the root's query level needs to supply to lower-level
 * subqueries, along with the PARAM_EXEC number to use for each such value.
 * Elements are added to this list while planning a subquery, and the list
 * is reset to empty after completion of each subquery.
 *
 * root->curOuterParams: a list of NestLoopParam nodes, recording Vars and
 * PlaceHolderVars that some outer level of nestloop needs to pass down to
 * a lower-level plan node in its righthand side.  Elements are added to this
 * list as createplan.c creates lower Plan nodes that need such Params, and
 * are removed when it creates a NestLoop Plan node that will supply those
 * values.
 *
 * The latter two data structures are used to prevent creating multiple
 * PARAM_EXEC slots (each requiring work to fill) when the same upper
 * SubPlan or NestLoop supplies a value that is referenced in more than
 * one place in its child plan nodes.  However, when the same Var has to
 * be supplied to different subplan trees by different SubPlan or NestLoop
 * parent nodes, we don't recognize any commonality; a fresh plan_params or
 * curOuterParams entry will be made (since the old one has been removed
 * when we finished processing the earlier SubPlan or NestLoop) and a fresh
 * PARAM_EXEC number will be assigned.  At one time we tried to avoid
 * allocating duplicate PARAM_EXEC numbers in such cases, but it's harder
 * than it seems to avoid bugs due to overlapping Param lifetimes, so we
 * don't risk that anymore.  Minimizing the number of PARAM_EXEC slots
 * doesn't really save much executor work anyway.
 *
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/optimizer/util/paramassign.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "nodes/nodeFuncs.h"
#include "nodes/plannodes.h"
#include "optimizer/paramassign.h"
#include "optimizer/placeholder.h"
#include "rewrite/rewriteManip.h"


/*
 * Select a PARAM_EXEC number to identify the given Var as a parameter for
 * the current subquery.  (It might already have one.)
 * Record the need for the Var in the proper upper-level root->plan_params.
 */
static int
assign_param_for_var(PlannerInfo *root, Var *var)
{
	ListCell   *ppl;
	PlannerParamItem *pitem;
	Index		levelsup;

	/* Find the query level the Var belongs to */
	for (levelsup = var->varlevelsup; levelsup > 0; levelsup--)
		root = root->parent_root;

	/* If there's already a matching PlannerParamItem there, just use it */
	foreach(ppl, root->plan_params)
	{
		pitem = (PlannerParamItem *) lfirst(ppl);
		if (IsA(pitem->item, Var))
		{
			Var		   *pvar = (Var *) pitem->item;

			/*
			 * This comparison must match _equalVar(), except for ignoring
			 * varlevelsup.  Note that _equalVar() ignores varnosyn,
			 * varattnosyn, and location, so this does too.
			 */
			if (pvar->varno == var->varno &&
				pvar->varattno == var->varattno &&
				pvar->vartype == var->vartype &&
				pvar->vartypmod == var->vartypmod &&
				pvar->varcollid == var->varcollid &&
				pvar->varreturningtype == var->varreturningtype &&
				bms_equal(pvar->varnullingrels, var->varnullingrels))
				return pitem->paramId;
		}
	}

	/* Nope, so make a new one */
	var = copyObject(var);
	var->varlevelsup = 0;

	pitem = makeNode(PlannerParamItem);
	pitem->item = (Node *) var;
	pitem->paramId = list_length(root->glob->paramExecTypes);
	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 var->vartype);

	root->plan_params = lappend(root->plan_params, pitem);

	return pitem->paramId;
}

/*
 * Generate a Param node to replace the given Var,
 * which is expected to have varlevelsup > 0 (ie, it is not local).
 * Record the need for the Var in the proper upper-level root->plan_params.
 */
Param *
replace_outer_var(PlannerInfo *root, Var *var)
{
	Param	   *retval;
	int			i;

	Assert(var->varlevelsup > 0 && var->varlevelsup < root->query_level);

	/* Find the Var in the appropriate plan_params, or add it if not present */
	i = assign_param_for_var(root, var);

	retval = makeNode(Param);
	retval->paramkind = PARAM_EXEC;
	retval->paramid = i;
	retval->paramtype = var->vartype;
	retval->paramtypmod = var->vartypmod;
	retval->paramcollid = var->varcollid;
	retval->location = var->location;

	return retval;
}

/*
 * Select a PARAM_EXEC number to identify the given PlaceHolderVar as a
 * parameter for the current subquery.  (It might already have one.)
 * Record the need for the PHV in the proper upper-level root->plan_params.
 *
 * This is just like assign_param_for_var, except for PlaceHolderVars.
 */
static int
assign_param_for_placeholdervar(PlannerInfo *root, PlaceHolderVar *phv)
{
	ListCell   *ppl;
	PlannerParamItem *pitem;
	Index		levelsup;

	/* Find the query level the PHV belongs to */
	for (levelsup = phv->phlevelsup; levelsup > 0; levelsup--)
		root = root->parent_root;

	/* If there's already a matching PlannerParamItem there, just use it */
	foreach(ppl, root->plan_params)
	{
		pitem = (PlannerParamItem *) lfirst(ppl);
		if (IsA(pitem->item, PlaceHolderVar))
		{
			PlaceHolderVar *pphv = (PlaceHolderVar *) pitem->item;

			/* We assume comparing the PHIDs is sufficient */
			if (pphv->phid == phv->phid)
				return pitem->paramId;
		}
	}

	/* Nope, so make a new one */
	phv = copyObject(phv);
	IncrementVarSublevelsUp((Node *) phv, -((int) phv->phlevelsup), 0);
	Assert(phv->phlevelsup == 0);

	pitem = makeNode(PlannerParamItem);
	pitem->item = (Node *) phv;
	pitem->paramId = list_length(root->glob->paramExecTypes);
	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 exprType((Node *) phv->phexpr));

	root->plan_params = lappend(root->plan_params, pitem);

	return pitem->paramId;
}

/*
 * Generate a Param node to replace the given PlaceHolderVar,
 * which is expected to have phlevelsup > 0 (ie, it is not local).
 * Record the need for the PHV in the proper upper-level root->plan_params.
 *
 * This is just like replace_outer_var, except for PlaceHolderVars.
 */
Param *
replace_outer_placeholdervar(PlannerInfo *root, PlaceHolderVar *phv)
{
	Param	   *retval;
	int			i;

	Assert(phv->phlevelsup > 0 && phv->phlevelsup < root->query_level);

	/* Find the PHV in the appropriate plan_params, or add it if not present */
	i = assign_param_for_placeholdervar(root, phv);

	retval = makeNode(Param);
	retval->paramkind = PARAM_EXEC;
	retval->paramid = i;
	retval->paramtype = exprType((Node *) phv->phexpr);
	retval->paramtypmod = exprTypmod((Node *) phv->phexpr);
	retval->paramcollid = exprCollation((Node *) phv->phexpr);
	retval->location = -1;

	return retval;
}

/*
 * Generate a Param node to replace the given Aggref
 * which is expected to have agglevelsup > 0 (ie, it is not local).
 * Record the need for the Aggref in the proper upper-level root->plan_params.
 */
Param *
replace_outer_agg(PlannerInfo *root, Aggref *agg)
{
	Param	   *retval;
	PlannerParamItem *pitem;
	Index		levelsup;

	Assert(agg->agglevelsup > 0 && agg->agglevelsup < root->query_level);

	/* Find the query level the Aggref belongs to */
	for (levelsup = agg->agglevelsup; levelsup > 0; levelsup--)
		root = root->parent_root;

	/*
	 * It does not seem worthwhile to try to de-duplicate references to outer
	 * aggs.  Just make a new slot every time.
	 */
	agg = copyObject(agg);
	IncrementVarSublevelsUp((Node *) agg, -((int) agg->agglevelsup), 0);
	Assert(agg->agglevelsup == 0);

	pitem = makeNode(PlannerParamItem);
	pitem->item = (Node *) agg;
	pitem->paramId = list_length(root->glob->paramExecTypes);
	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 agg->aggtype);

	root->plan_params = lappend(root->plan_params, pitem);

	retval = makeNode(Param);
	retval->paramkind = PARAM_EXEC;
	retval->paramid = pitem->paramId;
	retval->paramtype = agg->aggtype;
	retval->paramtypmod = -1;
	retval->paramcollid = agg->aggcollid;
	retval->location = agg->location;

	return retval;
}

/*
 * Generate a Param node to replace the given GroupingFunc expression which is
 * expected to have agglevelsup > 0 (ie, it is not local).
 * Record the need for the GroupingFunc in the proper upper-level
 * root->plan_params.
 */
Param *
replace_outer_grouping(PlannerInfo *root, GroupingFunc *grp)
{
	Param	   *retval;
	PlannerParamItem *pitem;
	Index		levelsup;
	Oid			ptype = exprType((Node *) grp);

	Assert(grp->agglevelsup > 0 && grp->agglevelsup < root->query_level);

	/* Find the query level the GroupingFunc belongs to */
	for (levelsup = grp->agglevelsup; levelsup > 0; levelsup--)
		root = root->parent_root;

	/*
	 * It does not seem worthwhile to try to de-duplicate references to outer
	 * aggs.  Just make a new slot every time.
	 */
	grp = copyObject(grp);
	IncrementVarSublevelsUp((Node *) grp, -((int) grp->agglevelsup), 0);
	Assert(grp->agglevelsup == 0);

	pitem = makeNode(PlannerParamItem);
	pitem->item = (Node *) grp;
	pitem->paramId = list_length(root->glob->paramExecTypes);
	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 ptype);

	root->plan_params = lappend(root->plan_params, pitem);

	retval = makeNode(Param);
	retval->paramkind = PARAM_EXEC;
	retval->paramid = pitem->paramId;
	retval->paramtype = ptype;
	retval->paramtypmod = -1;
	retval->paramcollid = InvalidOid;
	retval->location = grp->location;

	return retval;
}

/*
 * Generate a Param node to replace the given MergeSupportFunc expression
 * which is expected to be in the RETURNING list of an upper-level MERGE
 * query.  Record the need for the MergeSupportFunc in the proper upper-level
 * root->plan_params.
 */
Param *
replace_outer_merge_support(PlannerInfo *root, MergeSupportFunc *msf)
{
	Param	   *retval;
	PlannerParamItem *pitem;
	Oid			ptype = exprType((Node *) msf);

	Assert(root->parse->commandType != CMD_MERGE);

	/*
	 * The parser should have ensured that the MergeSupportFunc is in the
	 * RETURNING list of an upper-level MERGE query, so find that query.
	 */
	do
	{
		root = root->parent_root;
		if (root == NULL)
			elog(ERROR, "MergeSupportFunc found outside MERGE");
	} while (root->parse->commandType != CMD_MERGE);

	/*
	 * It does not seem worthwhile to try to de-duplicate references to outer
	 * MergeSupportFunc expressions.  Just make a new slot every time.
	 */
	msf = copyObject(msf);

	pitem = makeNode(PlannerParamItem);
	pitem->item = (Node *) msf;
	pitem->paramId = list_length(root->glob->paramExecTypes);
	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 ptype);

	root->plan_params = lappend(root->plan_params, pitem);

	retval = makeNode(Param);
	retval->paramkind = PARAM_EXEC;
	retval->paramid = pitem->paramId;
	retval->paramtype = ptype;
	retval->paramtypmod = -1;
	retval->paramcollid = InvalidOid;
	retval->location = msf->location;

	return retval;
}

/*
 * Generate a Param node to replace the given ReturningExpr expression which
 * is expected to have retlevelsup > 0 (ie, it is not local).  Record the need
 * for the ReturningExpr in the proper upper-level root->plan_params.
 */
Param *
replace_outer_returning(PlannerInfo *root, ReturningExpr *rexpr)
{
	Param	   *retval;
	PlannerParamItem *pitem;
	Index		levelsup;
	Oid			ptype = exprType((Node *) rexpr->retexpr);

	Assert(rexpr->retlevelsup > 0 && rexpr->retlevelsup < root->query_level);

	/* Find the query level the ReturningExpr belongs to */
	for (levelsup = rexpr->retlevelsup; levelsup > 0; levelsup--)
		root = root->parent_root;

	/*
	 * It does not seem worthwhile to try to de-duplicate references to outer
	 * ReturningExprs.  Just make a new slot every time.
	 */
	rexpr = copyObject(rexpr);
	IncrementVarSublevelsUp((Node *) rexpr, -((int) rexpr->retlevelsup), 0);
	Assert(rexpr->retlevelsup == 0);

	pitem = makeNode(PlannerParamItem);
	pitem->item = (Node *) rexpr;
	pitem->paramId = list_length(root->glob->paramExecTypes);
	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 ptype);

	root->plan_params = lappend(root->plan_params, pitem);

	retval = makeNode(Param);
	retval->paramkind = PARAM_EXEC;
	retval->paramid = pitem->paramId;
	retval->paramtype = ptype;
	retval->paramtypmod = exprTypmod((Node *) rexpr->retexpr);
	retval->paramcollid = exprCollation((Node *) rexpr->retexpr);
	retval->location = exprLocation((Node *) rexpr->retexpr);

	return retval;
}

/*
 * Generate a Param node to replace the given Var,
 * which is expected to come from some upper NestLoop plan node.
 * Record the need for the Var in root->curOuterParams.
 */
Param *
replace_nestloop_param_var(PlannerInfo *root, Var *var)
{
	Param	   *param;
	NestLoopParam *nlp;
	ListCell   *lc;

	/* Is this Var already listed in root->curOuterParams? */
	foreach(lc, root->curOuterParams)
	{
		nlp = (NestLoopParam *) lfirst(lc);
		if (equal(var, nlp->paramval))
		{
			/* Yes, so just make a Param referencing this NLP's slot */
			param = makeNode(Param);
			param->paramkind = PARAM_EXEC;
			param->paramid = nlp->paramno;
			param->paramtype = var->vartype;
			param->paramtypmod = var->vartypmod;
			param->paramcollid = var->varcollid;
			param->location = var->location;
			return param;
		}
	}

	/* No, so assign a PARAM_EXEC slot for a new NLP */
	param = generate_new_exec_param(root,
									var->vartype,
									var->vartypmod,
									var->varcollid);
	param->location = var->location;

	/* Add it to the list of required NLPs */
	nlp = makeNode(NestLoopParam);
	nlp->paramno = param->paramid;
	nlp->paramval = copyObject(var);
	root->curOuterParams = lappend(root->curOuterParams, nlp);

	/* And return the replacement Param */
	return param;
}

/*
 * Generate a Param node to replace the given PlaceHolderVar,
 * which is expected to come from some upper NestLoop plan node.
 * Record the need for the PHV in root->curOuterParams.
 *
 * This is just like replace_nestloop_param_var, except for PlaceHolderVars.
 */
Param *
replace_nestloop_param_placeholdervar(PlannerInfo *root, PlaceHolderVar *phv)
{
	Param	   *param;
	NestLoopParam *nlp;
	ListCell   *lc;

	/* Is this PHV already listed in root->curOuterParams? */
	foreach(lc, root->curOuterParams)
	{
		nlp = (NestLoopParam *) lfirst(lc);
		if (equal(phv, nlp->paramval))
		{
			/* Yes, so just make a Param referencing this NLP's slot */
			param = makeNode(Param);
			param->paramkind = PARAM_EXEC;
			param->paramid = nlp->paramno;
			param->paramtype = exprType((Node *) phv->phexpr);
			param->paramtypmod = exprTypmod((Node *) phv->phexpr);
			param->paramcollid = exprCollation((Node *) phv->phexpr);
			param->location = -1;
			return param;
		}
	}

	/* No, so assign a PARAM_EXEC slot for a new NLP */
	param = generate_new_exec_param(root,
									exprType((Node *) phv->phexpr),
									exprTypmod((Node *) phv->phexpr),
									exprCollation((Node *) phv->phexpr));

	/* Add it to the list of required NLPs */
	nlp = makeNode(NestLoopParam);
	nlp->paramno = param->paramid;
	nlp->paramval = (Var *) copyObject(phv);
	root->curOuterParams = lappend(root->curOuterParams, nlp);

	/* And return the replacement Param */
	return param;
}

/*
 * process_subquery_nestloop_params
 *	  Handle params of a parameterized subquery that need to be fed
 *	  from an outer nestloop.
 *
 * Currently, that would be *all* params that a subquery in FROM has demanded
 * from the current query level, since they must be LATERAL references.
 *
 * subplan_params is a list of PlannerParamItems that we intend to pass to
 * a subquery-in-FROM.  (This was constructed in root->plan_params while
 * planning the subquery, but isn't there anymore when this is called.)
 *
 * The subplan's references to the outer variables are already represented
 * as PARAM_EXEC Params, since that conversion was done by the routines above
 * while planning the subquery.  So we need not modify the subplan or the
 * PlannerParamItems here.  What we do need to do is add entries to
 * root->curOuterParams to signal the parent nestloop plan node that it must
 * provide these values.  This differs from replace_nestloop_param_var in
 * that the PARAM_EXEC slots to use have already been determined.
 *
 * Note that we also use root->curOuterRels as an implicit parameter for
 * sanity checks.
 */
void
process_subquery_nestloop_params(PlannerInfo *root, List *subplan_params)
{
	ListCell   *lc;

	foreach(lc, subplan_params)
	{
		PlannerParamItem *pitem = lfirst_node(PlannerParamItem, lc);

		if (IsA(pitem->item, Var))
		{
			Var		   *var = (Var *) pitem->item;
			NestLoopParam *nlp;
			ListCell   *lc2;

			/* If not from a nestloop outer rel, complain */
			if (!bms_is_member(var->varno, root->curOuterRels))
				elog(ERROR, "non-LATERAL parameter required by subquery");

			/* Is this param already listed in root->curOuterParams? */
			foreach(lc2, root->curOuterParams)
			{
				nlp = (NestLoopParam *) lfirst(lc2);
				if (nlp->paramno == pitem->paramId)
				{
					Assert(equal(var, nlp->paramval));
					/* Present, so nothing to do */
					break;
				}
			}
			if (lc2 == NULL)
			{
				/* No, so add it */
				nlp = makeNode(NestLoopParam);
				nlp->paramno = pitem->paramId;
				nlp->paramval = copyObject(var);
				root->curOuterParams = lappend(root->curOuterParams, nlp);
			}
		}
		else if (IsA(pitem->item, PlaceHolderVar))
		{
			PlaceHolderVar *phv = (PlaceHolderVar *) pitem->item;
			NestLoopParam *nlp;
			ListCell   *lc2;

			/* If not from a nestloop outer rel, complain */
			if (!bms_is_subset(find_placeholder_info(root, phv)->ph_eval_at,
							   root->curOuterRels))
				elog(ERROR, "non-LATERAL parameter required by subquery");

			/* Is this param already listed in root->curOuterParams? */
			foreach(lc2, root->curOuterParams)
			{
				nlp = (NestLoopParam *) lfirst(lc2);
				if (nlp->paramno == pitem->paramId)
				{
					Assert(equal(phv, nlp->paramval));
					/* Present, so nothing to do */
					break;
				}
			}
			if (lc2 == NULL)
			{
				/* No, so add it */
				nlp = makeNode(NestLoopParam);
				nlp->paramno = pitem->paramId;
				nlp->paramval = (Var *) copyObject(phv);
				root->curOuterParams = lappend(root->curOuterParams, nlp);
			}
		}
		else
			elog(ERROR, "unexpected type of subquery parameter");
	}
}

/*
 * Identify any NestLoopParams that should be supplied by a NestLoop
 * plan node with the specified lefthand rels and required-outer rels.
 * Remove them from the active root->curOuterParams list and return
 * them as the result list.
 *
 * Vars and PHVs appearing in the result list must have nullingrel sets
 * that could validly appear in the lefthand rel's output.  Ordinarily that
 * would be true already, but if we have applied outer join identity 3,
 * there could be more or fewer nullingrel bits in the nodes appearing in
 * curOuterParams than are in the nominal leftrelids.  We deal with that by
 * forcing their nullingrel sets to include exactly the outer-join relids
 * that appear in leftrelids and can null the respective Var or PHV.
 * This fix is a bit ad-hoc and intellectually unsatisfactory, because it's
 * essentially jumping to the conclusion that we've placed evaluation of
 * the nestloop parameters correctly, and thus it defeats the intent of the
 * subsequent nullingrel cross-checks in setrefs.c.  But the alternative
 * seems to be to generate multiple versions of each laterally-parameterized
 * subquery, which'd be unduly expensive.
 */
List *
identify_current_nestloop_params(PlannerInfo *root,
								 Relids leftrelids,
								 Relids outerrelids)
{
	List	   *result;
	Relids		allleftrelids;
	ListCell   *cell;

	/*
	 * We'll be able to evaluate a PHV in the lefthand path if it uses the
	 * lefthand rels plus any available required-outer rels.  But don't do so
	 * if it uses *only* required-outer rels; in that case it should be
	 * evaluated higher in the tree.  For Vars, no such hair-splitting is
	 * necessary since they depend on only one relid.
	 */
	if (outerrelids)
		allleftrelids = bms_union(leftrelids, outerrelids);
	else
		allleftrelids = leftrelids;

	result = NIL;
	foreach(cell, root->curOuterParams)
	{
		NestLoopParam *nlp = (NestLoopParam *) lfirst(cell);

		/*
		 * We are looking for Vars and PHVs that can be supplied by the
		 * lefthand rels.  When we find one, it's okay to modify it in-place
		 * because all the routines above make a fresh copy to put into
		 * curOuterParams.
		 */
		if (IsA(nlp->paramval, Var) &&
			bms_is_member(nlp->paramval->varno, leftrelids))
		{
			Var		   *var = (Var *) nlp->paramval;
			RelOptInfo *rel = root->simple_rel_array[var->varno];

			root->curOuterParams = foreach_delete_current(root->curOuterParams,
														  cell);
			var->varnullingrels = bms_intersect(rel->nulling_relids,
												leftrelids);
			result = lappend(result, nlp);
		}
		else if (IsA(nlp->paramval, PlaceHolderVar))
		{
			PlaceHolderVar *phv = (PlaceHolderVar *) nlp->paramval;
			PlaceHolderInfo *phinfo = find_placeholder_info(root, phv);
			Relids		eval_at = phinfo->ph_eval_at;

			if (bms_is_subset(eval_at, allleftrelids) &&
				bms_overlap(eval_at, leftrelids))
			{
				root->curOuterParams = foreach_delete_current(root->curOuterParams,
															  cell);

				/*
				 * Deal with an edge case: if the PHV was pulled up out of a
				 * subquery and it contains a subquery that was originally
				 * pushed down from this query level, then that will still be
				 * represented as a SubLink, because SS_process_sublinks won't
				 * recurse into outer PHVs, so it didn't get transformed
				 * during expression preprocessing in the subquery.  We need a
				 * version of the PHV that has a SubPlan, which we can get
				 * from the current query level's placeholder_list.  This is
				 * quite grotty of course, but dealing with it earlier in the
				 * handling of subplan params would be just as grotty, and it
				 * might end up being a waste of cycles if we don't decide to
				 * treat the PHV as a NestLoopParam.  (Perhaps that whole
				 * mechanism should be redesigned someday, but today is not
				 * that day.)
				 */
				if (root->parse->hasSubLinks)
				{
					phv = copyObject(phinfo->ph_var);

					/*
					 * The ph_var will have empty nullingrels, but that
					 * doesn't matter since we're about to overwrite
					 * phv->phnullingrels.  Other fields should be OK already.
					 */
					nlp->paramval = (Var *) phv;
				}

				phv->phnullingrels =
					bms_intersect(get_placeholder_nulling_relids(root, phinfo),
								  leftrelids);

				result = lappend(result, nlp);
			}
		}
	}
	return result;
}

/*
 * Generate a new Param node that will not conflict with any other.
 *
 * This is used to create Params representing subplan outputs or
 * NestLoop parameters.
 *
 * We don't need to build a PlannerParamItem for such a Param, but we do
 * need to make sure we record the type in paramExecTypes (otherwise,
 * there won't be a slot allocated for it).
 */
Param *
generate_new_exec_param(PlannerInfo *root, Oid paramtype, int32 paramtypmod,
						Oid paramcollation)
{
	Param	   *retval;

	retval = makeNode(Param);
	retval->paramkind = PARAM_EXEC;
	retval->paramid = list_length(root->glob->paramExecTypes);
	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 paramtype);
	retval->paramtype = paramtype;
	retval->paramtypmod = paramtypmod;
	retval->paramcollid = paramcollation;
	retval->location = -1;

	return retval;
}

/*
 * Assign a (nonnegative) PARAM_EXEC ID for a special parameter (one that
 * is not actually used to carry a value at runtime).  Such parameters are
 * used for special runtime signaling purposes, such as connecting a
 * recursive union node to its worktable scan node or forcing plan
 * re-evaluation within the EvalPlanQual mechanism.  No actual Param node
 * exists with this ID, however.
 */
int
assign_special_exec_param(PlannerInfo *root)
{
	int			paramId = list_length(root->glob->paramExecTypes);

	root->glob->paramExecTypes = lappend_oid(root->glob->paramExecTypes,
											 InvalidOid);
	return paramId;
}
