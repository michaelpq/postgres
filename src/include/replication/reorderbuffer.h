/*
 * reorderbuffer.h
 *	  PostgreSQL logical replay/reorder buffer management.
 *
 * Copyright (c) 2012-2025, PostgreSQL Global Development Group
 *
 * src/include/replication/reorderbuffer.h
 */
#ifndef REORDERBUFFER_H
#define REORDERBUFFER_H

#include "access/htup_details.h"
#include "lib/ilist.h"
#include "lib/pairingheap.h"
#include "storage/sinval.h"
#include "utils/hsearch.h"
#include "utils/relcache.h"
#include "utils/snapshot.h"
#include "utils/timestamp.h"

/* paths for logical decoding data (relative to installation's $PGDATA) */
#define PG_LOGICAL_DIR				"pg_logical"
#define PG_LOGICAL_MAPPINGS_DIR		PG_LOGICAL_DIR "/mappings"
#define PG_LOGICAL_SNAPSHOTS_DIR	PG_LOGICAL_DIR "/snapshots"

/* GUC variables */
extern PGDLLIMPORT int logical_decoding_work_mem;
extern PGDLLIMPORT int debug_logical_replication_streaming;

/* possible values for debug_logical_replication_streaming */
typedef enum
{
	DEBUG_LOGICAL_REP_STREAMING_BUFFERED,
	DEBUG_LOGICAL_REP_STREAMING_IMMEDIATE,
}			DebugLogicalRepStreamingMode;

/*
 * Types of the change passed to a 'change' callback.
 *
 * For efficiency and simplicity reasons we want to keep Snapshots, CommandIds
 * and ComboCids in the same list with the user visible INSERT/UPDATE/DELETE
 * changes. Users of the decoding facilities will never see changes with
 * *_INTERNAL_* actions.
 *
 * The INTERNAL_SPEC_INSERT and INTERNAL_SPEC_CONFIRM, and INTERNAL_SPEC_ABORT
 * changes concern "speculative insertions", their confirmation, and abort
 * respectively.  They're used by INSERT .. ON CONFLICT .. UPDATE.  Users of
 * logical decoding don't have to care about these.
 */
typedef enum ReorderBufferChangeType
{
	REORDER_BUFFER_CHANGE_INSERT,
	REORDER_BUFFER_CHANGE_UPDATE,
	REORDER_BUFFER_CHANGE_DELETE,
	REORDER_BUFFER_CHANGE_MESSAGE,
	REORDER_BUFFER_CHANGE_INVALIDATION,
	REORDER_BUFFER_CHANGE_INTERNAL_SNAPSHOT,
	REORDER_BUFFER_CHANGE_INTERNAL_COMMAND_ID,
	REORDER_BUFFER_CHANGE_INTERNAL_TUPLECID,
	REORDER_BUFFER_CHANGE_INTERNAL_SPEC_INSERT,
	REORDER_BUFFER_CHANGE_INTERNAL_SPEC_CONFIRM,
	REORDER_BUFFER_CHANGE_INTERNAL_SPEC_ABORT,
	REORDER_BUFFER_CHANGE_TRUNCATE,
} ReorderBufferChangeType;

/* forward declaration */
struct ReorderBufferTXN;

/*
 * a single 'change', can be an insert (with one tuple), an update (old, new),
 * or a delete (old).
 *
 * The same struct is also used internally for other purposes but that should
 * never be visible outside reorderbuffer.c.
 */
