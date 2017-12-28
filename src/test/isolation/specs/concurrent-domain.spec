# Test for interactions with DDL commands manipulating domain
# objects.
# The following set of commands can interact in concurrency:
# - ALTER DOMAIN SET NOT NULL
# - ALTER DOMAIN DROP NOT NULL
# - ALTER DOMAIN SET DEFAULT
# - ALTER DOMAIN DROP DEFAULT
# - ALTER DOMAIN ADD CONSTRAINT
# - ALTER DOMAIN DROP CONSTRAINT
# - ALTER DOMAIN VALIDATE CONSTRAINT
# - ALTER DOMAIN OWNER TO
# - ALTER DOMAIN SET SCHEMA

setup
{
  CREATE SCHEMA regress_domain_schema;
  CREATE DOMAIN regress_domain_conc AS int;
}

teardown
{
  DROP DOMAIN IF EXISTS regress_domain_conc;
  DROP SCHEMA regress_domain_schema CASCADE;

}

session "s1"
step "s1_begin"				{ BEGIN; }
step "s1_dom_set_notnull"	{
	ALTER DOMAIN regress_domain_conc SET NOT NULL; }
step "s1_dom_drop_notnull"	{
	ALTER DOMAIN regress_domain_conc DROP NOT NULL; }
step "s1_dom_set_default"	{
	ALTER DOMAIN regress_domain_conc SET DEFAULT 1; }
step "s1_dom_drop_default"	{
	ALTER DOMAIN regress_domain_conc DROP DEFAULT; }
step "s1_dom_add_con"		{
	ALTER DOMAIN regress_domain_conc ADD CONSTRAINT dom_con CHECK (VALUE > 0); }
step "s1_dom_drop_con"		{
	ALTER DOMAIN regress_domain_conc DROP CONSTRAINT dom_con; }
step "s1_dom_validate_con"	{
	ALTER DOMAIN regress_domain_conc VALIDATE CONSTRAINT dom_con; }
step "s1_dom_owner"			{
	ALTER DOMAIN regress_domain_conc OWNER TO CURRENT_USER; }
step "s1_dom_schema_priv"	{
	ALTER DOMAIN regress_domain_conc SET SCHEMA regress_domain_schema; }
step "s1_dom_schema_publ"	{
	ALTER DOMAIN regress_domain_conc SET SCHEMA public; }
step "s1_commit"			{ COMMIT; }

session "s2"
step "s2_begin"			{ BEGIN; }
step "s2_dom_set_notnull"	{
	ALTER DOMAIN regress_domain_conc SET NOT NULL; }
step "s2_dom_drop_notnull"	{
	ALTER DOMAIN regress_domain_conc DROP NOT NULL; }
step "s2_dom_set_default"	{
	ALTER DOMAIN regress_domain_conc SET DEFAULT 1; }
step "s2_dom_drop_default"	{
	ALTER DOMAIN regress_domain_conc DROP DEFAULT; }
step "s2_dom_add_con"		{
	ALTER DOMAIN regress_domain_conc ADD CONSTRAINT dom_con CHECK (VALUE > 0); }
step "s2_dom_add_con2"		{
	ALTER DOMAIN regress_domain_conc ADD CONSTRAINT dom_con2 CHECK (VALUE > 0); }
step "s2_dom_drop_con"		{
	ALTER DOMAIN regress_domain_conc DROP CONSTRAINT dom_con; }
step "s2_dom_validate_con"	{
	ALTER DOMAIN regress_domain_conc VALIDATE CONSTRAINT dom_con; }
step "s2_dom_owner"			{
	ALTER DOMAIN regress_domain_conc OWNER TO CURRENT_USER; }
step "s2_dom_schema_priv"	{
	ALTER DOMAIN regress_domain_conc SET SCHEMA regress_domain_schema; }
step "s2_dom_schema_publ"	{
	ALTER DOMAIN regress_domain_conc SET SCHEMA public; }
step "s2_commit"	{ COMMIT; }

# Round 1 of all permutations using SET NOT NULL as base
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_set_notnull"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_drop_notnull"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_set_default"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_drop_default"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_add_con"
	"s1_commit" "s2_commit"
# No DROP/VALIDATE CONSTRAINT needed as nothing is defined.
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_schema_priv"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_notnull" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"

