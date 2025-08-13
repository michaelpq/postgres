CREATE EXTENSION IF NOT EXISTS test_bitmapset;

-- BASIC FUNCTIONALITY TESTS

-- Test bms_values function

SELECT 'values NULL' as test, bms_values(NULL) as result;
SELECT 'values empty' as test, bms_values(test_bms_from_array(ARRAY[]::integer[])) as result;
SELECT 'values singleton' as test, bms_values(test_bms_make_singleton(42)) as result;
SELECT 'values small set' as test, bms_values(test_bms_from_array(ARRAY[1,3,5])) as result;
SELECT 'values larger set' as test, bms_values(test_bms_from_array(ARRAY[0,5,10,15,20])) as result;
SELECT 'values unsorted input' as test, bms_values(test_bms_from_array(ARRAY[20,5,15,0,10])) as result;

-- Test 1: Basic utility functions
SELECT 'NULL input to from_array' as test, test_bms_from_array(NULL) IS NULL as result;
SELECT 'Empty array to from_array' as test, test_bms_from_array(ARRAY[]::integer[]) IS NULL as result;
SELECT 'NULL input to to_array' as test, test_bms_to_array(NULL) IS NULL as result;

-- Test 2: Singleton operations
SELECT 'make_singleton(42)' as test, test_bms_to_array(test_bms_make_singleton(42)) = ARRAY[42] as result;
SELECT 'make_singleton(0)' as test, test_bms_to_array(test_bms_make_singleton(0)) = ARRAY[0] as result;
SELECT 'make_singleton(1000)' as test, test_bms_to_array(test_bms_make_singleton(1000)) = ARRAY[1000] as result;

-- Test 3: Add member operations
SELECT 'add_member(NULL, 10)' as test, test_bms_to_array(test_bms_add_member(NULL, 10)) = ARRAY[10] as result;
SELECT 'add_member consistency' as test, bms_values(test_bms_add_member(NULL, 10)) as values;
SELECT 'add_member to existing' as test, test_bms_to_array(test_bms_add_member(test_bms_make_singleton(5), 10)) = ARRAY[5,10] as result;
SELECT 'add_member sorted' as test, test_bms_to_array(test_bms_add_member(test_bms_make_singleton(10), 5)) = ARRAY[5,10] as result;
SELECT 'add_member idempotent' as test, bms_values(test_bms_add_member(test_bms_make_singleton(10), 10)) as values;


-- Test 4: Delete member operations
SELECT 'del_member from NULL' as test, test_bms_del_member(NULL, 10) IS NULL as result;
SELECT 'del_member singleton becomes empty' as test, test_bms_del_member(test_bms_make_singleton(10), 10) IS NULL as result;
SELECT 'del_member no change' as test, bms_values(test_bms_del_member(test_bms_make_singleton(10), 5)) as values;
SELECT 'del_member from middle' as test, test_bms_to_array(test_bms_del_member(test_bms_from_array(ARRAY[1,2,3]), 2)) = ARRAY[1,3] as result;
SELECT 'del_member triggers realloc' as test, test_bms_del_member(test_bms_del_member(test_bms_from_array(ARRAY[0,31,32,63,64]), 32), 63) as result;
SELECT 'del_member word boundary' as test,
       bms_values(test_bms_del_member(
           test_bms_add_range(NULL, 30, 34),
           32
       )) as result;

-- SET OPERATIONS TESTS

-- Test 5: Union operations
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135,
	test_bms_from_array(ARRAY[3,5,7]) AS set_357,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'union overlapping sets' as test,
       test_bms_to_array(test_bms_union(set_135, set_357)) = ARRAY[1,3,5,7] as result
FROM test_sets
UNION ALL
SELECT 'union with NULL' as test,
       test_bms_to_array(test_bms_union(set_135, NULL)) = ARRAY[1,3,5] as result
FROM test_sets
UNION ALL
SELECT 'union NULL with NULL' as test,
       test_bms_union(NULL, NULL) IS NULL as result
FROM test_sets;
SELECT 'overlapping ranges' as test,
       bms_values(test_bms_union(
           test_bms_add_range(NULL, 50, 150),
           test_bms_add_range(NULL, 100, 200)
       )) as result;

