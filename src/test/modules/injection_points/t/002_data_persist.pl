
# Copyright (c) 2024-2025, PostgreSQL Global Development Group

# Tests for persistence of injection point data.

use strict;
use warnings FATAL => 'all';
use locale;

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

# Test persistency of statistics generated for injection points.
if ($ENV{enable_injection_points} ne 'yes')
{
	plan skip_all => 'Injection points not supported by this build';
}

# Node initialization
my $node = PostgreSQL::Test::Cluster->new('master');
$node->init;
$node->append_conf(
	'postgresql.conf', qq(
shared_preload_libraries = 'injection_points'
));
$node->start;
$node->safe_psql('postgres', 'CREATE EXTENSION injection_points;');

# Attach a couple of points, which are going to be made persistent.
$node->safe_psql('postgres',
	"SELECT injection_points_attach('persist-notice', 'notice');");
$node->safe_psql('postgres',
	"SELECT injection_points_attach('persist-error', 'error');");
$node->safe_psql('postgres',
	"SELECT injection_points_attach('persist-notice-2', 'notice');");

# Flush and restart, the injection points still exist.
$node->safe_psql('postgres', "SELECT injection_points_flush();");
$node->restart;

my ($result, $stdout, $stderr) =
  $node->psql('postgres', "SELECT injection_points_run('persist-notice-2')");
ok( $stderr =~
	  /NOTICE:  notice triggered for injection point persist-notice-2/,
	"injection point triggering NOTICE exists");

($result, $stdout, $stderr) =
  $node->psql('postgres', "SELECT injection_points_run('persist-error')");
ok($stderr =~ /ERROR:  error triggered for injection point persist-error/,
	"injection point triggering ERROR exists");

done_testing();
