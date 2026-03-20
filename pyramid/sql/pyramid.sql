-- Regression/demo script for the `pyramid` extension. The steps below
-- exercise the mapping (pyramid_value), the range decomposition
-- (pyramid_ranges) and an index-backed neighborhood query with an exact
-- refinement using pyramid_contains.

CREATE EXTENSION pyramid;

-- simple pyramid_value checks (rounded for stable expected output)
SELECT round(pyramid_value(ARRAY[0.10, 0.50, 0.50]::float8[])::numeric, 6) AS pv1;
SELECT round(pyramid_value(ARRAY[0.90, 0.50, 0.50]::float8[])::numeric, 6) AS pv2;
SELECT round(pyramid_value(ARRAY[0.50, 0.80, 0.20]::float8[])::numeric, 6) AS pv3;

-- show the set of 1-D ranges the algorithm decomposes a small 3-D box into
SELECT pyramid,
	round(range_lo::numeric, 6) AS range_lo,
	round(range_hi::numeric, 6) AS range_hi
FROM pyramid_ranges(
	ARRAY[0.10, 0.10, 0.10]::float8[],
	ARRAY[0.40, 0.40, 0.40]::float8[]
)
ORDER BY pyramid;

-- create a small table and index to demonstrate an end-to-end query
CREATE TABLE pyramid_points (
	id int,
	v float8[]
);

INSERT INTO pyramid_points VALUES
	(1, ARRAY[0.15, 0.15, 0.15]::float8[]),
	(2, ARRAY[0.20, 0.25, 0.30]::float8[]),
	(3, ARRAY[0.35, 0.35, 0.35]::float8[]),
	(4, ARRAY[0.41, 0.30, 0.20]::float8[]),
	(5, ARRAY[0.80, 0.80, 0.80]::float8[]);

-- index the expression pyramid_value(v) using a B-tree (internal B+tree)
CREATE INDEX pyramid_points_idx ON pyramid_points USING btree (pyramid_value(v));

-- use the decomposition to find candidate PV ranges, then refine with
-- pyramid_contains to remove false positives
WITH q AS (
	SELECT ARRAY[0.10, 0.10, 0.10]::float8[] AS lo,
		   ARRAY[0.40, 0.40, 0.40]::float8[] AS hi
), r AS (
	SELECT pr.range_lo, pr.range_hi
	FROM q, LATERAL pyramid_ranges(q.lo, q.hi) pr
)
SELECT p.id
FROM pyramid_points p, q, r
WHERE pyramid_value(p.v) BETWEEN r.range_lo AND r.range_hi
  AND pyramid_contains(p.v, q.lo, q.hi)
GROUP BY p.id
ORDER BY p.id;

-- cleanup
DROP TABLE pyramid_points;
DROP EXTENSION pyramid;
