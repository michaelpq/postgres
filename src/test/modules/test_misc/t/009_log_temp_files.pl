#
# Verify that temp files are logged with the right statement.
#
# Copyright (c) 2021-2025, PostgreSQL Global Development Group
#

use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

# Initialize a new PostgreSQL test cluster
my $node = PostgreSQL::Test::Cluster->new('primary');
$node->init();
$node->append_conf(
    'postgresql.conf', qq(
work_mem = 64kB
log_temp_files = 0
log_statement = all
log_min_duration_statement = 0
));
$node->start;

# Setup table and populate with data
$node->safe_psql("postgres", qq{
CREATE UNLOGGED TABLE foo(a int);
INSERT INTO foo(a) SELECT * FROM generate_series(1, 5000);
VACUUM ANALYZE foo;
});

# unnamed portal test
my $log_offset = -s $node->logfile;
$node->safe_psql("postgres", qq{
BEGIN;
SELECT a FROM foo ORDER BY a OFFSET \$1 \\bind 4999 \\g
SELECT 1;
END;
});
$node->wait_for_log(qr/LOG:\s+execute <unnamed>:\s+SELECT a FROM foo ORDER BY a OFFSET \$1.*LOG:\s+temporary file: path/s, $log_offset);
ok("log temp with unnamed portal");

# named portal test
$log_offset = -s $node->logfile;
$node->safe_psql("postgres", qq{
BEGIN;
SELECT a FROM foo ORDER BY a OFFSET \$1 \\parse stmt
\\bind_named stmt 4999 \\g
SELECT 1;
END;
});
$node->wait_for_log(qr/LOG:\s+execute stmt:\s+SELECT a FROM foo ORDER BY a OFFSET \$1.*LOG:\s+temporary file: path/s, $log_offset);
ok("log temp with named portal");

# pipelined query test
$log_offset = -s $node->logfile;
$node->safe_psql("postgres", qq{
\\startpipeline
SELECT a FROM foo ORDER BY a OFFSET \$1 \\bind 4999 \\sendpipeline
SELECT 1;
\\endpipeline
});
$node->wait_for_log(qr/LOG:\s+execute <unnamed>:\s+SELECT a FROM foo ORDER BY a OFFSET \$1.*LOG:\s+temporary file: path/s, $log_offset);
ok("log temp with pipelined query");

# cursor test
$log_offset = -s $node->logfile;
$node->safe_psql("postgres", qq{
BEGIN;
DECLARE mycur CURSOR FOR SELECT a FROM foo ORDER BY a OFFSET 4999;
FETCH 10 FROM mycur;
SELECT 1;
CLOSE mycur;
END;
});
$node->wait_for_log(qr/LOG:  statement: CLOSE mycur;.*LOG:  temporary file: path.*/s, $log_offset);
ok("log temp with cursor");

# simple query test
$log_offset = -s $node->logfile;
$node->safe_psql("postgres", qq{
BEGIN;
SELECT a FROM foo ORDER BY a OFFSET 4999;
END;
});
$node->wait_for_log(qr/LOG:  statement: SELECT a FROM foo ORDER BY a OFFSET 4999;.*statement: END;/s, $log_offset);
ok("log temp with simple query");

# Stop the node
$node->stop('fast');
done_testing();
