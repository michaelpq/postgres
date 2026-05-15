/*-------------------------------------------------------------------------
 *
 * injection_point_condition.h
 *		Shared layout for the InjectionPointCondition private_data blob
 *
 * Used as private_data when attaching an injection_wait callback to scope
 * the wait to a specific backend.  Defined in a header so the producers
 * (injection_points.c's injection_points_attach() and regress_prockill.c's
 * prockill_attach_injection_wait_pid()) and the consumer
 * (injection_points.c's injection_wait via injection_point_allowed())
 * agree on the layout.
 *
 * Copyright (c) 2025-2026, PostgreSQL Global Development Group
 *
 * src/test/modules/injection_points/injection_point_condition.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef INJECTION_POINT_CONDITION_H
#define INJECTION_POINT_CONDITION_H

typedef enum InjectionPointConditionType
{
	INJ_CONDITION_ALWAYS = 0,	/* always run */
	INJ_CONDITION_PID,			/* PID restriction */
} InjectionPointConditionType;

typedef struct InjectionPointCondition
{
	/* Type of the condition */
	InjectionPointConditionType type;

	/* ID of the process where the injection point is allowed to run */
	int			pid;
} InjectionPointCondition;

#endif							/* INJECTION_POINT_CONDITION_H */
