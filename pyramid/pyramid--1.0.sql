/* contrib/pyramid/pyramid--1.0.sql
 *
 * SQL installation script for the `pyramid` extension. This file is
 * intended to be loaded via CREATE EXTENSION and not sourced directly from
 * psql. It defines the `pyramid_range` composite type used by the
 * `pyramid_ranges` SRF and the SQL-visible function signatures that map to
 * the C entry points implemented in `pyramid.c`.
 */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pyramid" to load this file. \quit

-- composite type returned by pyramid_ranges(): (pyramid, range_lo, range_hi)
CREATE TYPE pyramid_range AS (
	pyramid int4,
	range_lo float8,
	range_hi float8
);

-- pyramid_value: map a float8[] point -> scalar Pyramid value (float8)
CREATE FUNCTION pyramid_value(v float8[])
RETURNS float8
AS 'MODULE_PATHNAME', 'pyramid_value'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- pyramid_contains: exact refinement predicate (point-in-box)
CREATE FUNCTION pyramid_contains(v float8[], q_lo float8[], q_hi float8[])
RETURNS boolean
AS 'MODULE_PATHNAME', 'pyramid_contains'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- pyramid_ranges: decompose a d-dimensional rectangular query into a set
-- of (pyramid, range_lo, range_hi) intervals suitable for 1-D index scans
CREATE FUNCTION pyramid_ranges(q_lo float8[], q_hi float8[])
RETURNS SETOF pyramid_range
AS 'MODULE_PATHNAME', 'pyramid_ranges'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
