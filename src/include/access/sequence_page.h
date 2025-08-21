/*-------------------------------------------------------------------------
 *
 * sequence_page.h
 *	  Helper macros for page manipulations with sequence access methods.
 *
 * These macros are useful for sequence access methods that hold their data
 * on a single page, like the in-core "local" method.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/sequence_page.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SEQUENCE_PAGE_H
#define SEQUENCE_PAGE_H

/*
 * Initialize the first page of a sequence relation.  This embeds the
 * handling for the special magic number, and enforces a frozen XID,
 * for VACUUM.
 *
 * Since VACUUM does not process sequences, we have to force the tuple to
 * have xmin = FrozenTransactionId now.  Otherwise it would become
 * invisible to SELECTs after 2G transactions.  It is okay to do this
 * because if the current transaction aborts, no other xact will ever
 * examine the sequence tuple anyway.
 *
 * "seqam_special" is the structure used for the special area of a
 * sequence access method.
 * "seqam_magic_value" is a value stored in the special area, used for
 * the validation of the page.
 */
#define SEQUENCE_PAGE_INIT(seqam_special, seqam_magic_value) \
do {																	\
	buf = ExtendBufferedRel(BMR_REL(rel), forkNum, NULL,				\
							EB_LOCK_FIRST | EB_SKIP_EXTENSION_LOCK);	\
	Assert(BufferGetBlockNumber(buf) == 0);								\
																		\
	page = BufferGetPage(buf);											\
																		\
	PageInit(page, BufferGetPageSize(buf), sizeof(seqam_special));		\
	sm = (seqam_special *) PageGetSpecialPointer(page);					\
	sm->magic = seqam_magic_value;										\
																		\
	/* Now insert sequence tuple */										\
	HeapTupleHeaderSetXmin(tuple->t_data, FrozenTransactionId);			\
	HeapTupleHeaderSetXminFrozen(tuple->t_data);						\
	HeapTupleHeaderSetCmin(tuple->t_data, FirstCommandId);				\
	HeapTupleHeaderSetXmax(tuple->t_data, InvalidTransactionId);		\
	tuple->t_data->t_infomask |= HEAP_XMAX_INVALID;						\
	ItemPointerSet(&tuple->t_data->t_ctid, 0, FirstOffsetNumber);		\
} while(0)


/*
 * Read the first page of a sequence relation, previously initialized with
 * SEQUENCE_PAGE_INIT.
 *
 * "Form_seqam_data" is the data retrieved from the page.
 * "seqam_special" is the structure used for the special area of a
 * sequence access method.
 * "seqam_magic_value" is a value stored in the special area, used for
 * the validation of the page.
 */
#define SEQUENCE_PAGE_READ(Form_seqam_data, seqam_special, seqam_magic_value) \
do {																	\
	Page		page;													\
	ItemId		lp;														\
																		\
	*buf = ReadBuffer(rel, 0);											\
	LockBuffer(*buf, BUFFER_LOCK_EXCLUSIVE);							\
																		\
	page = BufferGetPage(*buf);											\
	sm = (seqam_special *) PageGetSpecialPointer(page);					\
																		\
	if (sm->magic != seqam_magic_value)									\
		elog(ERROR, "bad magic number in sequence \"%s\": %08X",		\
			 RelationGetRelationName(rel), sm->magic);					\
																		\
	lp = PageGetItemId(page, FirstOffsetNumber);						\
	Assert(ItemIdIsNormal(lp));											\
																		\
	/*																	\
	 * Note we currently only bother to set these two fields of			\
	 * *seqdatatuple.													\
	 */																	\
	seqdatatuple->t_data = (HeapTupleHeader) PageGetItem(page, lp);		\
	seqdatatuple->t_len = ItemIdGetLength(lp);							\
																		\
	seq = (Form_seqam_data) GETSTRUCT(seqdatatuple);					\
} while(0)

#endif							/* SEQUENCE_PAGE_H */