typedef struct ReorderBufferChange
{
	XLogRecPtr	lsn;

	/* The type of change. */
	ReorderBufferChangeType action;

	/* Transaction this change belongs to. */
	struct ReorderBufferTXN *txn;

	RepOriginId origin_id;

	/*
	 * Context data for the change. Which part of the union is valid depends
	 * on action.
	 */
	union
	{
		/* Old, new tuples when action == *_INSERT|UPDATE|DELETE */
		struct
		{
			/* relation that has been changed */
			RelFileLocator rlocator;

			/* no previously reassembled toast chunks are necessary anymore */
			bool		clear_toast_afterwards;

			/* valid for DELETE || UPDATE */
			HeapTuple	oldtuple;
			/* valid for INSERT || UPDATE */
			HeapTuple	newtuple;
		}			tp;

		/*
		 * Truncate data for REORDER_BUFFER_CHANGE_TRUNCATE representing one
		 * set of relations to be truncated.
		 */
		struct
		{
			Size		nrelids;
			bool		cascade;
			bool		restart_seqs;
			Oid		   *relids;
		}			truncate;

		/* Message with arbitrary data. */
		struct
		{
			char	   *prefix;
			Size		message_size;
			char	   *message;
		}			msg;

		/* New snapshot, set when action == *_INTERNAL_SNAPSHOT */
		Snapshot	snapshot;

		/*
		 * New command id for existing snapshot in a catalog changing tx. Set
		 * when action == *_INTERNAL_COMMAND_ID.
		 */
		CommandId	command_id;

		/*
		 * New cid mapping for catalog changing transaction, set when action
		 * == *_INTERNAL_TUPLECID.
		 */
		struct
		{
			RelFileLocator locator;
			ItemPointerData tid;
			CommandId	cmin;
			CommandId	cmax;
			CommandId	combocid;
		}			tuplecid;

		/* Invalidation. */
		struct
		{
			uint32		ninvalidations; /* Number of messages */
			SharedInvalidationMessage *invalidations;	/* invalidation message */
		}			inval;
	}			data;

	/*
	 * While in use this is how a change is linked into a transactions,
	 * otherwise it's the preallocated list.
	 */
	dlist_node	node;
} ReorderBufferChange;

/* ReorderBufferTXN txn_flags */
#define RBTXN_HAS_CATALOG_CHANGES 	0x0001
#define RBTXN_IS_SUBXACT          	0x0002
#define RBTXN_IS_SERIALIZED       	0x0004
#define RBTXN_IS_SERIALIZED_CLEAR 	0x0008
#define RBTXN_IS_STREAMED         	0x0010
#define RBTXN_HAS_PARTIAL_CHANGE  	0x0020
#define RBTXN_IS_PREPARED 			0x0040
#define RBTXN_SKIPPED_PREPARE	  	0x0080
#define RBTXN_HAS_STREAMABLE_CHANGE	0x0100
#define RBTXN_SENT_PREPARE			0x0200
#define RBTXN_IS_COMMITTED			0x0400
#define RBTXN_IS_ABORTED			0x0800
#define RBTXN_DISTR_INVAL_OVERFLOWED	0x1000

#define RBTXN_PREPARE_STATUS_MASK	(RBTXN_IS_PREPARED | RBTXN_SKIPPED_PREPARE | RBTXN_SENT_PREPARE)

/* Does the transaction have catalog changes? */
#define rbtxn_has_catalog_changes(txn) \
( \
	 ((txn)->txn_flags & RBTXN_HAS_CATALOG_CHANGES) != 0 \
)

/* Is the transaction known as a subxact? */
#define rbtxn_is_known_subxact(txn) \
( \
	((txn)->txn_flags & RBTXN_IS_SUBXACT) != 0 \
)

/* Has this transaction been spilled to disk? */
#define rbtxn_is_serialized(txn) \
( \
	((txn)->txn_flags & RBTXN_IS_SERIALIZED) != 0 \
)

/* Has this transaction ever been spilled to disk? */
#define rbtxn_is_serialized_clear(txn) \
( \
	((txn)->txn_flags & RBTXN_IS_SERIALIZED_CLEAR) != 0 \
)

/* Does this transaction contain partial changes? */
#define rbtxn_has_partial_change(txn) \
( \
	((txn)->txn_flags & RBTXN_HAS_PARTIAL_CHANGE) != 0 \
)

/* Does this transaction contain streamable changes? */
#define rbtxn_has_streamable_change(txn) \
( \
	((txn)->txn_flags & RBTXN_HAS_STREAMABLE_CHANGE) != 0 \
)

/*
 * Has this transaction been streamed to downstream?
 *
 * (It's not possible to deduce this from nentries and nentries_mem for
 * various reasons. For example, all changes may be in subtransactions in
 * which case we'd have nentries==0 for the toplevel one, which would say
 * nothing about the streaming. So we maintain this flag, but only for the
 * toplevel transaction.)
 */
