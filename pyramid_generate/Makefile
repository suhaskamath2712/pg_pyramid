# contrib/pyramid_generate/Makefile

# Makefile for building the `pyramid_generate` extension. Supports both
# in-tree and PGXS external builds. The SRF in this module is intended for
# testing and benchmark data generation, not for production data import.

MODULE_big = pyramid_generate
OBJS = \
	$(WIN32RES) \
	pyramid_generate.o

EXTENSION = pyramid_generate
DATA = pyramid_generate--1.0.sql
PGFILEDESC = "pyramid_generate - generate uniform random high-dimensional arrays"

REGRESS = pyramid_generate

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pyramid_generate
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
