
# Copyright (c) 2021-2025, PostgreSQL Global Development Group

# Tests for connection behavior prior to authentication.

use strict;
use warnings FATAL => 'all';

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Time::HiRes qw(usleep);
use Test::More;

if ($ENV{enable_injection_points} ne 'yes')
{
	plan skip_all => 'Injection points not supported by this build';
}

my $node = PostgreSQL::Test::Cluster->new('primary');
$node->init;
$node->append_conf(
	'postgresql.conf', q[
log_connections = on
]);

$node->start;

# Check if the extension injection_points is available, as it may be
# possible that this script is run with installcheck, where the module
# would not be installed by default.
if (!$node->check_extension('injection_points'))
{
	plan skip_all => 'Extension injection_points not installed';
}

$node->safe_psql('postgres', 'CREATE EXTENSION injection_points');

# Connect to the server and inject a waitpoint.
my $psql = $node->background_psql('postgres');
$psql->query_safe("SELECT injection_points_attach('init-pre-auth', 'wait')");

# From this point on, all new connections will hang during startup, just before
# authentication. Use the $psql connection handle for server interaction.
my $conn = $node->background_psql('postgres', wait => 0);

# Wait for the connection to show up in pg_stat_activity, with the wait_event
# of the injection point.
my $res = $psql->poll_query_until(
	qq{SELECT count(pid) > 0 FROM pg_stat_activity
  WHERE state = 'starting' and wait_event = 'init-pre-auth';});
ok($res, 'authenticating connections are recorded in pg_stat_activity');

# Get the PID of the backend waiting, for the next checks.
my $pid = $psql->query(qq{SELECT pid FROM pg_stat_activity
  WHERE state = 'starting' and wait_event = 'init-pre-auth';});

# Detach the waitpoint and wait for the connection to complete.
$psql->query_safe("SELECT injection_points_wakeup('init-pre-auth');");
$conn->wait_connect();

# Make sure the pgstat entry is updated eventually.
$res = $psql->poll_query_until(
qq{SELECT state FROM pg_stat_activity WHERE pid = $pid;}, 'idle');
ok($res, 'authenticated connections reach idle state in pg_stat_activity');

$psql->query_safe("SELECT injection_points_detach('init-pre-auth');");
$psql->quit();
$conn->quit();

done_testing();
