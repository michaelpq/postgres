# VACUUM FULL on a catalog, with an aborted update by a concurrent
# transaction
#
# s1 creates a table and then updates its pg_class row in a subtransaction
# that rolls back.  s1 is still open when s2 rewrites pg_class, and no one
# has set HEAP_XMAX_INVALID on the pg_class row yet.  The row must survive.

teardown
{
  DROP TABLE IF EXISTS cluster_catalog_tab;
  DROP TABLE IF EXISTS cluster_plain_tab;
}

session s1
step s1_create	{ BEGIN; CREATE TABLE cluster_catalog_tab (a int); }
step s1_alter	{ SAVEPOINT sp; ALTER TABLE cluster_catalog_tab ADD COLUMN b int; ROLLBACK TO sp; }
step s1_commit	{ COMMIT; }
step s1_plain	{ BEGIN; CREATE TABLE cluster_plain_tab (a int, b int); CREATE INDEX cluster_plain_idx ON cluster_plain_tab (b); INSERT INTO cluster_plain_tab VALUES (1, 1); SAVEPOINT sp; UPDATE cluster_plain_tab SET a = 2; ROLLBACK TO sp; CLUSTER cluster_plain_tab USING cluster_plain_idx; COMMIT; }
step s1_count	{ SELECT count(*) FROM cluster_plain_tab; }

session s2
step s2_vacfull	{ VACUUM FULL pg_class; }
step s2_check	{ SELECT count(*) FROM pg_class WHERE relname = 'cluster_catalog_tab'; }

permutation s1_create s1_alter s2_vacfull s1_commit s2_check
permutation s1_plain s1_count