#define rbtxn_is_streamed(txn) \
( \
	((txn)->txn_flags & RBTXN_IS_STREAMED) != 0 \
)

/*
 * Is this a prepared transaction?
 *
 * Being true means that this transaction should be prepared instead of
 * committed. To check whether a prepare or a stream_prepare has already
 * been sent for this transaction, we need to use rbtxn_sent_prepare().
 */
#define rbtxn_is_prepared(txn) \
( \
	((txn)->txn_flags & RBTXN_IS_PREPARED) != 0 \
)

/* Has a prepare or stream_prepare already been sent? */
#define rbtxn_sent_prepare(txn) \
( \
	((txn)->txn_flags & RBTXN_SENT_PREPARE) != 0 \
)

/* Is this transaction committed? */
#define rbtxn_is_committed(txn) \
( \
	((txn)->txn_flags & RBTXN_IS_COMMITTED) != 0 \
)

/* Is this transaction aborted? */
#define rbtxn_is_aborted(txn) \
( \
	((txn)->txn_flags & RBTXN_IS_ABORTED) != 0 \
)

/* prepare for this transaction skipped? */
#define rbtxn_skip_prepared(txn) \
( \
	((txn)->txn_flags & RBTXN_SKIPPED_PREPARE) != 0 \
)

/* Is the array of distributed inval messages overflowed? */
#define rbtxn_distr_inval_overflowed(txn) \
( \
	((txn)->txn_flags & RBTXN_DISTR_INVAL_OVERFLOWED) != 0 \
)

/* Is this a top-level transaction? */
#define rbtxn_is_toptxn(txn) \
( \
	(txn)->toptxn == NULL \
)

/* Is this a subtransaction? */
#define rbtxn_is_subtxn(txn) \
( \
	(txn)->toptxn != NULL \
)

/* Get the top-level transaction of this (sub)transaction. */
#define rbtxn_get_toptxn(txn) \
( \
	rbtxn_is_subtxn(txn) ? (txn)->toptxn : (txn) \
)

