# Test that a KEY SHARE lock taken by an foreign key check is not lost
# when a concurrent non-key UPDATE is inside its TOAST step.

setup
{
  CREATE TABLE pk_toast (
    id int PRIMARY KEY,
    payload text
  );
  ALTER TABLE pk_toast ALTER COLUMN payload SET STORAGE EXTERNAL;

  CREATE TABLE fk_child (
    cid serial PRIMARY KEY,
    pid int REFERENCES pk_toast(id)
  );

  INSERT INTO pk_toast VALUES (1, 'small');

  -- Rename the TOAST table to a deterministic name.
  SET allow_system_table_mods TO true;
  DO $$DECLARE r record;
  BEGIN
    SELECT INTO r reltoastrelid::regclass::text AS table_name FROM pg_class
      WHERE oid = 'pk_toast'::regclass;
    EXECUTE 'ALTER TABLE ' || r.table_name || ' RENAME TO pk_toast_toast;';
  END$$;
  RESET allow_system_table_mods;
}

teardown
{
  DROP TABLE fk_child, pk_toast;
}

# REINDEX blocking TOAST inserts.
session s1
step s1_reindex  { BEGIN; REINDEX TABLE pg_toast.pk_toast_toast; }
step s1_commit   { COMMIT; }

# Non-key UPDATE requiring TOAST.
session s2
step s2_update   { UPDATE pk_toast SET payload = repeat('x', 10000) WHERE id = 1; }

# Insert a child row, foreign-key check taking KEY SHARE on parent.
session s3
step s3_insert   { BEGIN; INSERT INTO fk_child(pid) VALUES (1); }
step s3_commit   { COMMIT; }

# Delete parent row, blocked on KEY SHARE due to s3.
session s4
step s4_delete   { DELETE FROM pk_toast WHERE id = 1; }

permutation s1_reindex s2_update s3_insert s1_commit s4_delete s3_commit