-- Test 6: Intersection operations
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135,
	test_bms_from_array(ARRAY[3,5,7]) AS set_357,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'intersect overlapping sets' as test,
       test_bms_to_array(test_bms_intersect(set_135, set_357)) = ARRAY[3,5] as result
FROM test_sets
UNION ALL
SELECT 'intersect disjoint sets' as test,
       test_bms_intersect(set_135, set_246) IS NULL as result
FROM test_sets
UNION ALL
SELECT 'intersect with NULL' as test,
       test_bms_intersect(set_135, NULL) IS NULL as result
FROM test_sets;

-- Test bms_int_members
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135,
	test_bms_from_array(ARRAY[3,5,7]) AS set_357,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'int(ersect) overlapping sets' as test,
       test_bms_to_array(test_bms_int_members(set_135, set_357)) = ARRAY[3,5] as result
FROM test_sets
UNION ALL
SELECT 'int(ersect) disjoint sets' as test,
       test_bms_int_members(set_135, set_246) IS NULL as result
FROM test_sets
UNION ALL
SELECT 'int(ersect) with NULL' as test,
       test_bms_int_members(set_135, NULL) IS NULL as result
FROM test_sets;
SELECT 'int(ersect) members' as test,
       bms_values(test_bms_int_members(
           test_bms_from_array(ARRAY[0,31,32,63,64]),
           test_bms_from_array(ARRAY[31,32,64,65])
       )) as result;

-- Test 7: Difference operations
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135,
	test_bms_from_array(ARRAY[3,5,7]) AS set_357,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'difference overlapping sets' as test,
       test_bms_to_array(test_bms_difference(set_135, set_357)) = ARRAY[1] as result
FROM test_sets
UNION ALL
SELECT 'difference disjoint sets' as test,
       test_bms_to_array(test_bms_difference(set_135, set_246)) = ARRAY[1,3,5] as result
FROM test_sets
UNION ALL
SELECT 'difference identical sets' as test,
       test_bms_difference(set_135, set_135) IS NULL as result
FROM test_sets;
SELECT 'difference subtraction edge case' as test,
       bms_values(test_bms_difference(
           test_bms_add_range(NULL, 0, 100),
           test_bms_add_range(NULL, 50, 150)
       )) as result;
SELECT 'difference subtract to empty' as test,
       bms_values(test_bms_difference(
           test_bms_make_singleton(42),
           test_bms_make_singleton(42)
       )) as result;

-- MEMBERSHIP AND COMPARISON TESTS

-- Test 8: Membership tests
WITH test_set AS (SELECT test_bms_from_array(ARRAY[1,3,5]) AS set_135)
SELECT 'is_member existing' as test, test_bms_is_member(set_135, 1) = true as result FROM test_set
UNION ALL
SELECT 'is_member missing' as test, test_bms_is_member(set_135, 2) = false as result FROM test_set
UNION ALL
SELECT 'is_member existing middle' as test, test_bms_is_member(set_135, 3) = true as result FROM test_set
UNION ALL
SELECT 'is_member NULL set' as test, test_bms_is_member(NULL, 1) = false as result FROM test_set;

-- Test 9: Set cardinality
SELECT 'num_members NULL' as test, test_bms_num_members(NULL) = 0 as result;
SELECT 'num_members small set' as test, test_bms_num_members(test_bms_from_array(ARRAY[1,3,5])) = 3 as result;
SELECT 'num_members larger set' as test, test_bms_num_members(test_bms_from_array(ARRAY[2,4,6,8,10])) = 5 as result;

