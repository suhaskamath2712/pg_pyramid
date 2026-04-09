/*-------------------------------------------------------------------------
 *
 * pyramid.c
 *    Pyramid-Technique mapping and query decomposition for PostgreSQL
 *
 * Overview / purpose
 *  - Implements the "Pyramid-Technique" (Berchtold et al.) mapping that
 *    converts a d-dimensional point v in [0,1]^d into a single float8
 *    Pyramid value (pv). The idea is to map the d-dim space into 2d
 *    pyramids and use the one-dimensional value pv = (pyramid_index + h)
 *    as an indexable key (btree).
 *
 * Exposed functions (SQL wrappers are in pyramid--1.0.sql)
 *  - pyramid_value(v float8[]) -> float8
 *      Compute scalar Pyramid value for point v.
 *  - pyramid_ranges(q_lo float8[], q_hi float8[]) -> SETOF pyramid_range
 *      Decompose an axis-aligned d-dimensional range query into a set of
 *      one-dimensional intervals over pyramid_value. Each returned row has
 *      (pyramid, range_lo, range_hi) and callers should combine the
 *      intervals across pyramids when querying the index.
 *  - pyramid_contains(v float8[], q_lo float8[], q_hi float8[]) -> boolean
 *      Exact refinement predicate: checks whether point v lies inside the
 *      d-dimensional rectangular query window [q_lo, q_hi]. Use this to
 *      filter false positives returned by the 1-D index scan.
 *
 * Implementation notes
 *  - arrays are expected to be one-dimensional float8[] and all arrays
 *    passed together must have the same length (dimension).
 *  - The mapping chooses the dimension with the largest absolute deviation
 *    from 0.5 (the center) as the pyramid axis; the pyramid index is
 *    argmax for values < 0.5 or argmax + d for values >= 0.5. The height h
 *    is the absolute deviation from 0.5 and pv = pyramid + h.
 *  - The range decomposition follows the paper's approach: for each
 *    pyramid (2d candidates) compute the allowable h interval for which
 *    any point inside the query box may map into that pyramid; return only
 *    valid intervals with h in [0,0.5].
 *
 * Safety and robustness
 *  - functions validate array dimensions and disallow NULL elements.
 *  - input range endpoints are validated (lo <= hi for each dimension).
 *  - the SRF (pyramid_ranges) uses a per-call memory context for results.
 *
 * Keys are intended to be indexed with PostgreSQL's btree access method,
 * which uses a B+tree structure internally.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <math.h>

#include "catalog/pg_type_d.h"
#include "fmgr.h"
#include "funcapi.h"
#include "nodes/supportnodes.h"
#include "optimizer/optimizer.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/selfuncs.h"

/*
 * PG_MODULE_MAGIC_EXT was added in newer PostgreSQL versions.
 * Use PG_MODULE_MAGIC on PG16 for compatibility.
 */
#if PG_VERSION_NUM >= 170000
PG_MODULE_MAGIC_EXT(
					.name = "pyramid",
					.version = PG_VERSION
);
#else
PG_MODULE_MAGIC;
#endif

PG_FUNCTION_INFO_V1(pyramid_value);
PG_FUNCTION_INFO_V1(pyramid_contains);
PG_FUNCTION_INFO_V1(pyramid_ranges);
PG_FUNCTION_INFO_V1(pyramid_support);

typedef struct PyramidRangeItem
{
	int			pyramid;
	float8		range_lo;
	float8		range_hi;
} PyramidRangeItem;

typedef struct PyramidRangesCtx
{
	int			nranges;
	int			next;
	PyramidRangeItem *ranges;
} PyramidRangesCtx;

static void extract_float8_array(ArrayType *arr, float8 **vals, int *ndims);
static void validate_same_dims(int ndims_a, int ndims_b, const char *what_a, const char *what_b);
static inline float8 minabs_interval(float8 lo, float8 hi);
static void append_range(PyramidRangesCtx *ctx, int pyramid, float8 lo, float8 hi);
static bool extract_const_float8_array(Node *node, PlannerInfo *root, float8 **vals, int *ndims);
static bool estimate_query_box_selectivity(Node *lo_node, Node *hi_node,
										   PlannerInfo *root, Selectivity *sel_out);
static double estimate_ranges_rows(Node *arg_node, PlannerInfo *root);

/*
 * extract_float8_array
 *  - Validate that `arr` is a one-dimensional float8[] with no NULLs.
 *  - Allocate a contiguous C array and copy values into *vals.
 *  - Write the element count into *ndims.
 *
 * This helper centralizes all array validation and conversion so callers
 * can assume a plain C array and a dimension count.
 */

