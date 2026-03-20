# Pyramid PostgreSQL Extensions

This repository contains a trimmed, extension-only layout for two PostgreSQL extensions:

- `pyramid`: high-dimensional Pyramid mapping and range decomposition for btree indexing
- `pyramid_generate`: uniform random `float8[]` vector generator for tests/benchmarks

## Layout

- `pyramid/` — extension source, control/SQL install script, optional regression files
- `pyramid_generate/` — extension source, control/SQL install script, optional regression files

Each extension directory includes the PGXS build inputs (`Makefile`, `*.c`, `*.control`, `--*.sql`) and test assets (`sql/`, `expected/`).

## Prerequisites

- PostgreSQL server development package for your target server version
- `pg_config` in `PATH`
- toolchain: `make` + C compiler

On Debian/Ubuntu (example for PostgreSQL 16):

- `sudo apt-get install postgresql-server-dev-16`

## Build and install (PGXS)

Build/install each extension from its own directory:

1. `cd pyramid`
2. `make USE_PGXS=1`
3. `make USE_PGXS=1 install`
4. `cd ../pyramid_generate`
5. `make USE_PGXS=1`
6. `make USE_PGXS=1 install`

Useful checks:

- `pg_config --version`
- `pg_config --sharedir`
- `pg_config --pkglibdir`

## Enable in SQL

- `CREATE EXTENSION pyramid;`
- `CREATE EXTENSION pyramid_generate;`

## Example usage

Generate sample vectors:

```sql
CREATE TABLE points (
		id bigserial PRIMARY KEY,
		v  float8[] NOT NULL
);

INSERT INTO points(v)
SELECT v
FROM pyramid_generate(8, 50000::bigint) AS t(v);
```

Index and query with Pyramid decomposition:

```sql
CREATE INDEX points_pyramid_idx
ON points USING btree (pyramid_value(v));

WITH q AS (
	SELECT ARRAY[0.10,0.10,0.10,0.10,0.10,0.10,0.10,0.10]::float8[] AS lo,
				 ARRAY[0.50,0.50,0.50,0.50,0.50,0.50,0.50,0.50]::float8[] AS hi
), r AS (
	SELECT pr.range_lo, pr.range_hi
	FROM q, LATERAL pyramid_ranges(q.lo, q.hi) pr
)
SELECT DISTINCT p.id
FROM points p, q, r
WHERE pyramid_value(p.v) BETWEEN r.range_lo AND r.range_hi
	AND pyramid_contains(p.v, q.lo, q.hi)
ORDER BY p.id;
```

## Optional tests

From each extension directory:

- `make USE_PGXS=1 installcheck`

## Pyramid function reference

- `pyramid_value(v float8[]) -> float8`  
	Maps a normalized $d$-dimensional point in $[0,1]^d$ to a 1D Pyramid key.

- `pyramid_ranges(q_lo float8[], q_hi float8[]) -> SETOF pyramid_range`  
	Decomposes a $d$-dimensional rectangular query into up to $2d$ one-dimensional candidate ranges.

- `pyramid_contains(v float8[], q_lo float8[], q_hi float8[]) -> boolean`  
	Exact refinement predicate that checks whether a point is inside query bounds.

## Pyramid query pattern

1. Keep vectors normalized to `[0,1]^d`.
2. Build a btree expression index on `pyramid_value(v)`.
3. Query candidates using `pyramid_ranges(lo, hi)` and refine with `pyramid_contains(...)`.

## Notes

- PostgreSQL core source is not required for this layout.
- Keep vectors normalized to `[0,1]` and dimensions consistent across data/query bounds.
- Arrays are expected to be 1-D `float8[]` without NULL elements.
- `pyramid_ranges` can return conservative candidate intervals; `pyramid_contains` performs exact filtering.
