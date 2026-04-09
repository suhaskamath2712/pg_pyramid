/* contrib/pyramid/pyramid--1.1.sql
 *
 * SQL installation script for the `pyramid` extension (version 1.1).
 *
 * This version adds a planner support function to improve cardinality,
 * selectivity and cost estimation for pyramid_value/pyramid_contains/
 * pyramid_ranges.
 */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pyramid" to load this file. \quit

-- composite type returned by pyramid_ranges(): (pyramid, range_lo, range_hi)
CREATE TYPE pyramid_range AS (
	pyramid int4,
	range_lo float8,
	range_hi float8
);

-- planner support callback for Pyramid functions
CREATE FUNCTION pyramid_support(internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'pyramid_support'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- pyramid_value: map a float8[] point -> scalar Pyramid value (float8)
CREATE FUNCTION pyramid_value(v float8[])
RETURNS float8
AS 'MODULE_PATHNAME', 'pyramid_value'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE
SUPPORT pyramid_support;

-- pyramid_contains: exact refinement predicate (point-in-box)
CREATE FUNCTION pyramid_contains(v float8[], q_lo float8[], q_hi float8[])
RETURNS boolean
AS 'MODULE_PATHNAME', 'pyramid_contains'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE
SUPPORT pyramid_support;

-- pyramid_ranges: decompose a d-dimensional rectangular query into a set
-- of (pyramid, range_lo, range_hi) intervals suitable for 1-D index scans
CREATE FUNCTION pyramid_ranges(q_lo float8[], q_hi float8[])
RETURNS SETOF pyramid_range
AS 'MODULE_PATHNAME', 'pyramid_ranges'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE
SUPPORT pyramid_support;
