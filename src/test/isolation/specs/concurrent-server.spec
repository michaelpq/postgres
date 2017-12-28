# Test for interactions with DDL commands manipulating server
# objects.
# The following set of commands can interact in concurrency:
# - ALTER SERVER OPTIONS
# - ALTER SERVER OWNER TO

setup
{
  CREATE FOREIGN DATA WRAPPER regress_fdw;
  CREATE SERVER regress_server FOREIGN DATA WRAPPER regress_fdw;
}

teardown
{
  DROP FOREIGN DATA WRAPPER regress_fdw CASCADE;
}

session "s1"
step "s1_begin"		{ BEGIN; }
step "s1_options"	{
	ALTER SERVER regress_server OPTIONS (a '1'); }
step "s1_owner"		{
	ALTER SERVER regress_server OWNER TO CURRENT_USER; }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_begin"			{ BEGIN; }
step "s2_options"	{
	ALTER SERVER regress_server OPTIONS (a '1'); }
step "s2_owner"		{
	ALTER SERVER regress_server OWNER TO CURRENT_USER; }
step "s2_commit"	{ COMMIT; }

permutation "s1_begin" "s1_options" "s2_begin" "s2_options"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_options" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_owner" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