typedef struct ReorderBufferTXN
{
	/* See above */
	bits32		txn_flags;

	/* The transaction's transaction id, can be a toplevel or sub xid. */
	TransactionId xid;

	/* Xid of top-level transaction, if known */
	TransactionId toplevel_xid;

	/*
	 * Global transaction id required for identification of prepared
	 * transactions.
	 */
	char	   *gid;

	/*
	 * LSN of the first data carrying, WAL record with knowledge about this
	 * xid. This is allowed to *not* be first record adorned with this xid, if
	 * the previous records aren't relevant for logical decoding.
	 */
	XLogRecPtr	first_lsn;

	/* ----
	 * LSN of the record that lead to this xact to be prepared or committed or
	 * aborted. This can be a
	 * * plain commit record
	 * * plain commit record, of a parent transaction
	 * * prepared transaction
	 * * prepared transaction commit
	 * * plain abort record
	 * * prepared transaction abort
	 *
	 * This can also become set to earlier values than transaction end when
	 * a transaction is spilled to disk; specifically it's set to the LSN of
	 * the latest change written to disk so far.
	 * ----
	 */
	XLogRecPtr	final_lsn;

	/*
	 * LSN pointing to the end of the commit record + 1.
	 */
	XLogRecPtr	end_lsn;

	/* Toplevel transaction for this subxact (NULL for top-level). */
	struct ReorderBufferTXN *toptxn;

	/*
	 * LSN of the last lsn at which snapshot information reside, so we can
	 * restart decoding from there and fully recover this transaction from
	 * WAL.
	 */
	XLogRecPtr	restart_decoding_lsn;

	/* origin of the change that caused this transaction */
	RepOriginId origin_id;
	XLogRecPtr	origin_lsn;

	/*
	 * Commit or Prepare time, only known when we read the actual commit or
	 * prepare record.
	 */
	union
	{
		TimestampTz commit_time;
		TimestampTz prepare_time;
		TimestampTz abort_time;
	}			xact_time;

	/*
	 * The base snapshot is used to decode all changes until either this
	 * transaction modifies the catalog, or another catalog-modifying
	 * transaction commits.
	 */
	Snapshot	base_snapshot;
	XLogRecPtr	base_snapshot_lsn;
	dlist_node	base_snapshot_node; /* link in txns_by_base_snapshot_lsn */

	/*
	 * Snapshot/CID from the previous streaming run. Only valid for already
	 * streamed transactions (NULL/InvalidCommandId otherwise).
	 */
	Snapshot	snapshot_now;
	CommandId	command_id;

	/*
	 * How many ReorderBufferChange's do we have in this txn.
	 *
	 * Changes in subtransactions are *not* included but tracked separately.
	 */
	uint64		nentries;

	/*
	 * How many of the above entries are stored in memory in contrast to being
	 * spilled to disk.
	 */
	uint64		nentries_mem;

	/*
	 * List of ReorderBufferChange structs, including new Snapshots, new
	 * CommandIds and command invalidation messages.
	 */
	dlist_head	changes;

	/*
	 * List of (relation, ctid) => (cmin, cmax) mappings for catalog tuples.
	 * Those are always assigned to the toplevel transaction. (Keep track of
	 * #entries to create a hash of the right size)
	 */
	dlist_head	tuplecids;
	uint64		ntuplecids;

	/*
	 * On-demand built hash for looking up the above values.
	 */
	HTAB	   *tuplecid_hash;

	/*
	 * Hash containing (potentially partial) toast entries. NULL if no toast
	 * tuples have been found for the current change.
	 */
	HTAB	   *toast_hash;

	/*
	 * non-hierarchical list of subtransactions that are *not* aborted. Only
	 * used in toplevel transactions.
	 */
	dlist_head	subtxns;
	uint32		nsubtxns;

	/*
	 * Stored cache invalidations. This is not a linked list because we get
	 * all the invalidations at once.
	 */
	uint32		ninvalidations;
	SharedInvalidationMessage *invalidations;

	/*
	 * Stores cache invalidation messages distributed by other transactions.
	 */
	uint32		ninvalidations_distributed;
	SharedInvalidationMessage *invalidations_distributed;

	/* ---
	 * Position in one of two lists:
	 * * list of subtransactions if we are *known* to be subxact
	 * * list of toplevel xacts (can be an as-yet unknown subxact)
	 * ---
	 */
	dlist_node	node;

	/*
	 * A node in the list of catalog modifying transactions
	 */
	dlist_node	catchange_node;

	/*
	 * A node in txn_heap
	 */
	pairingheap_node txn_node;

	/*
	 * Size of this transaction (changes currently in memory, in bytes).
	 */
	Size		size;

	/* Size of top-transaction including sub-transactions. */
	Size		total_size;

	/*
	 * Private data pointer of the output plugin.
	 */
	void	   *output_plugin_private;
} ReorderBufferTXN;

/* so we can define the callbacks used inside struct ReorderBuffer itself */
typedef struct ReorderBuffer ReorderBuffer;

/* change callback signature */
typedef void (*ReorderBufferApplyChangeCB) (ReorderBuffer *rb,
											ReorderBufferTXN *txn,
											Relation relation,
											ReorderBufferChange *change);

/* truncate callback signature */
typedef void (*ReorderBufferApplyTruncateCB) (ReorderBuffer *rb,
											  ReorderBufferTXN *txn,
											  int nrelations,
											  Relation relations[],
											  ReorderBufferChange *change);

/* begin callback signature */
typedef void (*ReorderBufferBeginCB) (ReorderBuffer *rb,
									  ReorderBufferTXN *txn);

/* commit callback signature */
typedef void (*ReorderBufferCommitCB) (ReorderBuffer *rb,
									   ReorderBufferTXN *txn,
									   XLogRecPtr commit_lsn);

/* message callback signature */
typedef void (*ReorderBufferMessageCB) (ReorderBuffer *rb,
										ReorderBufferTXN *txn,
										XLogRecPtr message_lsn,
										bool transactional,
										const char *prefix, Size sz,
										const char *message);

/* begin prepare callback signature */
typedef void (*ReorderBufferBeginPrepareCB) (ReorderBuffer *rb,
											 ReorderBufferTXN *txn);

