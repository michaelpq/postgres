# Test for interactions with DDL commands manipulating user
# mappings
# The following set of commands can interact in concurrency:
# - ALTER USER MAPPING OPTIONS

setup
{
  CREATE FOREIGN DATA WRAPPER regress_fdw;
  CREATE SERVER regress_server FOREIGN DATA WRAPPER regress_fdw;
  CREATE USER MAPPING FOR CURRENT_USER SERVER regress_server
    OPTIONS (user 'CURRENT_USER');
}

teardown
{
  DROP FOREIGN DATA WRAPPER regress_fdw CASCADE;
}

session "s1"
step "s1_begin"		{ BEGIN; }
step "s1_options"	{
	ALTER USER MAPPING FOR CURRENT_USER server regress_server
	  OPTIONS (SET USER 'popo1'); }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_begin"		{ BEGIN; }
step "s2_options"	{
	ALTER USER MAPPING FOR CURRENT_USER server regress_server
	  OPTIONS (SET USER 'popo1'); }
step "s2_commit"	{ COMMIT; }

permutation "s1_begin" "s1_options" "s2_begin" "s2_options"
	"s1_commit" "s2_commit"