-- Test 10: Set equality and comparison
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_a,
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_b,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246,
	test_bms_from_array(ARRAY[1,3]) AS set_13
)
SELECT 'equal NULL NULL' as test, test_bms_equal(NULL, NULL) = true as result FROM test_sets
UNION ALL
SELECT 'equal NULL set' as test, test_bms_equal(NULL, set_135_a) = false as result FROM test_sets
UNION ALL
SELECT 'equal set NULL' as test, test_bms_equal(set_135_a, NULL) = false as result FROM test_sets
UNION ALL
SELECT 'equal identical sets' as test, test_bms_equal(set_135_a, set_135_b) = true as result FROM test_sets
UNION ALL
SELECT 'equal different sets' as test, test_bms_equal(set_135_a, set_246) = false as result FROM test_sets
UNION ALL
SELECT 'compare NULL NULL' as test, test_bms_compare(NULL, NULL) = 0 as result FROM test_sets
UNION ALL
SELECT 'compare NULL set' as test, test_bms_compare(NULL, set_13) = -1 as result FROM test_sets
UNION ALL
SELECT 'compare set NULL' as test, test_bms_compare(set_13, NULL) = 1 as result FROM test_sets
UNION ALL
SELECT 'compare equal sets' as test, test_bms_compare(set_13, set_13) = 0 as result FROM test_sets
UNION ALL
SELECT 'compare subset superset' as test, test_bms_compare(set_13, set_135_a) = -1 as result FROM test_sets
UNION ALL
SELECT 'compare superset subset' as test, test_bms_compare(set_135_a, set_13) = 1 as result FROM test_sets;
SELECT 'compare edge case' as test,
       test_bms_compare(
           test_bms_add_range(NULL, 0, 63),
           test_bms_add_range(NULL, 0, 64)
       ) as result;

-- ADVANCED OPERATIONS TESTS

-- Test 11: Range operations
SELECT 'add_range basic' as test, test_bms_to_array(test_bms_add_range(NULL, 5, 7)) = ARRAY[5,6,7] as result;
SELECT 'add_range single element' as test, test_bms_to_array(test_bms_add_range(NULL, 5, 5)) = ARRAY[5] as result;
SELECT 'add_range to existing' as test, test_bms_to_array(test_bms_add_range(test_bms_from_array(ARRAY[1,10]), 5, 7)) = ARRAY[1,5,6,7,10] as result;
SELECT 'add_range at word boundary 31' as test, bms_values(test_bms_add_range(NULL, 30, 34)) as result;
SELECT 'add_range at word boundary 63' as test, bms_values(test_bms_add_range(NULL, 62, 66)) as result;
SELECT 'add_range large range' as test, bms_values(test_bms_add_range(NULL, 0, 1000)) as result;
SELECT 'add_range force realloc test 1' as test, bms_values(test_bms_add_range(NULL, 0, 200)) as result;
SELECT 'add_range foce realloc test 2' as test, bms_values(test_bms_add_range(NULL, 1000, 1100)) as result;

-- Test 12: Membership types
SELECT 'membership empty' as test, test_bms_membership(NULL) = 0 as result;
SELECT 'membership singleton' as test, test_bms_membership(test_bms_from_array(ARRAY[42])) = 1 as result;
SELECT 'membership multiple' as test, test_bms_membership(test_bms_from_array(ARRAY[1,2,3])) = 2 as result;

-- Test 13: Singleton member extraction
SELECT 'singleton_member valid' as test, test_bms_singleton_member(test_bms_from_array(ARRAY[42])) = 42 as result;

-- Test 14: Set iteration
WITH test_set AS (SELECT test_bms_from_array(ARRAY[5,10,15,20]) AS set_numbers)
SELECT 'next_member first' as test, test_bms_next_member(set_numbers, -1) = 5 as result FROM test_set
UNION ALL
SELECT 'next_member second' as test, test_bms_next_member(set_numbers, 5) = 10 as result FROM test_set
UNION ALL
SELECT 'next_member past end' as test, test_bms_next_member(set_numbers, 20) = -2 as result FROM test_set
UNION ALL
SELECT 'next_member empty set' as test, test_bms_next_member(NULL, -1) = -2 as result FROM test_set
UNION ALL
SELECT 'prev_member last' as test, test_bms_prev_member(set_numbers, 21) = 20 as result FROM test_set
UNION ALL
SELECT 'prev_member penultimate' as test, test_bms_prev_member(set_numbers, 20) = 15 as result FROM test_set
UNION ALL
SELECT 'prev_member past beginning' as test, test_bms_prev_member(set_numbers, 5) = -2 as result FROM test_set
UNION ALL
SELECT 'prev_member empty set' as test, test_bms_prev_member(NULL, 100) = -2 as result FROM test_set;