/* prepare callback signature */
typedef void (*ReorderBufferPrepareCB) (ReorderBuffer *rb,
										ReorderBufferTXN *txn,
										XLogRecPtr prepare_lsn);

/* commit prepared callback signature */
typedef void (*ReorderBufferCommitPreparedCB) (ReorderBuffer *rb,
											   ReorderBufferTXN *txn,
											   XLogRecPtr commit_lsn);

/* rollback  prepared callback signature */
typedef void (*ReorderBufferRollbackPreparedCB) (ReorderBuffer *rb,
												 ReorderBufferTXN *txn,
												 XLogRecPtr prepare_end_lsn,
												 TimestampTz prepare_time);

/* start streaming transaction callback signature */
typedef void (*ReorderBufferStreamStartCB) (ReorderBuffer *rb,
											ReorderBufferTXN *txn,
											XLogRecPtr first_lsn);

/* stop streaming transaction callback signature */
typedef void (*ReorderBufferStreamStopCB) (ReorderBuffer *rb,
										   ReorderBufferTXN *txn,
										   XLogRecPtr last_lsn);

/* discard streamed transaction callback signature */
typedef void (*ReorderBufferStreamAbortCB) (ReorderBuffer *rb,
											ReorderBufferTXN *txn,
											XLogRecPtr abort_lsn);

/* prepare streamed transaction callback signature */
typedef void (*ReorderBufferStreamPrepareCB) (ReorderBuffer *rb,
											  ReorderBufferTXN *txn,
											  XLogRecPtr prepare_lsn);

/* commit streamed transaction callback signature */
typedef void (*ReorderBufferStreamCommitCB) (ReorderBuffer *rb,
											 ReorderBufferTXN *txn,
											 XLogRecPtr commit_lsn);

/* stream change callback signature */
typedef void (*ReorderBufferStreamChangeCB) (ReorderBuffer *rb,
											 ReorderBufferTXN *txn,
											 Relation relation,
											 ReorderBufferChange *change);

/* stream message callback signature */
typedef void (*ReorderBufferStreamMessageCB) (ReorderBuffer *rb,
											  ReorderBufferTXN *txn,
											  XLogRecPtr message_lsn,
											  bool transactional,
											  const char *prefix, Size sz,
											  const char *message);

/* stream truncate callback signature */
typedef void (*ReorderBufferStreamTruncateCB) (ReorderBuffer *rb,
											   ReorderBufferTXN *txn,
											   int nrelations,
											   Relation relations[],
											   ReorderBufferChange *change);

/* update progress txn callback signature */
typedef void (*ReorderBufferUpdateProgressTxnCB) (ReorderBuffer *rb,
												  ReorderBufferTXN *txn,
												  XLogRecPtr lsn);

struct ReorderBuffer
{
	/*
	 * xid => ReorderBufferTXN lookup table
	 */
	HTAB	   *by_txn;

	/*
	 * Transactions that could be a toplevel xact, ordered by LSN of the first
	 * record bearing that xid.
	 */
	dlist_head	toplevel_by_lsn;

	/*
	 * Transactions and subtransactions that have a base snapshot, ordered by
	 * LSN of the record which caused us to first obtain the base snapshot.
	 * This is not the same as toplevel_by_lsn, because we only set the base
	 * snapshot on the first logical-decoding-relevant record (eg. heap
	 * writes), whereas the initial LSN could be set by other operations.
	 */
	dlist_head	txns_by_base_snapshot_lsn;

	/*
	 * Transactions and subtransactions that have modified system catalogs.
	 */
	dclist_head catchange_txns;

	/*
	 * one-entry sized cache for by_txn. Very frequently the same txn gets
	 * looked up over and over again.
	 */
	TransactionId by_txn_last_xid;
	ReorderBufferTXN *by_txn_last_txn;

	/*
	 * Callbacks to be called when a transactions commits.
	 */
	ReorderBufferBeginCB begin;
	ReorderBufferApplyChangeCB apply_change;
	ReorderBufferApplyTruncateCB apply_truncate;
	ReorderBufferCommitCB commit;
	ReorderBufferMessageCB message;

