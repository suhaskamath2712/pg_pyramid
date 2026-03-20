/*-------------------------------------------------------------------------
 *
 * pyramid_generate.c
 *    Generator SRF to create uniformly distributed float8[] vectors for
 *    testing and benchmarking the Pyramid indexing extension.
 *
 * Purpose and behaviour
 *  - Exposes a set-returning function `pyramid_generate(dimension int4,
 *    size int8) RETURNS SETOF float8[]` which yields `size` arrays of length
 *    `dimension` containing uniformly distributed values in [0,1).
 *  - The function validates its inputs and uses PostgreSQL's portable
 *    pseudo-random generator state (`pg_global_prng_state`) for reproducible
 *    behaviour inside the backend.
 *
 * Memory model
 *  - The SRF uses the standard per-call memory context (multi_call_memory_ctx)
 *    for its small per-call state (dimension and size). Each returned
 *    array is created with `construct_array` and returned to the caller; we
 *    pfree temporary C arrays after constructing the SQL array object.
 *
 * Error handling
 *  - Negative `dimension` or `size` values raise ERROR with clear messages.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/pg_type.h"
#include "common/pg_prng.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"

/*
 * PG_MODULE_MAGIC_EXT was added in newer PostgreSQL versions.
 * Use PG_MODULE_MAGIC on PG16 for compatibility.
 */
#if PG_VERSION_NUM >= 170000
PG_MODULE_MAGIC_EXT(
					.name = "pyramid_generate",
					.version = PG_VERSION
);
#else
PG_MODULE_MAGIC;
#endif

PG_FUNCTION_INFO_V1(pyramid_generate);

typedef struct PyramidGenerateCtx
{
	int32		dimension;
	int64		size;
} PyramidGenerateCtx;

Datum
pyramid_generate(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	PyramidGenerateCtx *fctx;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		int32		dimension = PG_GETARG_INT32(0);
		int64		size = PG_GETARG_INT64(1);

		if (dimension <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("dimension must be greater than zero")));

		if (size < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("size cannot be negative")));

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		fctx = (PyramidGenerateCtx *) palloc(sizeof(PyramidGenerateCtx));
		fctx->dimension = dimension;
		fctx->size = size;
		funcctx->user_fctx = fctx;
		funcctx->max_calls = size;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	fctx = (PyramidGenerateCtx *) funcctx->user_fctx;

	if (funcctx->call_cntr < funcctx->max_calls)
	{
		int32		dim = fctx->dimension;
		Datum	   *elems;
		ArrayType  *arr;

		elems = palloc(sizeof(Datum) * dim);
		for (int32 i = 0; i < dim; i++)
		{
			float8 v = pg_prng_double(&pg_global_prng_state);

			elems[i] = Float8GetDatum(v);
		}

		arr = construct_array(elems,
							  dim,
							  FLOAT8OID,
							  sizeof(float8),
							  FLOAT8PASSBYVAL,
							  TYPALIGN_DOUBLE);

		pfree(elems);
		SRF_RETURN_NEXT(funcctx, PointerGetDatum(arr));
	}

	SRF_RETURN_DONE(funcctx);
}