-- Test 15: Hash functions
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135a,
	test_bms_from_array(ARRAY[1,3,5]) AS set_135b,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'hash NULL' as test, test_bms_hash_value(NULL) = 0 as result FROM test_sets
UNION ALL
SELECT 'hash consistency' as test, test_bms_hash_value(set_135a) = test_bms_hash_value(set_135b) as result FROM test_sets
UNION ALL
SELECT 'hash different sets' as test, test_bms_hash_value(set_135a) != test_bms_hash_value(set_246) as result FROM test_sets;

-- Test 16: Set overlap
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135,
	test_bms_from_array(ARRAY[3,5,7]) AS set_357,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'overlap existing' as test, test_bms_overlap(set_135, set_357) = true as result FROM test_sets
UNION ALL
SELECT 'overlap none' as test, test_bms_overlap(set_135, set_246) = false as result FROM test_sets
UNION ALL
SELECT 'overlap with NULL' as test, test_bms_overlap(NULL, set_135) = false as result FROM test_sets;

-- Test 17: Subset relations
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3]) AS set_13,
	test_bms_from_array(ARRAY[1,3,5]) AS set_135,
	test_bms_from_array(ARRAY[2,4]) AS set_24,
	test_bms_add_range(NULL, 0, 31) AS rng_0_31, -- 1 word
	test_bms_add_range(NULL, 0, 63) as rng_0_63  -- 2 words
)
SELECT 'subset NULL is subset of all' as test, test_bms_is_subset(NULL, set_135) = true as result FROM test_sets
UNION ALL
SELECT 'subset proper subset' as test, test_bms_is_subset(set_13, set_135) = true as result FROM test_sets
UNION ALL
SELECT 'subset improper subset' as test, test_bms_is_subset(set_135, set_13) = false as result FROM test_sets
UNION ALL
SELECT 'subset disjoint sets' as test, test_bms_is_subset(set_13, set_24) = false as result FROM test_sets
UNION ALL
SELECT 'subset comparison edge' as test, test_bms_is_subset(rng_0_31, rng_0_63) = true as result FROM test_sets;

-- Test 18: Copy operations
WITH test_set AS (SELECT test_bms_from_array(ARRAY[1,3,5,7]) AS original)
SELECT 'copy NULL' as test, test_bms_copy(NULL) IS NULL as result FROM test_set
UNION ALL
SELECT 'copy equality' as test, test_bms_equal(original, test_bms_copy(original)) = true as result FROM test_set;

-- Test 19: Add members operation
WITH base_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3]) AS set_13,
	test_bms_from_array(ARRAY[5,7]) AS set_57
)
SELECT 'add_members operation' as test,
       test_bms_to_array(test_bms_add_members(set_13, set_57)) = ARRAY[1,3,5,7] as result
FROM base_sets;
SELECT 'add members complex' as test,
       bms_values(test_bms_add_members(
           test_bms_from_array(ARRAY[1,3,5]),
           test_bms_from_array(ARRAY[100,200,300])
       )) as result;

-- Test 20: Test hash consistency

WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_a,
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_b,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'bitmap_hash NULL' as test,
       test_bitmap_hash(NULL) = 0 as result
UNION ALL
SELECT 'bitmap_hash consistency' as test,
       test_bitmap_hash(set_135_a) = test_bitmap_hash(set_135_b) as result
FROM test_sets
UNION ALL
SELECT 'bitmap_hash vs bms_hash_value' as test,
       test_bitmap_hash(set_135_a) = test_bms_hash_value(set_135_a) as result
FROM test_sets
UNION ALL
SELECT 'bitmap_hash different sets' as test,
       test_bitmap_hash(set_135_a) != test_bitmap_hash(set_246) as result
FROM test_sets;

-- Test 21: bitmap_match function

WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_a,
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_b,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246,
	test_bms_from_array(ARRAY[1,3]) AS set_13
)
SELECT 'bitmap_match NULL NULL (should be 0)' as test,
       test_bitmap_match(NULL, NULL) = 0 as result
