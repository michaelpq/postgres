
# Copyright (c) 2021-2026, PostgreSQL Global Development Group

# Test for recovery targets: name, timestamp, XID
use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use Time::HiRes qw(usleep);

# Create and test a standby from given backup, with a certain recovery target.
# Choose $until_lsn later than the transaction commit that causes the row
# count to reach $num_rows, yet not later than the recovery target.
sub test_recovery_standby
{
	local $Test::Builder::Level = $Test::Builder::Level + 1;

	my $test_name = shift;
	my $node_name = shift;
	my $node_primary = shift;
	my $recovery_params = shift;
	my $num_rows = shift;
	my $until_lsn = shift;

	my $node_standby = PostgreSQL::Test::Cluster->new($node_name);
	$node_standby->init_from_backup($node_primary, 'my_backup',
		has_restoring => 1);

	foreach my $param_item (@$recovery_params)
	{
		$node_standby->append_conf('postgresql.conf', qq($param_item));
	}

	$node_standby->start;

	# Wait until standby has replayed enough data
	my $caughtup_query =
	  "SELECT '$until_lsn'::pg_lsn <= pg_last_wal_replay_lsn()";
	$node_standby->poll_query_until('postgres', $caughtup_query)
	  or die "Timed out while waiting for standby to catch up";

	# Create some content on primary and check its presence in standby
	my $result =
	  $node_standby->safe_psql('postgres', "SELECT count(*) FROM tab_int");
	is($result, qq($num_rows), "check standby content for $test_name");

	# Stop standby node
	$node_standby->teardown_node;

	return;
}

# Start a standby with the given pg_ctl --options string and verify that
# the standby reaches the given LSN and row count.  Used to exercise
# scenarios that require the postmaster command line to receive multiple
# "-c name=value" instances of the same GUC, which postgresql.conf cannot
# express because ProcessConfigFile collapses duplicate keys.
sub test_recovery_standby_with_options
{
	local $Test::Builder::Level = $Test::Builder::Level + 1;

	my $test_name = shift;
	my $node_name = shift;
	my $node_primary = shift;
	my $options = shift;
	my $num_rows = shift;
	my $until_lsn = shift;

	my $node_standby = PostgreSQL::Test::Cluster->new($node_name);
	$node_standby->init_from_backup($node_primary, 'my_backup',
		has_restoring => 1);

	my $res = run_log(
		[
			'pg_ctl',
			'--pgdata' => $node_standby->data_dir,
			'--log' => $node_standby->logfile,
			'--options' => $options,
			'start',
		]);
	ok($res, "server starts for $test_name");

	$node_standby->poll_query_until('postgres',
		"SELECT '$until_lsn'::pg_lsn <= pg_last_wal_replay_lsn()")
	  or die "Timed out while waiting for standby to catch up";

	my $count = $node_standby->safe_psql('postgres',
		"SELECT count(*) FROM tab_int");
	is($count, qq($num_rows), "check standby content for $test_name");

	$node_standby->teardown_node;

	return;
}

# Initialize primary node
my $node_primary = PostgreSQL::Test::Cluster->new('primary');
$node_primary->init(has_archiving => 1, allows_streaming => 1);

# Bump the transaction ID epoch.  This is useful to stress the portability
# of recovery_target_xid parsing.
system_or_bail('pg_resetwal', '--epoch' => '1', $node_primary->data_dir);

# Start it
$node_primary->start;

# Create data before taking the backup, aimed at testing
# recovery_target = 'immediate'
$node_primary->safe_psql('postgres',
	"CREATE TABLE tab_int AS SELECT generate_series(1,1000) AS a");
my $lsn1 =
  $node_primary->safe_psql('postgres', "SELECT pg_current_wal_lsn();");

# Take backup from which all operations will be run
$node_primary->backup('my_backup');

# Insert some data with used as a replay reference, with a recovery
# target TXID.
$node_primary->safe_psql('postgres',
	"INSERT INTO tab_int VALUES (generate_series(1001,2000))");
my $ret = $node_primary->safe_psql('postgres',
	"SELECT pg_current_wal_lsn(), pg_current_xact_id();");
my ($lsn2, $recovery_txid) = split /\|/, $ret;

# More data, with recovery target timestamp
$node_primary->safe_psql('postgres',
	"INSERT INTO tab_int VALUES (generate_series(2001,3000))");
my $lsn3 =
  $node_primary->safe_psql('postgres', "SELECT pg_current_wal_lsn();");
my $recovery_time = $node_primary->safe_psql('postgres', "SELECT now()");

# Even more data, this time with a recovery target name
$node_primary->safe_psql('postgres',
	"INSERT INTO tab_int VALUES (generate_series(3001,4000))");