static void
extract_float8_array(ArrayType *arr, float8 **vals, int *ndims)
{
	int			nelems;
	Datum	   *elems;
	bool	   *nulls;
	int			null_count = 0;

	if (ARR_NDIM(arr) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("input must be a one-dimensional float8[]")));

	if (ARR_ELEMTYPE(arr) != FLOAT8OID)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("input must be float8[]")));

	*ndims = ArrayGetNItems(ARR_NDIM(arr), ARR_DIMS(arr));

	if (*ndims <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("array must not be empty")));

	deconstruct_array(arr,
					  FLOAT8OID,
					  sizeof(float8),
					  FLOAT8PASSBYVAL,
					  TYPALIGN_DOUBLE,
					  &elems,
					  &nulls,
					  &nelems);

	for (int i = 0; i < nelems; i++)
	{
		if (nulls[i])
			null_count++;
	}

	if (null_count > 0)
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("input arrays must not contain NULL elements")));

	*vals = palloc(sizeof(float8) * nelems);
	for (int i = 0; i < nelems; i++)
		(*vals)[i] = DatumGetFloat8(elems[i]);

	pfree(elems);
	pfree(nulls);
}

static void
validate_same_dims(int ndims_a, int ndims_b, const char *what_a, const char *what_b)
{
	if (ndims_a != ndims_b)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("dimension mismatch between %s (%d) and %s (%d)",
						what_a, ndims_a, what_b, ndims_b)));
}

static inline float8
minabs_interval(float8 lo, float8 hi)
{
	if (lo <= 0.0 && hi >= 0.0)
		return 0.0;

	return Min(fabs(lo), fabs(hi));
}

static void
append_range(PyramidRangesCtx *ctx, int pyramid, float8 lo, float8 hi)
{
	ctx->ranges[ctx->nranges].pyramid = pyramid;
	ctx->ranges[ctx->nranges].range_lo = lo;
	ctx->ranges[ctx->nranges].range_hi = hi;
	ctx->nranges++;
}

/*
 * Planner support helpers
 *
 * These helpers provide planner-visible cardinality/selectivity/cost
 * estimates for the SQL-callable functions in this extension.
 */

static bool
extract_const_float8_array(Node *node, PlannerInfo *root, float8 **vals, int *ndims)
{
	Node	   *est_node = node;
	Const	  *c;
	ArrayType  *arr;
	Datum	   *elems;
	bool	   *nulls;
	int			nelems;

	*vals = NULL;
	*ndims = 0;

	if (root != NULL)
		est_node = estimate_expression_value(root, node);

	if (est_node == NULL || !IsA(est_node, Const))
		return false;

	c = (Const *) est_node;
	if (c->constisnull || c->consttype != FLOAT8ARRAYOID)
		return false;

	arr = DatumGetArrayTypeP(c->constvalue);
	if (ARR_NDIM(arr) != 1 || ARR_ELEMTYPE(arr) != FLOAT8OID)
		return false;

	*ndims = ArrayGetNItems(ARR_NDIM(arr), ARR_DIMS(arr));
	if (*ndims <= 0)
		return false;

	deconstruct_array(arr,
					  FLOAT8OID,
					  sizeof(float8),
					  FLOAT8PASSBYVAL,
					  TYPALIGN_DOUBLE,
					  &elems,
					  &nulls,
					  &nelems);

	*vals = palloc(sizeof(float8) * nelems);
	for (int i = 0; i < nelems; i++)
	{
		if (nulls[i])
		{
			pfree(*vals);
			*vals = NULL;
			*ndims = 0;
			pfree(elems);
			pfree(nulls);
			return false;
		}

		(*vals)[i] = DatumGetFloat8(elems[i]);
	}

	pfree(elems);
	pfree(nulls);
	return true;
}

static bool
estimate_query_box_selectivity(Node *lo_node, Node *hi_node,
									PlannerInfo *root, Selectivity *sel_out)
{
	float8	 *lo;
	float8	 *hi;
	int			d_lo;
	int			d_hi;
	Selectivity sel = 1.0;

	if (!extract_const_float8_array(lo_node, root, &lo, &d_lo))
		return false;

	if (!extract_const_float8_array(hi_node, root, &hi, &d_hi))
	{
		pfree(lo);
		return false;
	}

	if (d_lo != d_hi)
	{
		pfree(lo);
		pfree(hi);
		return false;
	}

	for (int i = 0; i < d_lo; i++)
	{
		float8		width;

		if (lo[i] > hi[i])
		{
			sel = 0.0;
			break;
		}

		width = hi[i] - lo[i];
		width = Max(0.0, Min(1.0, width));
		sel *= width;
	}

	pfree(lo);
	pfree(hi);

	CLAMP_PROBABILITY(sel);
	*sel_out = sel;
	return true;
}

