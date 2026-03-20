/* contrib/pyramid_generate/pyramid_generate--1.0.sql
 *
 * SQL installation script for the `pyramid_generate` extension. It
 * defines the generator SRF used to create uniformly distributed test
 * vectors. Load via CREATE EXTENSION rather than sourcing directly.
 */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pyramid_generate" to load this file. \quit

-- pyramid_generate(dimension, size): return `size` arrays each of length
-- `dimension` containing random values in [0,1). The function is marked
-- VOLATILE because it uses a PRNG; it is STRICT (rejects NULL inputs) and
-- PARALLEL RESTRICTED because the generator uses global PRNG state.
CREATE FUNCTION pyramid_generate(dimension int4, size int8)
RETURNS SETOF float8[]
AS 'MODULE_PATHNAME', 'pyramid_generate'
LANGUAGE C VOLATILE STRICT PARALLEL RESTRICTED;
