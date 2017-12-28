# Test for interactions with DDL commands manipulating database
# objects.
# XXX: This cannot be included in the final patch as installcheck would
# create a database on existing instances!
# The following set of commands can interact in concurrency:
# - ALTER DATABASE SET
# - ALTER DATABASE OWNER TO
# - ALTER DATABASE SET TABLESPACE
# - ALTER DATABASE WITH

setup
{
  CREATE DATABASE regress_database TEMPLATE template0;
}

teardown
{
  DROP DATABASE regress_database;
}

session "s1"
step "s1_begin"			{ BEGIN; }
step "s1_alterdb_with"	{
	ALTER DATABASE regress_database WITH ALLOW_CONNECTIONS true; }
step "s1_alterdb_set"	{
	ALTER DATABASE regress_database SET commit_delay = '10'; }
step "s1_alterdb_owner"	{
	ALTER DATABASE regress_database OWNER TO CURRENT_USER; }
step "s1_alterdb_tbc"	{
	ALTER DATABASE regress_database SET TABLESPACE TO DEFAULT; }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_begin"			{ BEGIN; }
step "s2_alterdb_with"	{
	ALTER DATABASE regress_database WITH ALLOW_CONNECTIONS true; }
step "s2_alterdb_set"	{
	ALTER DATABASE regress_database SET commit_delay = '10'; }
step "s2_alterdb_owner"	{
	ALTER DATABASE regress_database OWNER TO CURRENT_USER; }
step "s2_alterdb_tbc"	{
	ALTER DATABASE regress_database SET TABLESPACE TO DEFAULT; }
step "s2_commit"	{ COMMIT; }

permutation "s1_begin" "s1_alterdb_with" "s2_begin" "s2_alterdb_with"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_with" "s2_begin" "s2_alterdb_set"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_with" "s2_begin" "s2_alterdb_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_with" "s2_begin" "s2_alterdb_tbc"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_set" "s2_begin" "s2_alterdb_set"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_set" "s2_begin" "s2_alterdb_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_set" "s2_begin" "s2_alterdb_tbc"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_owner" "s2_begin" "s2_alterdb_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_owner" "s2_begin" "s2_alterdb_tbc"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterdb_tbc" "s2_begin" "s2_alterdb_tbc"
	"s1_commit" "s2_commit"
