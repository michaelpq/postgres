# Copyright (c) 2024-2024, PostgreSQL Global Development Group

# Test signaling autovacuum worker backend by non-superuser role.
# Regular roles are tested to fail to signals autovacuum worker, roles
# with pg_signal_autovacuum_worker are tested to success this.
# Test uses an injection point to ensure that the worker is present
# for the whole duration of the test.

use strict;
use warnings;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

if ($ENV{enable_injection_points} ne 'yes')
{
	plan skip_all => 'Injection points not supported by this build';
}

# Initialize postgres
my $psql_err = '';
my $psql_out = '';
my $node = PostgreSQL::Test::Cluster->new('node');
$node->init();
$node->append_conf(
	'postgresql.conf', 'autovacuum_naptime = 1
');
$node->start;
$node->safe_psql('postgres', 'CREATE EXTENSION injection_points;');

# Regular user and user with pg_signal_backend role cannot signal autovacuum worker
# User with pg_signal_backend can signal autovacuum worker 
$node->safe_psql('postgres', qq(
    CREATE ROLE regular_role;
    CREATE ROLE signal_backend_role;
    CREATE ROLE signal_autovacuum_worker_role;
    GRANT pg_signal_backend TO signal_backend_role;
    GRANT pg_signal_autovacuum_worker TO signal_autovacuum_worker_role;
));

# From this point, autovacuum worker will wait before doing any vacuum.
$node->safe_psql('postgres',
	"SELECT injection_points_attach('autovacuum-worker-start', 'wait');");

# Create some content and set autovacuum setting such that it would be triggered.
$node->safe_psql('postgres', qq(
    CREATE TABLE tab_int(i int);
    ALTER TABLE tab_int SET (autovacuum_vacuum_cost_limit = 1);
    ALTER TABLE tab_int SET (autovacuum_vacuum_cost_delay = 100);
));

$node->safe_psql('postgres', qq(
    INSERT INTO tab_int VALUES(1);
));

# Wait until the autovacuum worker starts
$node->wait_for_event('autovacuum worker', 'autovacuum-worker-start');

my $av_pid = $node->safe_psql('postgres', qq(
    SELECT pid FROM pg_stat_activity WHERE backend_type = 'autovacuum worker';
));

# Regular user cannot terminate autovacuum worker
my $terminate_with_no_pg_signal_av = $node->psql('postgres', qq( 
    SET ROLE regular_role;
    SELECT pg_terminate_backend($av_pid);
), stdout => \$psql_out, stderr => \$psql_err);

ok($terminate_with_no_pg_signal_av != 0, "Terminating autovacuum worker should not succeed without pg_signal_autovacuum role");
like($psql_err, qr/ERROR:  permission denied to terminate autovacuum worker backend\nDETAIL:  Only roles with the SUPERUSER attribute or with privileges of the "pg_signal_autovacuum_worker" role may terminate autovacuum worker backend/,
    "Terminating autovacuum errors gracefully when role is not granted with pg_signal_autovacuum_worker");

my $offset = -s $node->logfile;

# User with pg_signal_autovacuum can terminate autovacuum worker 
my $terminate_with_pg_signal_av = $node->psql('postgres', qq( 
    SET ROLE signal_autovacuum_worker_role;
    SELECT pg_terminate_backend($av_pid);
), stdout => \$psql_out, stderr => \$psql_err);


ok($terminate_with_pg_signal_av == 0, "Terminating autovacuum worker should succeed with pg_signal_autovacuum role");

# Check that the primary server logs a FATAL indicating that
# autovacuum is terminated.
ok($node->log_contains(qr/FATAL:  terminating autovacuum process due to administrator command/, $offset), 
        "Autovacuum terminates when role is granted with pg_signal_autovacuum_worker");

# Release injection point.
$node->safe_psql('postgres',
	"SELECT injection_point_detach('autovacuum-worker-start');");

done_testing();
