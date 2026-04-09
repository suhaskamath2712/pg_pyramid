# Pyramid PostgreSQL Extensions

## Introduction

This project is an implementation-inspired extension set based on the Pyramid-Technique introduced in the SIGMOD'98 paper by Berchtold, Böhm, and Kriegel. The core idea is to map high-dimensional points into a single scalar key so that standard 1D index structures (here, PostgreSQL `btree`) can still be used for efficient candidate retrieval. For range queries, the high-dimensional query box is decomposed into a small set of 1D intervals, and exact filtering is then applied to remove false positives.

In short, this repository translates the paper’s concept into practical PostgreSQL extension functions for:

- dimensionality-reducing key mapping (`pyramid_value`)
- range decomposition (`pyramid_ranges`)
- exact refinement (`pyramid_contains`)
- synthetic dataset generation (`pyramid_generate`)

### ACM reference

Stefan Berchtold, Christian Böhm, and Hans-Peter Kriegal. 1998. The pyramid-technique: towards breaking the curse of dimensionality. In Proceedings of the 1998 ACM SIGMOD international conference on Management of data (SIGMOD '98). Association for Computing Machinery, New York, NY, USA, 142–153. https://doi.org/10.1145/276304.276318

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

## Upgrade existing `pyramid` installs (e.g. 1.0 -> 1.1)

If you already have `pyramid` installed in one or more databases, upgrading has **two parts**:

1. install new extension files on the server host (shared library + SQL scripts)
2. run `ALTER EXTENSION` inside each target database

The new `1.1` release adds planner support hooks (`SUPPORT pyramid_support`) to improve planner row/selectivity/cost estimation.

### Step 1: install updated extension files on the PostgreSQL server host

From this repository:

```bash
cd pyramid
make USE_PGXS=1
sudo make USE_PGXS=1 install
```

This copies:

- `pyramid.so` to PostgreSQL's library directory (`$pkglibdir`)
- `pyramid--1.1.sql` and `pyramid--1.0--1.1.sql` to PostgreSQL's extension directory (`$sharedir/extension`)

Notes:

- If you build against PostgreSQL 16, install into the PostgreSQL 16 instance you are actually running.
- Running `make install` is cluster-level; it does **not** upgrade individual databases yet.

### Step 2: upgrade the extension in each database

Connect to each database where `pyramid` is installed and run:

```sql
SELECT extname, extversion
FROM pg_extension
WHERE extname = 'pyramid';

ALTER EXTENSION pyramid UPDATE TO '1.1';
```

You can use `ALTER EXTENSION pyramid UPDATE;` as well, but `UPDATE TO '1.1'` is explicit and safer for controlled rollouts.

Important:

- Extension upgrades are **per database** (not global). Repeat for every DB that has `pyramid`.
- You need sufficient privileges (typically superuser or extension owner, depending on setup).

### Step 3: verify upgrade success

Check installed and available versions:

```sql
SELECT extname, extversion
FROM pg_extension
WHERE extname = 'pyramid';

SELECT name, version, installed
FROM pg_available_extension_versions
WHERE name = 'pyramid'
ORDER BY version;
```

Confirm support hooks are attached:

```sql
SELECT proname,
			 prosupport::regproc AS support_function
FROM pg_proc
WHERE proname IN ('pyramid_value', 'pyramid_contains', 'pyramid_ranges')
ORDER BY proname;
```

Expected `support_function`: `pyramid_support`.

### Step 4: refresh stats and re-check plans

After upgrade, refresh table stats and inspect plans again:

```sql
ANALYZE points;
EXPLAIN (ANALYZE, BUFFERS)
WITH q AS (
		SELECT ARRAY[0.10,0.10,0.10,0.10,0.10,0.10,0.10,0.10]::float8[] AS lo,
					 ARRAY[0.50,0.50,0.50,0.50,0.50,0.50,0.50,0.50]::float8[] AS hi
), r AS (
		SELECT pr.range_lo, pr.range_hi
		FROM q, LATERAL pyramid_ranges(q.lo, q.hi) pr
)
SELECT p.id
FROM points p, q, r
WHERE pyramid_value(p.v) BETWEEN r.range_lo AND r.range_hi
	AND pyramid_contains(p.v, q.lo, q.hi)
GROUP BY p.id
ORDER BY p.id;
```

### Troubleshooting

- **"No update path from version '1.0' to version '1.1'"**
	- New SQL upgrade files are not installed on the server used by your running cluster.
	- Re-run `sudo make USE_PGXS=1 install` from `pyramid/` on that host.

- **`ALTER EXTENSION` succeeds but sessions still behave like old code**
	- Existing backend processes may keep old shared library code loaded.
	- Reconnect application sessions (or recycle pool workers) to ensure new backends load updated `pyramid.so`.

- **Permission errors**
	- Run upgrade as a role that owns the extension (or superuser where required).

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

## Python experiment script

A convenience script `run_pyramid_experiment.py` is included at the repository root to run a small end-to-end experiment. The script:

- Connects to PostgreSQL using the hardcoded `DB_CONFIG` in the script.
- Ensures the `pyramid` and `pyramid_generate` extensions exist in the target database.
- Creates a table (default name `pyramid`), populates it using `pyramid_generate`, builds a B-tree expression index on `pyramid_value(v)`, and runs the standard Pyramid candidate+refinement query both without and with the index.
- Measures wall-clock execution time for each run and verifies that both result sets are identical.

Prerequisites:

- Python 3.8+ and `psycopg2` (development use: `pip install psycopg2-binary`).

Quick start / usage:

1. Edit `run_pyramid_experiment.py` to adjust `DB_CONFIG` (host, dbname, user, password) and `ROW_COUNT` if you want fewer rows for a fast test.
2. (Optional) Change the query bounds by modifying `query_lo` and `query_hi` in the script's `main()`; by default the script uses `lo = [0.25]*dims` and `hi = [0.75]*dims`.
3. Run the script and pass the desired dimensionality with `-d/--dims`:

```bash
python3 run_pyramid_experiment.py --dims 8
```

The script prints:

- total matches and wall-clock time for the query executed *without* the index
- whether the planner used the Pyramid index (via `EXPLAIN (FORMAT JSON)`)
- total matches and wall-clock time for the query executed *with* the index
- a verification that both result sets are equal (the script exits non-zero on mismatch)

Notes:

- The script issues `CREATE EXTENSION IF NOT EXISTS pyramid` and `CREATE EXTENSION IF NOT EXISTS pyramid_generate` in the target database; the DB user must have sufficient privileges.
- `ROW_COUNT` defaults to a large value (1,000,000) — reduce it for quicker iterations during development.

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
