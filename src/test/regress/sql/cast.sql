--
-- CAST and CASTABLE tests
--
CREATE TEMPORARY TABLE castable_strings(
  id integer GENERATED ALWAYS AS IDENTITY,
  strval text);

INSERT INTO castable_strings(strval)
VALUES ('abc'),
        ('123'),
        ('09/24/1991');

-- plain CAST
SELECT CAST('error' AS integer);

-- pedantic restating of default behavior
SELECT CAST('error' AS integer ERROR ON CONVERSION ERROR);

-- NULL
SELECT CAST('error' AS integer NULL ON CONVERSION ERROR);

-- constant expression
SELECT CAST('error' AS integer DEFAULT 42 ON CONVERSION ERROR);

-- expression
SELECT CAST(cs.strval AS integer DEFAULT cs.id ON CONVERSION ERROR)
FROM castable_strings AS cs
ORDER BY cs.id;

-- castable
SELECT cs.strval
FROM castable_strings AS cs
WHERE cs.strval IS CASTABLE AS integer;

-- not castable
SELECT cs.strval
FROM castable_strings AS cs
WHERE cs.strval IS NOT CASTABLE AS integer;

-- text to array
SELECT CAST('{123,abc,456}' AS integer[] DEFAULT '{-789}' ON CONVERSION ERROR) as array_test1;

-- array to array
SELECT CAST('{234,def,567}'::text[] AS integer[] DEFAULT '{-1011}' ON CONVERSION ERROR) as array_test2;

-- i don't remember why we had this one
SELECT CAST(u.arg AS integer DEFAULT -1 ON ERROR) AS unnest_test1
FROM unnest('{345,ghi,678}'::text[]) AS u(arg);
