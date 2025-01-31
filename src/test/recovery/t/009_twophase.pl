
# Copyright (c) 2021, PostgreSQL Global Development Group

# Tests dedicated to two-phase commit in recovery
use strict;
use warnings;

use File::Copy;
use PostgresNode;
use TestLib;
use Test::More tests => 33;

my $psql_out = '';
my $psql_rc  = '';

sub configure_and_reload
{
	local $Test::Builder::Level = $Test::Builder::Level + 1;

	my ($node, $parameter) = @_;
	my $name = $node->name;

	$node->append_conf(
		'postgresql.conf', qq(
		$parameter
	));
	$node->psql('postgres', "SELECT pg_reload_conf()", stdout => \$psql_out);
	is($psql_out, 't', "reload node $name with $parameter");
	return;
}

sub twophase_file_name
{
	local $Test::Builder::Level = $Test::Builder::Level + 1;

	my $xid = shift;
	return sprintf("%08X", $xid);
}

# Set up two nodes, which will alternately be primary and replication standby.

# Setup london node
my $node_london = get_new_node("london");
$node_london->init(allows_streaming => 1);
$node_london->append_conf(
	'postgresql.conf', qq(
	max_prepared_transactions = 10
	log_checkpoints = true
));
$node_london->start;
$node_london->backup('london_backup');

# Setup paris node
my $node_paris = get_new_node('paris');
$node_paris->init_from_backup($node_london, 'london_backup',
	has_streaming => 1);
$node_paris->start;

# Switch to synchronous replication in both directions
configure_and_reload($node_london, "synchronous_standby_names = 'paris'");
configure_and_reload($node_paris,  "synchronous_standby_names = 'london'");

# Set up nonce names for current primary and standby nodes
note "Initially, london is primary and paris is standby";
my ($cur_primary, $cur_standby) = ($node_london, $node_paris);
my $cur_primary_name = $cur_primary->name;

# Create table we'll use in the test transactions
$cur_primary->psql('postgres', "CREATE TABLE t_009_tbl (id int, msg text)");

