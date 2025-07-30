--
-- Information related to plan cache
--

--
-- Setup
--
CREATE OR REPLACE FUNCTION select_one_func(int) RETURNS VOID AS $$
DECLARE
    ret INT;
BEGIN
    SELECT $1 INTO ret;
END;
$$ LANGUAGE plpgsql;
CREATE OR REPLACE PROCEDURE select_one_proc(int) AS $$
DECLARE
    ret INT;
BEGIN
    select $1 INTO ret;
END;
$$ LANGUAGE plpgsql;

--
-- plan cache counters for prepared statements
--
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
PREPARE p1 AS SELECT $1;
-- plan cache auto
SET plan_cache_mode TO auto;
EXECUTE p1(1);
-- force generic plan
SET plan_cache_mode TO force_generic_plan;
EXECUTE p1(1);
-- force custom plan
SET plan_cache_mode TO force_custom_plan;
EXECUTE p1(1);
SELECT calls, generic_plan_calls, custom_plan_calls, toplevel, query FROM pg_stat_statements
    ORDER BY query COLLATE "C";
DEALLOCATE p1;

--
-- plan cache counters for extended query protocol
--
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
SELECT $1 \parse p1
SET plan_cache_mode TO auto;
\bind_named p1 1
;
SET plan_cache_mode TO force_generic_plan;
\bind_named p1 1
;
SET plan_cache_mode TO force_custom_plan;
\bind_named p1 1
;
SELECT calls, generic_plan_calls, custom_plan_calls, toplevel, query FROM pg_stat_statements
    ORDER BY query COLLATE "C";
\close_prepared p1

--
-- plan cache counters for explain|explain (analyze) with prepared statements
--
SET pg_stat_statements.track = 'all';
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
PREPARE p1 AS SELECT $1;
-- plan cache auto
SET plan_cache_mode TO auto;
EXPLAIN (COSTS OFF) EXECUTE p1(1);
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF, BUFFERS OFF) EXECUTE p1(1);
-- force generic plan
SET plan_cache_mode TO force_generic_plan;
EXPLAIN (COSTS OFF) EXECUTE p1(1);
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF, BUFFERS OFF) EXECUTE p1(1);
-- force custom plan
SET plan_cache_mode TO force_custom_plan;
EXPLAIN (COSTS OFF) EXECUTE p1(1);
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF, BUFFERS OFF) EXECUTE p1(1);
SELECT calls, generic_plan_calls, custom_plan_calls, toplevel, query FROM pg_stat_statements
    ORDER BY query COLLATE "C";
RESET pg_stat_statements.track;
DEALLOCATE p1;

--
-- plan cache counters for functions and procedures
--
SET pg_stat_statements.track = 'all';
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
-- plan cache auto
SET plan_cache_mode TO auto;
SELECT select_one_func(1);
CALL select_one_proc(1);
-- force generic plan
SET plan_cache_mode TO force_generic_plan;
SELECT select_one_func(1);
CALL select_one_proc(1);
-- force custom plan
SET plan_cache_mode TO force_custom_plan;
SELECT select_one_func(1);
CALL select_one_proc(1);
SELECT calls, generic_plan_calls, custom_plan_calls, toplevel, query FROM pg_stat_statements
    ORDER BY query COLLATE "C";

--
-- plan cache counters for explain|explain (analyze) with functions and procedures
--
SET pg_stat_statements.track = 'all';
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
-- plan cache auto
SET plan_cache_mode TO auto;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT select_one_func(1);
EXPLAIN (COSTS OFF) SELECT select_one_func(1);
CALL select_one_proc(1);
-- force generic plan
SET plan_cache_mode TO force_generic_plan;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT select_one_func(1);
EXPLAIN (COSTS OFF) SELECT select_one_func(1);
CALL select_one_proc(1);
-- force custom plan
SET plan_cache_mode TO force_custom_plan;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT select_one_func(1);
EXPLAIN (COSTS OFF) SELECT select_one_func(1);
CALL select_one_proc(1);
SELECT calls, generic_plan_calls, custom_plan_calls, toplevel, query FROM pg_stat_statements
    ORDER BY query COLLATE "C";

--
-- Cleanup
--
DROP FUNCTION select_one_func(int);
DROP PROCEDURE select_one_proc(int);