	/*
	 * Callbacks to be called when streaming a transaction at prepare time.
	 */
	ReorderBufferBeginCB begin_prepare;
	ReorderBufferPrepareCB prepare;
	ReorderBufferCommitPreparedCB commit_prepared;
	ReorderBufferRollbackPreparedCB rollback_prepared;

	/*
	 * Callbacks to be called when streaming a transaction.
	 */
	ReorderBufferStreamStartCB stream_start;
	ReorderBufferStreamStopCB stream_stop;
	ReorderBufferStreamAbortCB stream_abort;
	ReorderBufferStreamPrepareCB stream_prepare;
	ReorderBufferStreamCommitCB stream_commit;
	ReorderBufferStreamChangeCB stream_change;
	ReorderBufferStreamMessageCB stream_message;
	ReorderBufferStreamTruncateCB stream_truncate;

	/*
	 * Callback to be called when updating progress during sending data of a
	 * transaction (and its subtransactions) to the output plugin.
	 */
	ReorderBufferUpdateProgressTxnCB update_progress_txn;

	/*
	 * Pointer that will be passed untouched to the callbacks.
	 */
	void	   *private_data;

	/*
	 * Saved output plugin option
	 */
	bool		output_rewrites;

	/*
	 * Private memory context.
	 */
	MemoryContext context;

	/*
	 * Memory contexts for specific types objects
	 */
	MemoryContext change_context;
	MemoryContext txn_context;
	MemoryContext tup_context;

	XLogRecPtr	current_restart_decoding_lsn;

	/* buffer for disk<->memory conversions */
	char	   *outbuf;
	Size		outbufsize;

	/* memory accounting */
	Size		size;

	/* Max-heap for sizes of all top-level and sub transactions */
	pairingheap *txn_heap;

	/*
	 * Statistics about transactions spilled to disk.
	 *
	 * A single transaction may be spilled repeatedly, which is why we keep
	 * two different counters. For spilling, the transaction counter includes
	 * both toplevel transactions and subtransactions.
	 */
	int64		spillTxns;		/* number of transactions spilled to disk */
	int64		spillCount;		/* spill-to-disk invocation counter */
	int64		spillBytes;		/* amount of data spilled to disk */

	/* Statistics about transactions streamed to the decoding output plugin */
	int64		streamTxns;		/* number of transactions streamed */
	int64		streamCount;	/* streaming invocation counter */
	int64		streamBytes;	/* amount of data decoded */

	/*
	 * Statistics about all the transactions sent to the decoding output
	 * plugin
	 */
	int64		totalTxns;		/* total number of transactions sent */
	int64		totalBytes;		/* total amount of data decoded */
};


extern ReorderBuffer *ReorderBufferAllocate(void);
extern void ReorderBufferFree(ReorderBuffer *rb);

extern HeapTuple ReorderBufferAllocTupleBuf(ReorderBuffer *rb, Size tuple_len);
extern void ReorderBufferFreeTupleBuf(HeapTuple tuple);

extern ReorderBufferChange *ReorderBufferAllocChange(ReorderBuffer *rb);
extern void ReorderBufferFreeChange(ReorderBuffer *rb,
									ReorderBufferChange *change, bool upd_mem);

extern Oid *ReorderBufferAllocRelids(ReorderBuffer *rb, int nrelids);
extern void ReorderBufferFreeRelids(ReorderBuffer *rb, Oid *relids);

extern void ReorderBufferQueueChange(ReorderBuffer *rb, TransactionId xid,
									 XLogRecPtr lsn, ReorderBufferChange *change,
									 bool toast_insert);
extern void ReorderBufferQueueMessage(ReorderBuffer *rb, TransactionId xid,
									  Snapshot snap, XLogRecPtr lsn,
									  bool transactional, const char *prefix,
									  Size message_size, const char *message);
extern void ReorderBufferCommit(ReorderBuffer *rb, TransactionId xid,
								XLogRecPtr commit_lsn, XLogRecPtr end_lsn,
								TimestampTz commit_time, RepOriginId origin_id, XLogRecPtr origin_lsn);