###############################################################################
# Check that we can commit and abort transaction after soft restart.
# Here checkpoint happens before shutdown and no WAL replay will occur at next
# startup. In this case postgres re-creates shared-memory state from twophase
# files.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (1, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (2, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_1';
	BEGIN;
	INSERT INTO t_009_tbl VALUES (3, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (4, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_2';");
$cur_primary->stop;
$cur_primary->start;

$psql_rc = $cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_1'");
is($psql_rc, '0', 'Commit prepared transaction after restart');

$psql_rc = $cur_primary->psql('postgres', "ROLLBACK PREPARED 'xact_009_2'");
is($psql_rc, '0', 'Rollback prepared transaction after restart');

###############################################################################
# Check that we can commit and abort after a hard restart.
# At next startup, WAL replay will re-create shared memory state for prepared
# transaction using dedicated WAL records.
###############################################################################

$cur_primary->psql(
	'postgres', "
	CHECKPOINT;
	BEGIN;
	INSERT INTO t_009_tbl VALUES (5, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (6, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_3';
	BEGIN;
	INSERT INTO t_009_tbl VALUES (7, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (8, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_4';");
$cur_primary->teardown_node;
$cur_primary->start;

$psql_rc = $cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_3'");
is($psql_rc, '0', 'Commit prepared transaction after teardown');

$psql_rc = $cur_primary->psql('postgres', "ROLLBACK PREPARED 'xact_009_4'");
is($psql_rc, '0', 'Rollback prepared transaction after teardown');

###############################################################################
# Check that WAL replay can handle several transactions with same GID name.
###############################################################################

$cur_primary->psql(
	'postgres', "
	CHECKPOINT;
	BEGIN;
	INSERT INTO t_009_tbl VALUES (9, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (10, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_5';
	COMMIT PREPARED 'xact_009_5';
	BEGIN;
	INSERT INTO t_009_tbl VALUES (11, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (12, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_5';");
$cur_primary->teardown_node;
$cur_primary->start;

$psql_rc = $cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_5'");
is($psql_rc, '0', 'Replay several transactions with same GID');

###############################################################################
# Check that WAL replay cleans up its shared memory state and releases locks
# while replaying transaction commits.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (13, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (14, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_6';
	COMMIT PREPARED 'xact_009_6';");
$cur_primary->teardown_node;
$cur_primary->start;
$psql_rc = $cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (15, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (16, 'issued to ${cur_primary_name}');
	-- This prepare can fail due to conflicting GID or locks conflicts if
	-- replay did not fully cleanup its state on previous commit.
	PREPARE TRANSACTION 'xact_009_7';");
is($psql_rc, '0', "Cleanup of shared memory state for 2PC commit");

$cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_7'");

###############################################################################
# Check that WAL replay will cleanup its shared memory state on running standby.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (17, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (18, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_8';
	COMMIT PREPARED 'xact_009_8';");
$cur_standby->psql(
	'postgres',
	"SELECT count(*) FROM pg_prepared_xacts",
	stdout => \$psql_out);
is($psql_out, '0',
	"Cleanup of shared memory state on running standby without checkpoint");

###############################################################################
# Same as in previous case, but let's force checkpoint on standby between
# prepare and commit to use on-disk twophase files.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (19, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (20, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_9';");
$cur_standby->psql('postgres', "CHECKPOINT");
$cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_9'");
$cur_standby->psql(
	'postgres',
	"SELECT count(*) FROM pg_prepared_xacts",
	stdout => \$psql_out);
is($psql_out, '0',
	"Cleanup of shared memory state on running standby after checkpoint");

###############################################################################
# Check that prepared transactions can be committed on promoted standby.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (21, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (22, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_10';");
$cur_primary->teardown_node;
$cur_standby->promote;

# change roles
note "Now paris is primary and london is standby";
($cur_primary, $cur_standby) = ($node_paris, $node_london);
$cur_primary_name = $cur_primary->name;

# because london is not running at this point, we can't use syncrep commit
# on this command
$psql_rc = $cur_primary->psql('postgres',
	"SET synchronous_commit = off; COMMIT PREPARED 'xact_009_10'");
is($psql_rc, '0', "Restore of prepared transaction on promoted standby");

# restart old primary as new standby
$cur_standby->enable_streaming($cur_primary);
$cur_standby->start;

###############################################################################
# Check that prepared transactions are replayed after soft restart of standby
# while primary is down. Since standby knows that primary is down it uses a
# different code path on startup to ensure that the status of transactions is
# consistent.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (23, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (24, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_11';");
$cur_primary->stop;
$cur_standby->restart;
$cur_standby->promote;

# change roles
note "Now london is primary and paris is standby";
($cur_primary, $cur_standby) = ($node_london, $node_paris);
$cur_primary_name = $cur_primary->name;

$cur_primary->psql(
	'postgres',
	"SELECT count(*) FROM pg_prepared_xacts",
	stdout => \$psql_out);
is($psql_out, '1',
	"Restore prepared transactions from files with primary down");

# restart old primary as new standby
$cur_standby->enable_streaming($cur_primary);
$cur_standby->start;

$cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_11'");

###############################################################################
# Check that prepared transactions are correctly replayed after standby hard
# restart while primary is down.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (25, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl VALUES (26, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_12';
	");
$cur_primary->stop;
$cur_standby->teardown_node;
$cur_standby->start;
$cur_standby->promote;

# change roles
note "Now paris is primary and london is standby";
($cur_primary, $cur_standby) = ($node_paris, $node_london);
$cur_primary_name = $cur_primary->name;

$cur_primary->psql(
	'postgres',
	"SELECT count(*) FROM pg_prepared_xacts",
	stdout => \$psql_out);
is($psql_out, '1',
	"Restore prepared transactions from records with primary down");

# restart old primary as new standby
$cur_standby->enable_streaming($cur_primary);
$cur_standby->start;

$cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_12'");

###############################################################################
# Check visibility of prepared transactions in standby after a restart while
# primary is down.
###############################################################################

$cur_primary->psql(
	'postgres', "
	SET synchronous_commit='remote_apply'; -- To ensure the standby is caught up
	CREATE TABLE t_009_tbl_standby_mvcc (id int, msg text);
	BEGIN;
	INSERT INTO t_009_tbl_standby_mvcc VALUES (1, 'issued to ${cur_primary_name}');
	SAVEPOINT s1;
	INSERT INTO t_009_tbl_standby_mvcc VALUES (2, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_standby_mvcc';
	");
$cur_primary->stop;
$cur_standby->restart;

# Acquire a snapshot in standby, before we commit the prepared transaction
my $standby_session = $cur_standby->background_psql('postgres', on_error_die => 1);
$standby_session->query_safe("BEGIN ISOLATION LEVEL REPEATABLE READ");
$psql_out = $standby_session->query_safe(
	"SELECT count(*) FROM t_009_tbl_standby_mvcc");
is($psql_out, '0',
	"Prepared transaction not visible in standby before commit");

# Commit the transaction in primary
$cur_primary->start;
$cur_primary->psql('postgres', "
SET synchronous_commit='remote_apply'; -- To ensure the standby is caught up
COMMIT PREPARED 'xact_009_standby_mvcc';
");

# Still not visible to the old snapshot
$psql_out = $standby_session->query_safe(
	"SELECT count(*) FROM t_009_tbl_standby_mvcc");
is($psql_out, '0',
	"Committed prepared transaction not visible to old snapshot in standby");

# Is visible to a new snapshot
$standby_session->query_safe("COMMIT");
$psql_out = $standby_session->query_safe(
	"SELECT count(*) FROM t_009_tbl_standby_mvcc");
is($psql_out, '2',
   "Committed prepared transaction is visible to new snapshot in standby");
$standby_session->quit;

###############################################################################
# Check for a lock conflict between prepared transaction with DDL inside and
# replay of XLOG_STANDBY_LOCK wal record.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	CREATE TABLE t_009_tbl2 (id int, msg text);
	SAVEPOINT s1;
	INSERT INTO t_009_tbl2 VALUES (27, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_13';
	-- checkpoint will issue XLOG_STANDBY_LOCK that can conflict with lock
	-- held by 'create table' statement
	CHECKPOINT;
	COMMIT PREPARED 'xact_009_13';");

# Ensure that last transaction is replayed on standby.
my $cur_primary_lsn =
  $cur_primary->safe_psql('postgres', "SELECT pg_current_wal_lsn()");
my $caughtup_query =
  "SELECT '$cur_primary_lsn'::pg_lsn <= pg_last_wal_replay_lsn()";
$cur_standby->poll_query_until('postgres', $caughtup_query)
  or die "Timed out while waiting for standby to catch up";

$cur_standby->psql(
	'postgres',
	"SELECT count(*) FROM t_009_tbl2",
	stdout => \$psql_out);
is($psql_out, '1', "Replay prepared transaction with DDL");

###############################################################################
# Check recovery of prepared transaction with DDL inside after a hard restart
# of the primary.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	CREATE TABLE t_009_tbl3 (id int, msg text);
	SAVEPOINT s1;
	INSERT INTO t_009_tbl3 VALUES (28, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_14';
	BEGIN;
	CREATE TABLE t_009_tbl4 (id int, msg text);
	SAVEPOINT s1;
	INSERT INTO t_009_tbl4 VALUES (29, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_15';");

$cur_primary->teardown_node;
$cur_primary->start;

$psql_rc = $cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_14'");
is($psql_rc, '0', 'Commit prepared transaction after teardown');

$psql_rc = $cur_primary->psql('postgres', "ROLLBACK PREPARED 'xact_009_15'");
is($psql_rc, '0', 'Rollback prepared transaction after teardown');

###############################################################################
# Check recovery of prepared transaction with DDL inside after a soft restart
# of the primary.
###############################################################################

$cur_primary->psql(
	'postgres', "
	BEGIN;
	CREATE TABLE t_009_tbl5 (id int, msg text);
	SAVEPOINT s1;
	INSERT INTO t_009_tbl5 VALUES (30, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_16';
	BEGIN;
	CREATE TABLE t_009_tbl6 (id int, msg text);
	SAVEPOINT s1;
	INSERT INTO t_009_tbl6 VALUES (31, 'issued to ${cur_primary_name}');
	PREPARE TRANSACTION 'xact_009_17';");

$cur_primary->stop;
$cur_primary->start;

$psql_rc = $cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_16'");
is($psql_rc, '0', 'Commit prepared transaction after restart');

$psql_rc = $cur_primary->psql('postgres', "ROLLBACK PREPARED 'xact_009_17'");
is($psql_rc, '0', 'Rollback prepared transaction after restart');

###############################################################################
# Verify expected data appears on both servers.
###############################################################################

$cur_primary->psql(
	'postgres',
	"SELECT count(*) FROM pg_prepared_xacts",
	stdout => \$psql_out);
is($psql_out, '0', "No uncommitted prepared transactions on primary");

$cur_primary->psql(
	'postgres',
	"SELECT * FROM t_009_tbl ORDER BY id",
	stdout => \$psql_out);
is( $psql_out, qq{1|issued to london
2|issued to london
5|issued to london
6|issued to london
9|issued to london
10|issued to london
11|issued to london
12|issued to london
13|issued to london
14|issued to london
15|issued to london
16|issued to london
17|issued to london
18|issued to london
19|issued to london
20|issued to london
21|issued to london
22|issued to london
23|issued to paris
24|issued to paris
25|issued to london
26|issued to london},
	"Check expected t_009_tbl data on primary");

$cur_primary->psql(
	'postgres',
	"SELECT * FROM t_009_tbl2",
	stdout => \$psql_out);
is( $psql_out,
	qq{27|issued to paris},
	"Check expected t_009_tbl2 data on primary");

$cur_standby->psql(
	'postgres',
	"SELECT count(*) FROM pg_prepared_xacts",
	stdout => \$psql_out);
is($psql_out, '0', "No uncommitted prepared transactions on standby");

$cur_standby->psql(
	'postgres',
	"SELECT * FROM t_009_tbl ORDER BY id",
	stdout => \$psql_out);
is( $psql_out, qq{1|issued to london
2|issued to london
5|issued to london
6|issued to london
9|issued to london
10|issued to london
11|issued to london
12|issued to london
13|issued to london
14|issued to london
15|issued to london
16|issued to london
17|issued to london
18|issued to london
19|issued to london
20|issued to london
21|issued to london
22|issued to london
23|issued to paris
24|issued to paris
25|issued to london
26|issued to london},
	"Check expected t_009_tbl data on standby");

$cur_standby->psql(
	'postgres',
	"SELECT * FROM t_009_tbl2",
	stdout => \$psql_out);
is( $psql_out,
	qq{27|issued to paris},
	"Check expected t_009_tbl2 data on standby");

###############################################################################
# Check handling of already committed or aborted 2PC files at recovery.
# This test does a manual copy of 2PC files created in a running server,
# to cheaply emulate situations that could be found in base backups.
###############################################################################

# Issue a set of transactions that will be used for this portion of the test:
# - One transaction to hold on the minimum xid horizon at bay.
# - One transaction that will be found as already committed at recovery.
# - One transaction that will be fonnd as already rollbacked at recovery.
$cur_primary->psql(
	'postgres', "
	BEGIN;
	INSERT INTO t_009_tbl VALUES (40, 'transaction: xid horizon');
	PREPARE TRANSACTION 'xact_009_40';
	BEGIN;
	INSERT INTO t_009_tbl VALUES (41, 'transaction: commit-prepared');
	PREPARE TRANSACTION 'xact_009_41';
	BEGIN;
	INSERT INTO t_009_tbl VALUES (42, 'transaction: rollback-prepared');
	PREPARE TRANSACTION 'xact_009_42';");

# Issue a checkpoint, fixing the XID horizon based on the first transaction,
# flushing to disk the two files to use.
$cur_primary->psql('postgres', "CHECKPOINT");

# Get the transaction IDs of the ones to 2PC files to manipulate.
my $commit_prepared_xid = int(
	$cur_primary->safe_psql(
		'postgres',
		"SELECT transaction FROM pg_prepared_xacts WHERE gid = 'xact_009_41'")
);
my $abort_prepared_xid = int(
	$cur_primary->safe_psql(
		'postgres',
		"SELECT transaction FROM pg_prepared_xacts WHERE gid = 'xact_009_42'")
);

# Copy the two-phase files that will be put back later.
my $commit_prepared_name = twophase_file_name($commit_prepared_xid);
my $abort_prepared_name = twophase_file_name($abort_prepared_xid);

my $twophase_tmpdir = $PostgreSQL::Test::Utils::tmp_check . '/' . "2pc_files";
mkdir($twophase_tmpdir);
my $primary_twophase_folder = $cur_primary->data_dir . '/pg_twophase/';
copy("$primary_twophase_folder/$commit_prepared_name", $twophase_tmpdir);
copy("$primary_twophase_folder/$abort_prepared_name", $twophase_tmpdir);

# Issue abort/commit prepared.
$cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_41'");
$cur_primary->psql('postgres', "ROLLBACK PREPARED 'xact_009_42'");

# Again checkpoint, to advance the LSN past the point where the two previous
# transaction records would be replayed.
$cur_primary->psql('postgres', "CHECKPOINT");

# Take down node.
$cur_primary->teardown_node;

# Move back the two twophase files.
copy("$twophase_tmpdir/$commit_prepared_name", $primary_twophase_folder);
copy("$twophase_tmpdir/$abort_prepared_name", $primary_twophase_folder);

# Grab location in logs of primary
my $log_offset = -s $cur_primary->logfile;

# Start node and check that the two previous files are removed by checking the
# server logs, following the CLOG lookup done at the end of recovery.
$cur_primary->start;

$cur_primary->log_check(
	"two-phase files of committed transactions removed at recovery",
	$log_offset,
	log_like => [
		qr/removing stale two-phase state from memory for transaction $commit_prepared_xid/,
		qr/removing stale two-phase state from memory for transaction $abort_prepared_xid/
	]);

# Commit the first transaction.
$cur_primary->psql('postgres', "COMMIT PREPARED 'xact_009_40'");
# After replay, there should be no 2PC transactions.
$cur_primary->psql(
	'postgres',
	"SELECT * FROM pg_prepared_xact",
	stdout => \$psql_out);
is($psql_out, qq{}, "Check expected pg_prepared_xact data on primary");
# Data from transactions should be around.
$cur_primary->psql(
	'postgres',
	"SELECT * FROM t_009_tbl WHERE id IN (40, 41, 42);",
	stdout => \$psql_out);
is( $psql_out, qq{40|transaction: xid horizon
41|transaction: commit-prepared},
	"Check expected table data on primary");

###############################################################################
# Check handling of orphaned 2PC files at recovery.
###############################################################################

$cur_primary->teardown_node;

# Grab location in logs of primary
$log_offset = -s $cur_primary->logfile;

# Create fake files with a transaction ID large or low enough to be in the
# future or the past, then check that the primary is able to start and remove
# these files at recovery.

my $future_2pc_file = $cur_primary->data_dir . '/pg_twophase/00FFFFFF';
append_to_file $future_2pc_file, "";
my $past_2pc_file = $cur_primary->data_dir . '/pg_twophase/000000FF';
append_to_file $past_2pc_file, "";

$cur_primary->start;
$cur_primary->log_check(
	"fake two-phase files removed at recovery",
	$log_offset,
	log_like => [
		qr/removing future two-phase state file for transaction 16777215/,
		qr/removing past two-phase state file for transaction 255/
	]);