my $recovery_name = "my_target";
my $lsn4 =
  $node_primary->safe_psql('postgres', "SELECT pg_current_wal_lsn();");
$node_primary->safe_psql('postgres',
	"SELECT pg_create_restore_point('$recovery_name');");

# And now for a recovery target LSN
$node_primary->safe_psql('postgres',
	"INSERT INTO tab_int VALUES (generate_series(4001,5000))");
my $lsn5 = my $recovery_lsn =
  $node_primary->safe_psql('postgres', "SELECT pg_current_wal_lsn()");

$node_primary->safe_psql('postgres',
	"INSERT INTO tab_int VALUES (generate_series(5001,6000))");

# Force archiving of WAL file
$node_primary->safe_psql('postgres', "SELECT pg_switch_wal()");

# LSN after the final 6000-row insert and WAL switch.  Used by the
# set-then-cleared scenarios below where recovery has no target and must
# replay all archived WAL; polling on $lsn5 would race against the 5001-6000
# rows.
my $lsn6 =
  $node_primary->safe_psql('postgres', "SELECT pg_current_wal_lsn()");

# Test recovery targets
my @recovery_params = ("recovery_target = 'immediate'");
test_recovery_standby('immediate target',
	'standby_1', $node_primary, \@recovery_params, "1000", $lsn1);
@recovery_params = ("recovery_target_xid = '$recovery_txid'");
test_recovery_standby('XID', 'standby_2', $node_primary, \@recovery_params,
	"2000", $lsn2);
@recovery_params = ("recovery_target_time = '$recovery_time'");
test_recovery_standby('time', 'standby_3', $node_primary, \@recovery_params,
	"3000", $lsn3);
@recovery_params = ("recovery_target_name = '$recovery_name'");
test_recovery_standby('name', 'standby_4', $node_primary, \@recovery_params,
	"4000", $lsn4);
@recovery_params = ("recovery_target_lsn = '$recovery_lsn'");
test_recovery_standby('LSN', 'standby_5', $node_primary, \@recovery_params,
	"5000", $lsn5);

# Regression: empty-string for one recovery_target_* GUC must not clobber
# another non-empty target.  Setting recovery_target_xid + recovery_target_time
# = '' must recover to the xid, not run as no-target recovery.
@recovery_params = (
	"recovery_target_xid = '$recovery_txid'",
	"recovery_target_time = ''");
test_recovery_standby('xid with empty time GUC',
	'standby_xid_empty_time', $node_primary, \@recovery_params,
	"2000", $lsn2);

# Multiple targets
#
# Multiple conflicting non-empty settings are not allowed.  Setting the same
# parameter multiple times is allowed (the last value wins, per ProcessConfigFile
# duplicate handling).  An empty string for a recovery_target_* GUC is treated
# as the GUC's default and is a no-op; it does not clear any other already-set
# target.  Conflict detection runs in CheckRecoveryTargetConflicts() at every
# server start, regardless of whether recovery is requested.

@recovery_params = (
	"recovery_target_name = '$recovery_name'",
	"recovery_target_name = ''",
	"recovery_target_time = '$recovery_time'");
test_recovery_standby('multiple overriding settings',
	'standby_6', $node_primary, \@recovery_params, "3000", $lsn3);

my $node_standby = PostgreSQL::Test::Cluster->new('standby_7');
$node_standby->init_from_backup($node_primary, 'my_backup',
	has_restoring => 1);
