/*-------------------------------------------------------------------------
 *
 * bitmapset.h
 *	  PostgreSQL generic bitmap set package
 *
 * A bitmap set can represent any set of nonnegative integers, although
 * it is mainly intended for sets where the maximum value is not large,
 * say at most a few hundred.  By convention, we always represent the
 * empty set by a NULL pointer.
 *
 *
 * Copyright (c) 2003-2025, PostgreSQL Global Development Group
 *
 * src/include/nodes/bitmapset.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef BITMAPSET_H
#define BITMAPSET_H

#include "nodes/nodes.h"

/*
 * Forward decl to save including pg_list.h
 */
struct List;

/*
 * Data representation
 *
 * Larger bitmap word sizes generally give better performance, so long as
 * they're not wider than the processor can handle efficiently.  We use
 * 64-bit words if pointers are that large, else 32-bit words.
 */
#if SIZEOF_VOID_P >= 8

#define BITS_PER_BITMAPWORD 64
typedef uint64 bitmapword;		/* must be an unsigned type */
typedef int64 signedbitmapword; /* must be the matching signed type */

#else

#define BITS_PER_BITMAPWORD 32
typedef uint32 bitmapword;		/* must be an unsigned type */
typedef int32 signedbitmapword; /* must be the matching signed type */

#endif

typedef struct Bitmapset
{
	pg_node_attr(custom_copy_equal, special_read_write, no_query_jumble)

	NodeTag		type;
	int			nwords;			/* number of words in array */
	bitmapword	words[FLEXIBLE_ARRAY_MEMBER];	/* really [nwords] */
} Bitmapset;


/* result of bms_subset_compare */
typedef enum
{
	BMS_EQUAL,					/* sets are equal */
	BMS_SUBSET1,				/* first set is a subset of the second */
	BMS_SUBSET2,				/* second set is a subset of the first */
	BMS_DIFFERENT,				/* neither set is a subset of the other */
} BMS_Comparison;

/* result of bms_membership */
typedef enum
{
	BMS_EMPTY_SET,				/* 0 members */
	BMS_SINGLETON,				/* 1 member */
	BMS_MULTIPLE,				/* >1 member */
} BMS_Membership;

/* Select appropriate bit-twiddling functions for bitmap word size */
#if BITS_PER_BITMAPWORD == 32
#define bmw_leftmost_one_pos(w)		pg_leftmost_one_pos32(w)
#define bmw_rightmost_one_pos(w)	pg_rightmost_one_pos32(w)
#define bmw_popcount(w)				pg_popcount32(w)
#elif BITS_PER_BITMAPWORD == 64
#define bmw_leftmost_one_pos(w)		pg_leftmost_one_pos64(w)
#define bmw_rightmost_one_pos(w)	pg_rightmost_one_pos64(w)
#define bmw_popcount(w)				pg_popcount64(w)
#else
#error "invalid BITS_PER_BITMAPWORD"
#endif


/*
 * function prototypes in nodes/bitmapset.c
 */

extern Bitmapset *bms_copy(const Bitmapset *a);
extern bool bms_equal(const Bitmapset *a, const Bitmapset *b);
extern int	bms_compare(const Bitmapset *a, const Bitmapset *b);
extern Bitmapset *bms_make_singleton(int x);
extern void bms_free(Bitmapset *a);

extern Bitmapset *bms_union(const Bitmapset *a, const Bitmapset *b);
extern Bitmapset *bms_intersect(const Bitmapset *a, const Bitmapset *b);
extern Bitmapset *bms_difference(const Bitmapset *a, const Bitmapset *b);
extern bool bms_is_subset(const Bitmapset *a, const Bitmapset *b);
extern BMS_Comparison bms_subset_compare(const Bitmapset *a, const Bitmapset *b);
extern bool bms_is_member(int x, const Bitmapset *a);
extern int	bms_member_index(Bitmapset *a, int x);
extern bool bms_overlap(const Bitmapset *a, const Bitmapset *b);
extern bool bms_overlap_list(const Bitmapset *a, const struct List *b);
extern bool bms_nonempty_difference(const Bitmapset *a, const Bitmapset *b);
extern int	bms_singleton_member(const Bitmapset *a);
extern bool bms_get_singleton_member(const Bitmapset *a, int *member);
extern int	bms_num_members(const Bitmapset *a);

/* optimized tests when we don't need to know exact membership count: */
extern BMS_Membership bms_membership(const Bitmapset *a);

/* NULL is now the only allowed representation of an empty bitmapset */
#define bms_is_empty(a)  ((a) == NULL)

/* these routines recycle (modify or free) their non-const inputs: */

extern Bitmapset *bms_add_member(Bitmapset *a, int x);
extern Bitmapset *bms_del_member(Bitmapset *a, int x);
extern Bitmapset *bms_add_members(Bitmapset *a, const Bitmapset *b);
extern Bitmapset *bms_replace_members(Bitmapset *a, const Bitmapset *b);
extern Bitmapset *bms_add_range(Bitmapset *a, int lower, int upper);
extern Bitmapset *bms_int_members(Bitmapset *a, const Bitmapset *b);
extern Bitmapset *bms_del_members(Bitmapset *a, const Bitmapset *b);
extern Bitmapset *bms_join(Bitmapset *a, Bitmapset *b);

/* support for iterating through the integer elements of a set: */
extern int	bms_next_member(const Bitmapset *a, int prevbit);
extern int	bms_prev_member(const Bitmapset *a, int prevbit);

/* support for hashtables using Bitmapsets as keys: */
extern uint32 bms_hash_value(const Bitmapset *a);
extern uint32 bitmap_hash(const void *key, Size keysize);
extern int	bitmap_match(const void *key1, const void *key2, Size keysize);

#endif							/* BITMAPSET_H */
