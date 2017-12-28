# Test for interactions with DDL commands manipulating role
# objects.
# The following set of commands can interact in concurrency:
# - ALTER ROLE SET
# - ALTER ROLE option

setup
{
  CREATE ROLE regress_role_conc;
}

teardown
{
  DROP ROLE regress_role_conc;
}

session "s1"
step "s1_begin"			{ BEGIN; }
step "s1_alterrole_set"	{
	ALTER ROLE regress_role_conc SET commit_delay = '10'; }
step "s1_alterrole_opt"	{
	ALTER ROLE regress_role_conc PASSWORD 'foo'; }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_begin"			{ BEGIN; }
step "s2_alterrole_set"	{
	ALTER ROLE regress_role_conc SET commit_delay = '10'; }
step "s2_alterrole_opt"	{
	ALTER ROLE regress_role_conc PASSWORD 'foo'; }
step "s2_commit"	{ COMMIT; }

permutation "s1_begin" "s1_alterrole_set" "s2_begin" "s2_alterrole_set"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterrole_set" "s2_begin" "s2_alterrole_opt"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterrole_opt" "s2_begin" "s2_alterrole_set"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alterrole_opt" "s2_begin" "s2_alterrole_opt"
	"s1_commit" "s2_commit"
