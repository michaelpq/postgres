# Test for interactions with DDL commands manipulating type
# objects.
# The following set of commands can interact in concurrency:
# - ALTER TYPE ADD ATTRIBUTE
# - ALTER TYPE DROP ATTRIBUTE
# - ALTER TYPE ALTER ATTRIBUTE
# - ALTER TYPE RENAME ATTRIBUTE
# - ALTER TYPE OWNER TO
# - ALTER TYPE SET SCHEMA

setup
{
  CREATE SCHEMA regress_type_schema;
  CREATE TYPE regress_type AS (a int);
}

teardown
{
  DROP TYPE IF EXISTS regress_type;
  DROP SCHEMA regress_type_schema CASCADE;
}

session "s1"
step "s1_begin"			{ BEGIN; }
step "s1_add_attr"		{
	ALTER TYPE regress_type ADD ATTRIBUTE b1 int; }
step "s1_drop_attr"		{
	ALTER TYPE regress_type DROP ATTRIBUTE a; }
step "s1_alter_attr"	{
	ALTER TYPE regress_type ALTER ATTRIBUTE a TYPE int8; }
step "s1_rename_attr"	{
	ALTER TYPE regress_type RENAME ATTRIBUTE a TO b; }
step "s1_owner"			{
	ALTER TYPE regress_type OWNER TO CURRENT_USER; }
step "s1_schema"		{
	ALTER TYPE regress_type SET SCHEMA regress_type_schema; }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_begin"		{ BEGIN; }
step "s2_add_attr"		{
	ALTER TYPE regress_type ADD ATTRIBUTE b2 int; }
step "s2_drop_attr"		{
	ALTER TYPE regress_type DROP ATTRIBUTE a; }
step "s2_alter_attr"	{
	ALTER TYPE regress_type ALTER ATTRIBUTE a TYPE int8; }
step "s2_rename_attr"	{
	ALTER TYPE regress_type RENAME ATTRIBUTE a TO b; }
step "s2_owner"			{
	ALTER TYPE regress_type OWNER TO CURRENT_USER; }
step "s2_schema"		{
	ALTER TYPE regress_type SET SCHEMA regress_type_schema; }
step "s2_commit"	{ COMMIT; }

# Round 1 of permutations based on ADD ATTRIBUTE
permutation "s1_begin" "s1_add_attr" "s2_begin" "s2_add_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_add_attr" "s2_begin" "s2_drop_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_add_attr" "s2_begin" "s2_alter_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_add_attr" "s2_begin" "s2_rename_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_add_attr" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_add_attr" "s2_begin" "s2_schema"
	"s1_commit" "s2_commit"

# Round 2 of permutations based on DROP ATTRIBUTE
permutation "s1_begin" "s1_drop_attr" "s2_begin" "s2_drop_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_drop_attr" "s2_begin" "s2_alter_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_drop_attr" "s2_begin" "s2_rename_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_drop_attr" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_drop_attr" "s2_begin" "s2_schema"
	"s1_commit" "s2_commit"

# Round 3 of permutations based on ALTER ATTRIBUTE
permutation "s1_begin" "s1_alter_attr" "s2_begin" "s2_alter_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alter_attr" "s2_begin" "s2_rename_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alter_attr" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_alter_attr" "s2_begin" "s2_schema"
	"s1_commit" "s2_commit"

# Round 4 of permutations based on RENAME ATTRIBUTE
permutation "s1_begin" "s1_rename_attr" "s2_begin" "s2_rename_attr"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_rename_attr" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_rename_attr" "s2_begin" "s2_schema"
	"s1_commit" "s2_commit"

# Round 5 of permutations based on OWNER TO
permutation "s1_begin" "s1_owner" "s2_begin" "s2_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_owner" "s2_begin" "s2_schema"
	"s1_commit" "s2_commit"

# Round 6 of permutations based on SET SCHEMA
permutation "s1_begin" "s1_schema" "s2_begin" "s2_schema"
	"s1_commit" "s2_commit"