# Round 2 of all permutations using DROP NOT NULL as base
permutation "s1_begin" "s1_dom_drop_notnull" "s2_begin" "s2_dom_drop_notnull"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_notnull" "s2_begin" "s2_dom_set_default"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_notnull" "s2_begin" "s2_dom_drop_default"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_notnull" "s2_begin" "s2_dom_add_con"
	"s1_commit" "s2_commit"
# No DROP/VALIDATE CONSTRAINT needed as nothing is defined.
permutation "s1_begin" "s1_dom_drop_notnull" "s2_begin" "s2_dom_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_notnull" "s2_begin" "s2_dom_schema_priv"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_notnull" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"

# Round 3 of all permutations using SET DEFAULT as base
permutation "s1_begin" "s1_dom_set_default" "s2_begin" "s2_dom_set_default"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_default" "s2_begin" "s2_dom_drop_default"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_default" "s2_begin" "s2_dom_add_con"
	"s1_commit" "s2_commit"
# No DROP/VALIDATE CONSTRAINT needed as nothing is defined.
permutation "s1_begin" "s1_dom_set_default" "s2_begin" "s2_dom_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_default" "s2_begin" "s2_dom_schema_priv"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_set_default" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"

# Round 4 of all permutations using DROP DEFAULT as base
permutation "s1_begin" "s1_dom_drop_default" "s2_begin" "s2_dom_drop_default"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_default" "s2_begin" "s2_dom_add_con"
	"s1_commit" "s2_commit"
# No DROP/VALIDATE CONSTRAINT needed as nothing is defined.
permutation "s1_begin" "s1_dom_drop_default" "s2_begin" "s2_dom_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_default" "s2_begin" "s2_dom_schema_priv"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_drop_default" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"

# Round 5 of all permutations using ADD CONSTRAINT as a base
permutation "s1_begin" "s1_dom_add_con" "s2_begin" "s2_dom_add_con2"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_add_con" "s2_begin" "s2_dom_drop_con"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_add_con" "s2_begin" "s2_dom_validate_con"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_add_con" "s2_begin" "s2_dom_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_add_con" "s2_begin" "s2_dom_schema_priv"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_add_con" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"

# Round 6 with DROP CONSTRAINT
permutation "s1_dom_add_con" "s1_begin" "s1_dom_drop_con" "s2_begin"
	"s2_dom_drop_con" "s1_commit" "s2_commit"
permutation "s1_dom_add_con" "s1_begin" "s1_dom_drop_con" "s2_begin"
	"s2_dom_validate_con" "s1_commit" "s2_commit"
permutation "s1_dom_add_con" "s1_begin" "s1_dom_drop_con" "s2_begin"
	"s2_dom_owner" "s1_commit" "s2_commit"
permutation "s1_dom_add_con" "s1_begin" "s1_dom_drop_con" "s2_begin"
	"s2_dom_schema_publ" "s1_commit" "s2_commit"
permutation "s1_dom_add_con" "s1_begin" "s1_dom_drop_con" "s2_begin"
	"s2_dom_schema_priv" "s1_commit" "s2_commit"

# Round 7 of all permutations using VALIDATE CONSTRAINT
permutation "s1_dom_add_con" "s1_begin" "s1_dom_validate_con" "s2_begin"
	"s2_dom_validate_con" "s1_commit" "s2_commit"
permutation "s1_dom_add_con" "s1_begin" "s1_dom_validate_con" "s2_begin"
	"s2_dom_owner" "s1_commit" "s2_commit"
permutation "s1_dom_add_con" "s1_begin" "s1_dom_validate_con" "s2_begin"
	"s2_dom_schema_priv" "s1_commit" "s2_commit"
permutation "s1_dom_add_con" "s1_begin" "s1_dom_validate_con" "s2_begin"
	"s2_dom_schema_publ" "s1_commit" "s2_commit"

# Round 8 of all permutations using OWNER TO as base
permutation "s1_begin" "s1_dom_owner" "s2_begin" "s2_dom_owner"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_owner" "s2_begin" "s2_dom_schema_priv"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_owner" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"

# Round 9 with schemas
permutation "s1_begin" "s1_dom_schema_priv" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_schema_publ" "s2_begin" "s2_dom_schema_priv"
	"s1_commit" "s2_commit"
permutation "s1_begin" "s1_dom_schema_publ" "s2_begin" "s2_dom_schema_publ"
	"s1_commit" "s2_commit"
