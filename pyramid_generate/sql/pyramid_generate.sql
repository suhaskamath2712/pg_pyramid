-- Regression/demo script for `pyramid_generate`. The script exercises
-- generation counts, checks produced array lengths, verifies values are in
-- [0,1), and verifies error handling for invalid inputs.

CREATE EXTENSION pyramid_generate;

-- generate 10 vectors of dimension 3
SELECT count(*) AS generated_rows
FROM pyramid_generate(3, 10);

-- ensure arrays produced by dimension=5 call all have length 5
SELECT bool_and(array_length(v, 1) = 5) AS all_len_5
FROM pyramid_generate(5, 50) AS g(v);

-- verify values lie in the half-open interval [0,1)
SELECT min(x) >= 0 AND max(x) < 1 AS values_in_unit_interval
FROM (
	SELECT unnest(v) AS x
	FROM pyramid_generate(4, 200) AS g(v)
) s;

-- size=0 should produce zero rows
SELECT count(*) AS generated_zero_rows
FROM pyramid_generate(4, 0);

-- invalid inputs should raise errors (tested in regression harness)
SELECT pyramid_generate(0, 5);
SELECT pyramid_generate(3, -1);

DROP EXTENSION pyramid_generate;