UNION ALL
SELECT 'bitmap_match NULL set (should be 1)' as test,
       test_bitmap_match(NULL, set_135_a) = 1 as result
FROM test_sets
UNION ALL
SELECT 'bitmap_match set NULL (should be 1)' as test,
       test_bitmap_match(set_135_a, NULL) = 1 as result
FROM test_sets
UNION ALL
SELECT 'bitmap_match identical sets (should be 0)' as test,
       test_bitmap_match(set_135_a, set_135_b) = 0 as result
FROM test_sets
UNION ALL
SELECT 'bitmap_match different sets (should be 1)' as test,
       test_bitmap_match(set_135_a, set_246) = 1 as result
FROM test_sets
UNION ALL
SELECT 'bitmap_match subset/superset (should be 1)' as test,
       test_bitmap_match(set_13, set_135_a) = 1 as result
FROM test_sets;

-- Test relationship with bms_equal
WITH test_sets AS (
    SELECT
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_a,
	test_bms_from_array(ARRAY[1,3,5]) AS set_135_b,
	test_bms_from_array(ARRAY[2,4,6]) AS set_246
)
SELECT 'bitmap_match vs bms_equal (equal sets)' as test,
       (test_bitmap_match(set_135_a, set_135_b) = 0) = test_bms_equal(set_135_a, set_135_b) as result
FROM test_sets
UNION ALL
SELECT 'bitmap_match vs bms_equal (different sets)' as test,
       (test_bitmap_match(set_135_a, set_246) = 0) = test_bms_equal(set_135_a, set_246) as result
FROM test_sets
UNION ALL
SELECT 'bitmap_match vs bms_equal (NULL cases)' as test,
       (test_bitmap_match(NULL, NULL) = 0) = test_bms_equal(NULL, NULL) as result
FROM test_sets;

-- Test specific match values for debugging
SELECT 'bitmap_match [1,3,5] vs [1,3,5]' as test,
       test_bitmap_match(test_bms_from_array(ARRAY[1,3,5]), test_bms_from_array(ARRAY[1,3,5])) as match_value;
SELECT 'bitmap_match [1,3,5] vs [2,4,6]' as test,
       test_bitmap_match(test_bms_from_array(ARRAY[1,3,5]), test_bms_from_array(ARRAY[2,4,6])) as match_value;

-- Test edge cases
SELECT 'bitmap_match empty arrays' as test,
       test_bitmap_match(test_bms_from_array(ARRAY[]::integer[]), test_bms_from_array(ARRAY[]::integer[])) = 0 as result;

-- Test 22: Overlap lists with various scenarios
WITH test_lists AS (
    SELECT ARRAY[
        test_bms_from_array(ARRAY[1,2,3]),
        test_bms_from_array(ARRAY[4,5,6]),
        test_bms_from_array(ARRAY[7,8,9]),
        test_bms_make_singleton(2)  -- This should overlap with first set
    ] as bms_list
)
SELECT 'overlap list complex' as test,
       test_bms_overlap_list(test_bms_from_array(ARRAY[2,10]), bms_list) as result
FROM test_lists;

SELECT 'overlap empty list' as test,
       test_bms_overlap_list(test_bms_make_singleton(1), ARRAY[]::text[]) as result;

-- Test 23: Random operations stress test
SELECT 'random operations' as test, test_random_operations(-1, 10000, 81920, 0) > 0 as result;

-- ERROR CONDITION TESTS

-- Test 24: Error conditions (these should produce ERRORs)
SELECT test_bms_make_singleton(-1);
SELECT test_bms_add_member(test_bms_make_singleton(1), -1);
SELECT test_bms_is_member(NULL, -5);
SELECT test_bms_add_member(NULL, -10);
SELECT test_bms_del_member(NULL, -20);
SELECT test_bms_add_range(NULL, -5, 10);
SELECT test_bms_singleton_member(test_bms_from_array(ARRAY[1,2]));
SELECT bms_values(test_bms_add_member(NULL, 1000));

DROP EXTENSION test_bitmapset;