static double
estimate_ranges_rows(Node *arg_node, PlannerInfo *root)
{
	float8	 *vals;
	int			dims;
	double		rows;

	if (!extract_const_float8_array(arg_node, root, &vals, &dims))
		return -1.0;

	rows = clamp_row_est((double) (2 * dims));
	pfree(vals);
	return rows;
}

Datum
pyramid_support(PG_FUNCTION_ARGS)
{
	Node	   *rawreq = (Node *) PG_GETARG_POINTER(0);

	if (rawreq == NULL)
		PG_RETURN_POINTER(NULL);

	if (IsA(rawreq, SupportRequestRows))
	{
		SupportRequestRows *req = (SupportRequestRows *) rawreq;

		if (req->node != NULL && IsA(req->node, FuncExpr))
		{
			FuncExpr   *fexpr = (FuncExpr *) req->node;

			if (list_length(fexpr->args) == 2)
			{
				double rows = estimate_ranges_rows((Node *) linitial(fexpr->args), req->root);

				if (rows > 0.0)
				{
					req->rows = rows;
					PG_RETURN_POINTER(req);
				}
			}
		}

		PG_RETURN_POINTER(NULL);
	}

	if (IsA(rawreq, SupportRequestSelectivity))
	{
		SupportRequestSelectivity *req = (SupportRequestSelectivity *) rawreq;

		/* Restriction case only: function selectivity for WHERE clause */
		if (!req->is_join && list_length(req->args) == 3)
		{
			Selectivity sel;

			if (estimate_query_box_selectivity((Node *) lsecond(req->args),
										  (Node *) lthird(req->args),
										  req->root,
										  &sel))
			{
				req->selectivity = sel;
				PG_RETURN_POINTER(req);
			}
		}

		PG_RETURN_POINTER(NULL);
	}

	if (IsA(rawreq, SupportRequestCost))
	{
		SupportRequestCost *req = (SupportRequestCost *) rawreq;
		int			nargs = get_func_nargs(req->funcid);
		Oid			rettype = get_func_rettype(req->funcid);
		double		dims = 8.0; /* fallback when argument cardinality is unknown */

		req->startup = 0.0;

		if (req->node != NULL && IsA(req->node, FuncExpr))
		{
			FuncExpr   *fexpr = (FuncExpr *) req->node;

			if (list_length(fexpr->args) > 0)
			{
				float8	 *dummy_vals;
				int			parsed_dims;

				if (extract_const_float8_array((Node *) linitial(fexpr->args),
											  req->root,
											  &dummy_vals,
											  &parsed_dims))
				{
					dims = (double) parsed_dims;
					pfree(dummy_vals);
				}
			}
		}

		/* pyramid_value(v): one pass to find argmax distance from center */
		if (rettype == FLOAT8OID && nargs == 1)
		{
			req->per_tuple = (2.0 + 6.0 * dims) * cpu_operator_cost;
			PG_RETURN_POINTER(req);
		}

		/* pyramid_contains(v, lo, hi): bound checks per dimension */
		if (rettype == BOOLOID && nargs == 3)
		{
			req->per_tuple = (2.0 + 4.0 * dims) * cpu_operator_cost;
			PG_RETURN_POINTER(req);
		}

		/* pyramid_ranges(lo, hi): SRF setup plus small per-output-row work */
		if (nargs == 2)
		{
			req->startup = (10.0 + 12.0 * dims) * cpu_operator_cost;
			req->per_tuple = 2.0 * cpu_operator_cost;
			PG_RETURN_POINTER(req);
		}

		PG_RETURN_POINTER(NULL);
	}

	PG_RETURN_POINTER(NULL);
}

Datum
pyramid_value(PG_FUNCTION_ARGS)
{
	ArrayType  *v_arr = PG_GETARG_ARRAYTYPE_P(0);
	float8	   *v;
	int			d;
	int			argmax = 0;
	float8		maxdev;
	int			pyramid;
	float8		h;
	float8		pv;

	extract_float8_array(v_arr, &v, &d);

	maxdev = fabs(0.5 - v[0]);
	for (int i = 1; i < d; i++)
	{
		float8 dev = fabs(0.5 - v[i]);

		if (dev > maxdev)
		{
			maxdev = dev;
			argmax = i;
		}
	}

	if (v[argmax] < 0.5)
		pyramid = argmax;
	else
		pyramid = argmax + d;

	h = maxdev;
	pv = (float8) pyramid + h;

	PG_RETURN_FLOAT8(pv);
}

