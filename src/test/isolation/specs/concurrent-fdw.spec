# Test for interactions with DDL commands manipulating fdw
# objects.
# The following set of commands can interact in concurrency:
# - ALTER FDW OPTIONS
# - ALTER FDW VALIDATOR
# - ALTER FDW HANDLER
# - ALTER FDW OWNER TO

setup
{
  CREATE FOREIGN DATA WRAPPER regress_fdw;
}

teardown
{
  DROP FOREIGN DATA WRAPPER regress_fdw;
}

session "s1"
step "s1_begin"			{ BEGIN; }
step "s1_options"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw OPTIONS (a '1'); }
step "s1_validator"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw NO VALIDATOR; }
step "s1_handler"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw NO HANDLER; }
step "s1_owner"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw OWNER TO CURRENT_USER; }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_begin"			{ BEGIN; }
step "s2_options"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw OPTIONS (a '1'); }
step "s2_validator"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw NO VALIDATOR; }
step "s2_handler"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw NO HANDLER; }
step "s2_owner"	{
	ALTER FOREIGN DATA WRAPPER regress_fdw OWNER TO CURRENT_USER; }
step "s2_commit"	{ COMMIT; }

permutation "s1_begin" "s1_options" "s2_begin" "s2_options"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_options" "s2_begin" "s2_validator"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_options" "s2_begin" "s2_handler"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_options" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_validator" "s2_begin" "s2_validator"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_validator" "s2_begin" "s2_handler"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_validator" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_handler" "s2_begin" "s2_handler"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_handler" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_owner" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
