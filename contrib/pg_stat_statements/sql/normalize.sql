--
-- Validate normalization of constants
--

-- Ensure that there are no gaps in the generated $n parameters. The following
-- queries will record some constant location one or more times.
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
SELECT WHERE '1' IN ('1'::int, '3'::int::text);
SELECT WHERE (1, 2) IN ((1, 2), (2, 3));
SELECT WHERE (3, 4) IN ((5, 6), (8, 7));
SELECT query, calls FROM pg_stat_statements ORDER BY query COLLATE "C";

-- With bind queries
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
-- Unique query IDs
SELECT WHERE ($1::int, 7) IN ((8, $2::int), ($3::int, 9)) \bind '1' '2' '3' \g
SELECT WHERE ($2::int, 10) IN ((11, $3::int), ($1::int, 12)) \bind '1' '2' '3' \g
SELECT WHERE $1::int IN ($2::int, $3::int) \bind '1' '2' '3' \g
SELECT WHERE $2::int IN ($3::int, $1::int) \bind '1' '2' '3' \g
SELECT WHERE $3::int IN ($1::int, $2::int) \bind '1' '2' '3' \g
-- Two groups of two queries with the same query ID.
SELECT WHERE '1'::int IN ($1::int, '2'::int) \bind '1' \g
SELECT WHERE '4'::int IN ($1::int, '5'::int) \bind '2' \g
SELECT WHERE $2::int IN ($1::int, '1'::int) \bind '1' '2' \g
SELECT WHERE $2::int IN ($1::int, '2'::int) \bind '3' '4' \g

SELECT query, calls FROM pg_stat_statements ORDER BY query COLLATE "C";
