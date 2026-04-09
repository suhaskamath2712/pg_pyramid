/* contrib/pyramid/pyramid--1.0--1.1.sql
 *
 * Upgrade script from pyramid 1.0 to 1.1.
 * Adds planner support hooks for improved planner estimates.
 */

-- planner support callback for Pyramid functions
CREATE OR REPLACE FUNCTION pyramid_support(internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'pyramid_support'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- attach planner support callback to Pyramid SQL functions
CREATE OR REPLACE FUNCTION pyramid_value(v float8[])
RETURNS float8
AS 'MODULE_PATHNAME', 'pyramid_value'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE
SUPPORT pyramid_support;

CREATE OR REPLACE FUNCTION pyramid_contains(v float8[], q_lo float8[], q_hi float8[])
RETURNS boolean
AS 'MODULE_PATHNAME', 'pyramid_contains'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE
SUPPORT pyramid_support;

CREATE OR REPLACE FUNCTION pyramid_ranges(q_lo float8[], q_hi float8[])
RETURNS SETOF pyramid_range
AS 'MODULE_PATHNAME', 'pyramid_ranges'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE
SUPPORT pyramid_support;
