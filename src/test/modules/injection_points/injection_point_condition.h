/*-------------------------------------------------------------------------
 * injection_point_condition.h
 *		Condition payload for injection_wait callbacks
 *
 * Copyright (c) 2025-2026, PostgreSQL Global Development Group
 *
 * src/test/modules/injection_points/injection_point_condition.h
 *-------------------------------------------------------------------------
 */
#ifndef INJECTION_POINT_CONDITION_H
#define INJECTION_POINT_CONDITION_H

typedef enum InjectionPointConditionType
{
	INJ_CONDITION_ALWAYS = 0,
	INJ_CONDITION_PID,
} InjectionPointConditionType;

typedef struct InjectionPointCondition
{
	InjectionPointConditionType type;
	int			pid;
} InjectionPointCondition;

#endif							/* INJECTION_POINT_CONDITION_H */
