# Copyright (c) 2025, PostgreSQL Global Development Group
#
# Check how the copy of WAL segments is handled from the source to
# the target server.

use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Utils;
use Test::More;
use File::stat qw(stat);

use FindBin;
use lib $FindBin::RealBin;
use RewindTest;
use Time::HiRes qw(usleep);

RewindTest::setup_cluster();
RewindTest::start_primary();
RewindTest::create_standby();

# Advance WAL on primary
RewindTest::primary_psql("CREATE TABLE t(a int)");
RewindTest::primary_psql("INSERT INTO t VALUES(0)");

# Segment that is not copied from the source to the target, being
# generated before the servers have diverged.
my $wal_seg_skipped = $node_primary->safe_psql('postgres',
	'SELECT pg_walfile_name(pg_current_wal_lsn())');

RewindTest::primary_psql("SELECT pg_switch_wal()");

# Follow-up segment, that will include corrupted contents, and should be
# copied from the source to the target even if generated before the point
# of divergence.
RewindTest::primary_psql("INSERT INTO t VALUES(0)");
my $corrupt_wal_seg = $node_primary->safe_psql('postgres',
	'SELECT pg_walfile_name(pg_current_wal_lsn())');
RewindTest::primary_psql("SELECT pg_switch_wal()");

RewindTest::primary_psql("CHECKPOINT");
RewindTest::promote_standby;

# New segment on a new timeline, expected to be copied.
my $new_timeline_wal_seg = $node_standby->safe_psql('postgres',
	'SELECT pg_walfile_name(pg_current_wal_lsn())');

# Get some stats info for the WAL files whose copies should be skipped.
my $wal_skipped_path =
  $node_primary->data_dir . '/pg_wal/' . $wal_seg_skipped;
my $wal_skipped_stat = stat($wal_skipped_path);
defined($wal_skipped_stat) or die("unable to stat $wal_skipped_path");

# Store modification time for later comparison
my $wal_seg_skipped_mod_time = $wal_skipped_stat->mtime;

# Corrupt a WAL segment on target that has been generated before the
# divergence point.  We will check that it is copied over from source.
my $corrupt_wal_seg_in_target_path =
  $node_primary->data_dir . '/pg_wal/' . $corrupt_wal_seg;
open my $fh, ">>", $corrupt_wal_seg_in_target_path
  or die "could not open $corrupt_wal_seg_in_target_path";

print $fh 'a';
close $fh;

my $corrupt_wal_seg_stat_before_rewind =
  stat($corrupt_wal_seg_in_target_path);
ok(defined($corrupt_wal_seg_stat_before_rewind),
	"WAL segment $corrupt_wal_seg should exist in target before rewind");

# Verify that the WAL segment on the new timeline does not exist in target
# before the rewind.
my $new_timeline_wal_seg_path =
  $node_primary->data_dir . '/pg_wal/' . $new_timeline_wal_seg;
my $new_timeline_wal_seg_stat = stat($new_timeline_wal_seg_path);
ok(!defined($new_timeline_wal_seg_stat),
	"WAL segment $new_timeline_wal_seg should not exist in target before rewind"
);

$node_standby->stop();
$node_primary->stop();

# Sleep to allow mtime to be different
usleep(1000000);

command_checks_all(
	[
		'pg_rewind', '--debug',
		'--source-pgdata' => $node_standby->data_dir,
		'--target-pgdata' => $node_primary->data_dir,
		'--no-sync',
	],
	0,
	[qr//],
	[
		qr/WAL segment \"$wal_seg_skipped\" not copied to target/,
		qr/pg_wal\/$corrupt_wal_seg \(COPY\)/
	],
	'run pg_rewind');

# Verify that the copied WAL segment now exists in target.
$new_timeline_wal_seg_stat = stat($new_timeline_wal_seg_path);
ok(defined($new_timeline_wal_seg_stat),
	"WAL segment $new_timeline_wal_seg should exist in target after rewind");

# Get current modification time of the skipped WAL segment.
my $wal_skipped_stat_after_rewind = stat($wal_skipped_path);
defined($wal_skipped_stat_after_rewind)
  or die("unable to stat $wal_skipped_path after rewind");
my $wal_seg_latest_skipped_mod_time = $wal_skipped_stat_after_rewind->mtime;

# Validate that modification time hasn't changed.
is($wal_seg_latest_skipped_mod_time, $wal_seg_skipped_mod_time,
	"WAL segment $wal_seg_skipped modification time should be unchanged (not overwritten)"
);

# Validate that the WAL segment with the same file name as the
# corrupted WAL segment in target has been copied from source
# where we have it intact.
my $corrupt_wal_seg_in_source_path =
  $node_standby->data_dir . '/pg_wal/' . $corrupt_wal_seg;
my $corrupt_wal_seg_source_stat = stat($corrupt_wal_seg_in_source_path);
ok(defined($corrupt_wal_seg_source_stat),
	"WAL segment $corrupt_wal_seg should exist in source after rewind");
my $corrupt_wal_seg_stat_after_rewind = stat($corrupt_wal_seg_in_target_path);
ok(defined($corrupt_wal_seg_stat_after_rewind),
	"WAL segment $corrupt_wal_seg should exist in target after rewind");
ok( $corrupt_wal_seg_stat_before_rewind->size !=
	  $corrupt_wal_seg_source_stat->size,
	"Expected WAL segment $corrupt_wal_seg to have different size in source vs target before rewind"
);
ok( $corrupt_wal_seg_stat_after_rewind->mtime >
	  $corrupt_wal_seg_stat_before_rewind->mtime,
	"Expected WAL segment $corrupt_wal_seg to have later mtime on target than source after rewind as it was copied"
);
ok( $corrupt_wal_seg_stat_after_rewind->size ==
	  $corrupt_wal_seg_source_stat->size,
	"Expected WAL segment $corrupt_wal_seg file sizes to be same between target and source after rewind as it was copied"
);

done_testing();