Datum
pyramid_contains(PG_FUNCTION_ARGS)
{
	ArrayType  *v_arr = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *lo_arr = PG_GETARG_ARRAYTYPE_P(1);
	ArrayType  *hi_arr = PG_GETARG_ARRAYTYPE_P(2);
	float8	   *v;
	float8	   *lo;
	float8	   *hi;
	int			d_v;
	int			d_lo;
	int			d_hi;

	extract_float8_array(v_arr, &v, &d_v);
	extract_float8_array(lo_arr, &lo, &d_lo);
	extract_float8_array(hi_arr, &hi, &d_hi);

	validate_same_dims(d_v, d_lo, "point", "lower bound");
	validate_same_dims(d_v, d_hi, "point", "upper bound");

	for (int i = 0; i < d_v; i++)
	{
		if (lo[i] > hi[i])
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid interval at dimension %d: lower bound is greater than upper bound", i + 1)));

		if (v[i] < lo[i] || v[i] > hi[i])
			PG_RETURN_BOOL(false);
	}

	PG_RETURN_BOOL(true);
}

Datum
pyramid_ranges(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	PyramidRangesCtx *ctx;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		ArrayType  *lo_arr = PG_GETARG_ARRAYTYPE_P(0);
		ArrayType  *hi_arr = PG_GETARG_ARRAYTYPE_P(1);
		float8	   *lo;
		float8	   *hi;
		int			d_lo;
		int			d_hi;
		float8	   *c_lo;
		float8	   *c_hi;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		extract_float8_array(lo_arr, &lo, &d_lo);
		extract_float8_array(hi_arr, &hi, &d_hi);
		validate_same_dims(d_lo, d_hi, "lower bound", "upper bound");

		c_lo = palloc(sizeof(float8) * d_lo);
		c_hi = palloc(sizeof(float8) * d_lo);
		for (int j = 0; j < d_lo; j++)
		{
			if (lo[j] > hi[j])
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid interval at dimension %d: lower bound is greater than upper bound", j + 1)));

			c_lo[j] = lo[j] - 0.5;
			c_hi[j] = hi[j] - 0.5;
		}

		ctx = palloc0(sizeof(PyramidRangesCtx));
		ctx->ranges = palloc0(sizeof(PyramidRangeItem) * 2 * d_lo);
		ctx->nranges = 0;
		ctx->next = 0;

		for (int p = 0; p < 2 * d_lo; p++)
		{
			int			dim = p % d_lo;
			bool		upper = (p >= d_lo);
			float8		base_lo;
			float8		base_hi;
			float8		other_req = 0.0;
			float8		h_lo;
			float8		h_hi;

			if (upper)
			{
				base_lo = Max(0.0, c_lo[dim]);
				base_hi = c_hi[dim];
			}
			else
			{
				base_lo = Max(0.0, -c_hi[dim]);
				base_hi = -c_lo[dim];
			}

			if (base_lo > base_hi)
				continue;

			for (int j = 0; j < d_lo; j++)
			{
				float8 req;

				if (j == dim)
					continue;

				req = minabs_interval(c_lo[j], c_hi[j]);
				if (req > other_req)
					other_req = req;
			}

			h_lo = Max(base_lo, other_req);
			h_hi = base_hi;

			h_lo = Max(0.0, h_lo);
			h_hi = Min(0.5, h_hi);

			if (h_lo <= h_hi)
				append_range(ctx, p, (float8) p + h_lo, (float8) p + h_hi);
		}

		funcctx->user_fctx = ctx;

		if (get_call_result_type(fcinfo, NULL, &funcctx->tuple_desc) != TYPEFUNC_COMPOSITE)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("function returning record called in context that cannot accept type record")));

		funcctx->tuple_desc = BlessTupleDesc(funcctx->tuple_desc);

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	ctx = (PyramidRangesCtx *) funcctx->user_fctx;

	if (ctx->next < ctx->nranges)
	{
		Datum		values[3];
		bool		nulls[3] = {false, false, false};
		HeapTuple	tuple;
		Datum		result;
		PyramidRangeItem *r = &ctx->ranges[ctx->next++];

		values[0] = Int32GetDatum(r->pyramid);
		values[1] = Float8GetDatum(r->range_lo);
		values[2] = Float8GetDatum(r->range_hi);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}

	SRF_RETURN_DONE(funcctx);
}
