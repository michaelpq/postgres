/*-------------------------------------------------------------------------
 *
 * toast_type.h
 *	  Internal definitions for the types supported by values in TOAST
 *	  relations.
 *
 * Copyright (c) 2000-2025, PostgreSQL Global Development Group
 *
 * src/include/access/toast_type.h
 *
 *-------------------------------------------------------------------------

 */
#ifndef TOAST_TYPE_H
#define TOAST_TYPE_H

/*
 * GUC support
 *
 * Detault value type in toast table.
 */
extern PGDLLIMPORT int default_toast_type;

typedef enum ToastTypeId
{
	TOAST_TYPE_INVALID = 0,
	TOAST_TYPE_OID = 1,
	TOAST_TYPE_INT8 = 2,
} ToastTypeId;

#endif							/* TOAST_TYPE_H */
