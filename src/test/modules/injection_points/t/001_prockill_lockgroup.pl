# Copyright (c) 2025-2026, PostgreSQL Global Development Group
#
# Regression test for the ProcKill lock-group vs. procLatch recycle race.
#
# Two backends form a lock group, then are terminated concurrently.  The test
# uses injection points placed inside ProcKill() to pause both victims there
# and verify that a freshly forked backend can claim the recycled PGPROC slot
# without hitting "latch already owned by PID ..." PANIC.
#
# Requires --enable-injection-points.

use strict;
use warnings FATAL => 'all';

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

if (($ENV{enable_injection_points} // '') ne 'yes')
{
	plan skip_all => 'Injection points not supported by this build';
}

my $node = PostgreSQL::Test::Cluster->new('prockill_race');
$node->init;
$node->append_conf('postgresql.conf',
	q{shared_preload_libraries = 'injection_points'});
$node->start;

$node->safe_psql('postgres', 'CREATE EXTENSION injection_points;');

# Form a two-backend lock group.
my $leader   = $node->background_psql('postgres');
my $follower = $node->background_psql('postgres');

$leader->query_safe('SELECT prockill_become_lock_group_leader()');
my $leader_pid   = $leader->query_safe('SELECT pg_backend_pid()');
my $follower_pid = $follower->query_safe('SELECT pg_backend_pid()');
$leader_pid   =~ s/\s+//g;
$follower_pid =~ s/\s+//g;

$follower->query_safe("SELECT prockill_become_lock_group_member($leader_pid)");

# Attach PID-scoped injection waits from a throwaway controller session, not
# from $leader/$follower.  injection_points_attach() via the victim would
# register a before_shmem_exit cleanup that fires before ProcKill and detaches
# the point mid-exit.  prockill_attach_injection_wait() calls
# InjectionPointAttach() directly, leaving no such hook on the victims.
$node->safe_psql('postgres', qq{
	SELECT prockill_attach_injection_wait('prockill-after-lockgroup-leader',   $leader_pid);
	SELECT prockill_attach_injection_wait('prockill-after-lockgroup-follower', $follower_pid);
});

# Kill the leader and wait for it to pause inside ProcKill.
# prockill_backend_in_injection() reads PGPROC->wait_event_info directly
# because pg_stat_activity and ProcArray are already torn down before ProcKill.
$node->safe_psql('postgres', "SELECT pg_terminate_backend($leader_pid)");
my $leader_ok = $node->poll_query_until(
	'postgres',
	"SELECT prockill_backend_in_injection($leader_pid, 'prockill-after-lockgroup-leader')"
);

# Kill the follower and wait for it similarly.  Use eval: if the fix has
# regressed, the postmaster may have already PANICed.
eval {
	$node->safe_psql('postgres',
		"SELECT pg_terminate_backend($follower_pid)");
};
my $follower_ok = $node->poll_query_until(
	'postgres',
	"SELECT prockill_backend_in_injection($follower_pid, 'prockill-after-lockgroup-follower')"
);

# Release both victims.
eval {
	$node->safe_psql('postgres',
		q{SELECT injection_points_wakeup('prockill-after-lockgroup-follower');
		  SELECT injection_points_wakeup('prockill-after-lockgroup-leader');});
};

# A new backend must be able to claim the recycled PGPROC slot without PANIC.
my $select_ok = eval { $node->safe_psql('postgres', 'SELECT 1'); 1 };

ok($leader_ok && $follower_ok,
	'leader and follower reached ProcKill injection points');
ok($select_ok,
	'new backend claimed recycled PGPROC without latch-recycle PANIC');

eval {
	$node->safe_psql('postgres',
		q{SELECT injection_points_detach('prockill-after-lockgroup-leader');
		  SELECT injection_points_detach('prockill-after-lockgroup-follower');});
};

eval { $leader->quit };
eval { $follower->quit };
$node->stop('fast', fail_ok => 1);

done_testing();
