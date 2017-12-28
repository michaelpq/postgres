# Test for interactions with DDL commands manipulating event trigger
# objects.
# The following set of commands can interact in concurrency:
# - ALTER EVENT TRIGGER ENABLE
# - ALTER EVENT TRIGGER DISABLE
# - ALTER EVENT TRIGGER OWNER TO

setup
{
  CREATE OR REPLACE FUNCTION notice_any_command()
    RETURNS event_trigger
    LANGUAGE plpgsql
    AS $$
      BEGIN
      RAISE NOTICE 'command % is run', tg_tag;
      END;
    $$;
  CREATE EVENT TRIGGER notice_ddl ON ddl_command_start
     EXECUTE PROCEDURE notice_any_command();
}

teardown
{
  DROP EVENT TRIGGER notice_ddl;
  DROP FUNCTION notice_any_command();
}

session "s1"
step "s1_begin"		{ BEGIN; }
step "s1_disable"	{ ALTER EVENT TRIGGER notice_ddl DISABLE; }
step "s1_enable"	{ ALTER EVENT TRIGGER notice_ddl ENABLE REPLICA; }
step "s1_owner"		{ ALTER EVENT TRIGGER notice_ddl OWNER TO CURRENT_USER; }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_begin"		{ BEGIN; }
step "s2_disable"	{ ALTER EVENT TRIGGER notice_ddl DISABLE; }
step "s2_enable"	{ ALTER EVENT TRIGGER notice_ddl ENABLE REPLICA; }
step "s2_owner"		{ ALTER EVENT TRIGGER notice_ddl OWNER TO CURRENT_USER; }
step "s2_commit"	{ COMMIT; }

permutation "s1_begin" "s1_disable" "s2_begin" "s2_disable"
  "s1_commit" "s2_commit"
permutation "s1_begin" "s1_disable" "s2_begin" "s2_enable"
  "s1_commit" "s2_commit"
permutation "s1_begin" "s1_disable" "s2_begin" "s2_owner"
  "s1_commit" "s2_commit"
permutation "s1_begin" "s1_enable" "s2_begin" "s2_enable"
  "s1_commit" "s2_commit"
permutation "s1_begin" "s1_enable" "s2_begin" "s2_owner"
  "s1_commit" "s2_commit"
permutation "s1_begin" "s1_owner" "s2_begin" "s2_owner"
  "s1_commit" "s2_commit"
