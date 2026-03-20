# contrib/pyramid/Makefile

# Makefile for building the `pyramid` extension. This file is used by two
# build modes:
#  - in-tree builds (when you run `./configure` in the PostgreSQL source
#    tree) where `top_srcdir` is defined and contrib/global makefiles are
#    included; and
#  - external builds using PGXS (set `USE_PGXS=1`) which query `pg_config`.
#
# The targets, objects and extension metadata below follow the standard
# PostgreSQL extension layout and are intentionally minimal.

MODULE_big = pyramid
OBJS = \
	$(WIN32RES) \
	pyramid.o

EXTENSION = pyramid
DATA = pyramid--1.0.sql
PGFILEDESC = "pyramid - high-dimensional pyramid mapping for btree indexing"

REGRESS = pyramid

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pyramid
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