$node_standby->append_conf(
	'postgresql.conf', "recovery_target_name = '$recovery_name'
recovery_target_time = '$recovery_time'");

my $res = run_log(
	[
		'pg_ctl',
		'--pgdata' => $node_standby->data_dir,
		'--log' => $node_standby->logfile,
		'start',
	]);
ok(!$res, 'invalid recovery startup fails');

my $logfile = slurp_file($node_standby->logfile());
like(
	$logfile,
	qr/multiple recovery targets specified/,
	'multiple conflicting settings');
# errdetail lists exactly the parameters that are set, in GUC-check order
# (recovery_target_name is checked before recovery_target_time).
like(
	$logfile,
	qr/Recovery targets "recovery_target_name", "recovery_target_time" are set/,
	'errdetail lists the set parameters in order');
# The parameter values must not be echoed (only the names are listed).
unlike(
	$logfile,
	qr/are set[^\n]*=/,
	'errdetail does not echo parameter values');
like(
	$logfile,
	qr/HINT:.*pg_settings/,
	'errhint points to pg_settings');

# Check behavior when recovery ends before target is reached

$node_standby = PostgreSQL::Test::Cluster->new('standby_8');
$node_standby->init_from_backup(
	$node_primary, 'my_backup',
	has_restoring => 1,
	standby => 0);
$node_standby->append_conf('postgresql.conf',
	"recovery_target_name = 'does_not_exist'");

run_log(
	[
		'pg_ctl',
		'--pgdata' => $node_standby->data_dir,
		'--log' => $node_standby->logfile,
		'start',
	]);

# wait for postgres to terminate
foreach my $i (0 .. 10 * $PostgreSQL::Test::Utils::timeout_default)
{
	last if !-f $node_standby->data_dir . '/postmaster.pid';
	usleep(100_000);
}
$logfile = slurp_file($node_standby->logfile());
like(
	$logfile,
	qr/FATAL: .* recovery ended before configured recovery target was reached/,
	'recovery end before target reached is a fatal error');

# Conflicting recovery targets are rejected at every startup, regardless of
# whether recovery.signal is present.  Use a throwaway cluster initialized
# from the existing backup so we can leave conflicting GUCs in its
# postgresql.conf without polluting the primary used by later tests.
# init_from_backup is called without has_restoring, so no recovery.signal
# is created and the cluster would normally start as a plain primary; the
# conflicting recovery_target_* GUCs must be rejected anyway.
my $node_no_signal = PostgreSQL::Test::Cluster->new('multi_target_no_signal');
$node_no_signal->init_from_backup($node_primary, 'my_backup');
$node_no_signal->append_conf(
	'postgresql.conf', "recovery_target_name = '$recovery_name'
recovery_target_time = '$recovery_time'");

my $res_no_signal = run_log(
	[
		'pg_ctl',
		'--pgdata' => $node_no_signal->data_dir,
		'--log' => $node_no_signal->logfile,
		'start',
	]);
ok(!$res_no_signal,
	'server fails to start with conflicting recovery targets and no recovery.signal');

my $logfile_no_signal = slurp_file($node_no_signal->logfile());
like(
	$logfile_no_signal,
	qr/multiple recovery targets specified/,
	'expected error message logged without recovery.signal');
like(
	$logfile_no_signal,
	qr/Recovery targets "recovery_target_name", "recovery_target_time" are set/,
	'errdetail lists the set parameters in order without recovery.signal');
unlike(
	$logfile_no_signal,
	qr/are set[^\n]*=/,
	'errdetail does not echo parameter values without recovery.signal');
like(
	$logfile_no_signal,
	qr/HINT:.*pg_settings/,
	'errhint points to pg_settings without recovery.signal');

# Conflict involving the bare recovery_target (no suffix): it is checked first,
# so its name appears at the head of the list.  The list entries are quoted, so
# the regex anchors on the closing quote to ensure "recovery_target" does not
# match a prefix of "recovery_target_xid".
my $node_bare = PostgreSQL::Test::Cluster->new('multi_target_bare');
$node_bare->init_from_backup($node_primary, 'my_backup');
$node_bare->append_conf('postgresql.conf',
	"recovery_target = 'immediate'\nrecovery_target_xid = '$recovery_txid'");

my $res_bare = run_log(
	[
		'pg_ctl',
		'--pgdata' => $node_bare->data_dir,
		'--log' => $node_bare->logfile,
		'start',
	]);
ok(!$res_bare,
	'server fails to start with bare recovery_target conflict');

my $logfile_bare = slurp_file($node_bare->logfile());
like(
	$logfile_bare,
	qr/Recovery targets "recovery_target", "recovery_target_xid" are set/,
	'errdetail lists the bare recovery_target first, without prefix mismatch');

# Three conflicting targets: exercises the ", " separator between more than two
# entries (no trailing separator after the last entry).
my $node_three = PostgreSQL::Test::Cluster->new('multi_target_three');
$node_three->init_from_backup($node_primary, 'my_backup');
$node_three->append_conf('postgresql.conf',
	"recovery_target_name = '$recovery_name'\n"
	  . "recovery_target_time = '$recovery_time'\n"
	  . "recovery_target_xid = '$recovery_txid'");

my $res_three = run_log(
	[
		'pg_ctl',
		'--pgdata' => $node_three->data_dir,
		'--log' => $node_three->logfile,
		'start',
	]);
ok(!$res_three,
	'server fails to start with three conflicting recovery targets');

my $logfile_three = slurp_file($node_three->logfile());
like(
	$logfile_three,
	qr/Recovery targets "recovery_target_name", "recovery_target_time", "recovery_target_xid" are set/,
	'errdetail lists three set parameters with correct separators');

# Same-GUC last-wins (one source of truth for the GUC's value): assigning a
# recovery_target_* GUC and then assigning the same GUC to an empty string
# leaves no target set and recovery proceeds to the end of WAL.  This is the
# "set then unset" form of GUC reassignment; the assign hook clears its own
# type when the new value is empty.  postgresql.conf cannot express duplicate
# keys (ProcessConfigFile collapses them), so the postmaster command line is
# used via "pg_ctl --options".  Each of the five recovery_target_* assign
# hooks is exercised once.
#
# Note: GUC values below are passed unquoted on the command line.  Windows
# cmd.exe does not strip single quotes the way POSIX shells do, so quoted
# values would reach the postmaster verbatim and be rejected.  recovery_time
# from now() contains a space; we replace it with the ISO 8601 T separator
# so the value is a single shell token without quoting.
my $recovery_time_t = $recovery_time;
$recovery_time_t =~ s/ /T/;

test_recovery_standby_with_options(
	'recovery_target_xid set then cleared',
	'standby_xid_set_clear', $node_primary,
	"-c recovery_target_xid=$recovery_txid -c recovery_target_xid=",
	"6000", $lsn6);

test_recovery_standby_with_options(
	'recovery_target_time set then cleared',
	'standby_time_set_clear', $node_primary,
	"-c recovery_target_time=$recovery_time_t -c recovery_target_time=",
	"6000", $lsn6);

test_recovery_standby_with_options(
	'recovery_target_name set then cleared',
	'standby_name_set_clear', $node_primary,
	"-c recovery_target_name=$recovery_name -c recovery_target_name=",
	"6000", $lsn6);

test_recovery_standby_with_options(
	'recovery_target_lsn set then cleared',
	'standby_lsn_set_clear', $node_primary,
	"-c recovery_target_lsn=$recovery_lsn -c recovery_target_lsn=",
	"6000", $lsn6);

test_recovery_standby_with_options(
	'recovery_target set then cleared',
	'standby_immediate_set_clear', $node_primary,
	"-c recovery_target=immediate -c recovery_target=",
	"6000", $lsn6);

# Same-GUC empty-then-set sanity: assigning an empty string and then a
# non-empty value to the same GUC must end with the target set.  The empty
# value seen first must not poison a later non-empty assignment.
my $node_xid_clear_set =
  PostgreSQL::Test::Cluster->new('standby_xid_clear_set');
$node_xid_clear_set->init_from_backup($node_primary, 'my_backup',
	has_restoring => 1);

my $res_xid_clear_set = run_log(
	[
		'pg_ctl',
		'--pgdata' => $node_xid_clear_set->data_dir,
		'--log' => $node_xid_clear_set->logfile,
		'--options' =>
		  "-c recovery_target_xid= -c recovery_target_xid=$recovery_txid",
		'start',
	]);
ok($res_xid_clear_set,
	'server starts with recovery_target_xid cleared then set');

$node_xid_clear_set->poll_query_until('postgres',
	"SELECT '$lsn2'::pg_lsn <= pg_last_wal_replay_lsn()")
  or die "Timed out while waiting for standby to catch up";
my $count_xid_clear_set = $node_xid_clear_set->safe_psql('postgres',
	"SELECT count(*) FROM tab_int");
is($count_xid_clear_set, "2000",
	'recovery_target_xid honored when cleared then set');
$node_xid_clear_set->teardown_node;

# Invalid recovery_target_timeline tests
my ($result, $stdout, $stderr) = $node_primary->psql('postgres',
	"ALTER SYSTEM SET recovery_target_timeline TO 'bogus'");
like(
	$stderr,
	qr/is not a valid number/,
	"invalid recovery_target_timeline (bogus value)");

($result, $stdout, $stderr) = $node_primary->psql('postgres',
	"ALTER SYSTEM SET recovery_target_timeline TO '0'");
like(
	$stderr,
	qr/must be between 1 and 4294967295/,
	"invalid recovery_target_timeline (lower bound check)");

($result, $stdout, $stderr) = $node_primary->psql('postgres',
	"ALTER SYSTEM SET recovery_target_timeline TO '4294967296'");
like(
	$stderr,
	qr/must be between 1 and 4294967295/,
	"invalid recovery_target_timeline (upper bound check)");

# Invalid recovery_target_xid tests
($result, $stdout, $stderr) = $node_primary->psql('postgres',
	"ALTER SYSTEM SET recovery_target_xid TO 'bogus'");
like(
	$stderr,
	qr/is not a valid number/,
	"invalid recovery_target_xid (bogus value)");

($result, $stdout, $stderr) = $node_primary->psql('postgres',
	"ALTER SYSTEM SET recovery_target_xid TO '-1'");
like(
	$stderr,
	qr/is not a valid number/,
	"invalid recovery_target_xid (negative)");

($result, $stdout, $stderr) = $node_primary->psql('postgres',
	"ALTER SYSTEM SET recovery_target_xid TO '0'");
like(
	$stderr,
	qr/without epoch must be greater than or equal to 3/,
	"invalid recovery_target_xid (lower bound check)");

done_testing();