extern void ReorderBufferFinishPrepared(ReorderBuffer *rb, TransactionId xid,
										XLogRecPtr commit_lsn, XLogRecPtr end_lsn,
										XLogRecPtr two_phase_at,
										TimestampTz commit_time,
										RepOriginId origin_id, XLogRecPtr origin_lsn,
										char *gid, bool is_commit);
extern void ReorderBufferAssignChild(ReorderBuffer *rb, TransactionId xid,
									 TransactionId subxid, XLogRecPtr lsn);
extern void ReorderBufferCommitChild(ReorderBuffer *rb, TransactionId xid,
									 TransactionId subxid, XLogRecPtr commit_lsn,
									 XLogRecPtr end_lsn);
extern void ReorderBufferAbort(ReorderBuffer *rb, TransactionId xid, XLogRecPtr lsn,
							   TimestampTz abort_time);
extern void ReorderBufferAbortOld(ReorderBuffer *rb, TransactionId oldestRunningXid);
extern void ReorderBufferForget(ReorderBuffer *rb, TransactionId xid, XLogRecPtr lsn);
extern void ReorderBufferInvalidate(ReorderBuffer *rb, TransactionId xid, XLogRecPtr lsn);

extern void ReorderBufferSetBaseSnapshot(ReorderBuffer *rb, TransactionId xid,
										 XLogRecPtr lsn, Snapshot snap);
extern void ReorderBufferAddSnapshot(ReorderBuffer *rb, TransactionId xid,
									 XLogRecPtr lsn, Snapshot snap);
extern void ReorderBufferAddNewCommandId(ReorderBuffer *rb, TransactionId xid,
										 XLogRecPtr lsn, CommandId cid);
extern void ReorderBufferAddNewTupleCids(ReorderBuffer *rb, TransactionId xid,
										 XLogRecPtr lsn, RelFileLocator locator,
										 ItemPointerData tid,
										 CommandId cmin, CommandId cmax, CommandId combocid);
extern void ReorderBufferAddInvalidations(ReorderBuffer *rb, TransactionId xid, XLogRecPtr lsn,
										  Size nmsgs, SharedInvalidationMessage *msgs);
extern void ReorderBufferAddDistributedInvalidations(ReorderBuffer *rb, TransactionId xid,
													 XLogRecPtr lsn, Size nmsgs,
													 SharedInvalidationMessage *msgs);
extern void ReorderBufferImmediateInvalidation(ReorderBuffer *rb, uint32 ninvalidations,
											   SharedInvalidationMessage *invalidations);
extern void ReorderBufferProcessXid(ReorderBuffer *rb, TransactionId xid, XLogRecPtr lsn);

extern void ReorderBufferXidSetCatalogChanges(ReorderBuffer *rb, TransactionId xid, XLogRecPtr lsn);
extern bool ReorderBufferXidHasCatalogChanges(ReorderBuffer *rb, TransactionId xid);
extern bool ReorderBufferXidHasBaseSnapshot(ReorderBuffer *rb, TransactionId xid);

extern bool ReorderBufferRememberPrepareInfo(ReorderBuffer *rb, TransactionId xid,
											 XLogRecPtr prepare_lsn, XLogRecPtr end_lsn,
											 TimestampTz prepare_time,
											 RepOriginId origin_id, XLogRecPtr origin_lsn);
extern void ReorderBufferSkipPrepare(ReorderBuffer *rb, TransactionId xid);
extern void ReorderBufferPrepare(ReorderBuffer *rb, TransactionId xid, char *gid);
extern ReorderBufferTXN *ReorderBufferGetOldestTXN(ReorderBuffer *rb);
extern TransactionId ReorderBufferGetOldestXmin(ReorderBuffer *rb);
extern TransactionId *ReorderBufferGetCatalogChangesXacts(ReorderBuffer *rb);

extern void ReorderBufferSetRestartPoint(ReorderBuffer *rb, XLogRecPtr ptr);

extern uint32 ReorderBufferGetInvalidations(ReorderBuffer *rb,
											TransactionId xid,
											SharedInvalidationMessage **msgs);

extern void StartupReorderBuffer(void);

#endif
