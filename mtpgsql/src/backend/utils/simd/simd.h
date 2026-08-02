/*-------------------------------------------------------------------------
 *
 * simd.h
 *	  SIMD (vector) primitives for high-performance data processing in WeaverDB.
 *
 * This provides portable access to SIMD instructions (AVX2 on x86, NEON on ARM)
 * for use in executor, storage, and utility code.
 *
 * High-impact uses:
 * - Vectorized numeric aggregates (SUM, etc.)
 * - SIMD hashing for joins/aggs
 * - Fast filters in scans
 * - Vector similarity (dot product, distances) for AI/vector workloads
 *
 * Usage: Include this and use the functions. They fall back to scalar if
 * SIMD not available at compile/runtime.
 *
 * Note: There is no SIMT/GPU path in WeaverDB; ANN distance kernels use CPU
 * SIMD only (see access/pgvector).
 *
 *-------------------------------------------------------------------------
 */

#ifndef SIMD_H
#define SIMD_H

#include <math.h>

#include "c.h"

/* Detect SIMD support */
#if defined(__AVX2__)
#define HAVE_AVX2 1
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#define HAVE_NEON 1
#include <arm_neon.h>
#else
#define HAVE_SCALAR_ONLY 1
#endif

/*
 * SIMD width in elements for common types.
 * These are the maximum for the platform; code should handle variable.
 */
#ifdef HAVE_AVX2
#define SIMD_FLOAT8_WIDTH 4   /* 256-bit / 64-bit */
#define SIMD_FLOAT4_WIDTH 8   /* 256-bit / 32-bit */
#define SIMD_INT32_WIDTH  8
#elif defined(HAVE_NEON)
#define SIMD_FLOAT8_WIDTH 2
#define SIMD_FLOAT4_WIDTH 4
#define SIMD_INT32_WIDTH  4
#else
#define SIMD_FLOAT8_WIDTH 1
#define SIMD_FLOAT4_WIDTH 1
#define SIMD_INT32_WIDTH  1
#endif

/*
 * Example: SIMD sum for float8 (double).
 * Processes as many as possible; caller handles remainder.
 */
static inline void
simd_sum_float8(const double *values, int n, double *result)
{
#ifdef HAVE_AVX2
	__m256d		sum = _mm256_setzero_pd();
	int			i;

	for (i = 0; i + SIMD_FLOAT8_WIDTH <= n; i += SIMD_FLOAT8_WIDTH)
	{
		__m256d		v = _mm256_loadu_pd(&values[i]);

		sum = _mm256_add_pd(sum, v);
	}
	/* Horizontal sum */
	{
		double		tmp[4];

		_mm256_storeu_pd(tmp, sum);
		*result = tmp[0] + tmp[1] + tmp[2] + tmp[3];
	}
	/* Handle tail */
	for (; i < n; i++)
		*result += values[i];
#elif defined(HAVE_NEON)
	float64x2_t sum = vdupq_n_f64(0.0);
	int			i;

	for (i = 0; i + SIMD_FLOAT8_WIDTH <= n; i += SIMD_FLOAT8_WIDTH)
	{
		float64x2_t v = vld1q_f64(&values[i]);

		sum = vaddq_f64(sum, v);
	}
	*result = vaddvq_f64(sum);	/* aarch64 horizontal add */
	for (; i < n; i++)
		*result += values[i];
#else
	/* Scalar fallback */
	{
		double		sum = 0.0;
		int			i;

		for (i = 0; i < n; i++)
			sum += values[i];
		*result = sum;
	}
#endif
}

/*
 * Horizontal sum of 8/4 float lanes → float.
 */
#ifdef HAVE_AVX2
static inline float
simd_hsum_f32_avx(__m256 v)
{
	float		tmp[8];

	_mm256_storeu_ps(tmp, v);
	return tmp[0] + tmp[1] + tmp[2] + tmp[3] + tmp[4] + tmp[5] + tmp[6] + tmp[7];
}
#endif

#ifdef HAVE_NEON
static inline float
simd_hsum_f32_neon(float32x4_t v)
{
	float32x2_t		sum2 = vadd_f32(vget_low_f32(v), vget_high_f32(v));

	return vget_lane_f32(vpadd_f32(sum2, sum2), 0);
}
#endif

/*
 * float32 inner product (dot). Primary kernel for pgvector IP / neg-IP.
 * Accumulates in float32 to match scalar VectorInnerProduct semantics.
 */
static inline float
simd_inner_product_f32(const float *a, const float *b, int dim)
{
#ifdef HAVE_AVX2
	__m256		sum = _mm256_setzero_ps();
	int			i;
	float		result;

	for (i = 0; i + 8 <= dim; i += 8)
	{
		__m256		va = _mm256_loadu_ps(&a[i]);
		__m256		vb = _mm256_loadu_ps(&b[i]);

#if defined(__FMA__)
		sum = _mm256_fmadd_ps(va, vb, sum);
#else
		sum = _mm256_add_ps(sum, _mm256_mul_ps(va, vb));
#endif
	}
	result = simd_hsum_f32_avx(sum);
	for (; i < dim; i++)
		result += a[i] * b[i];
	return result;
#elif defined(HAVE_NEON)
	float32x4_t sum = vdupq_n_f32(0.0f);
	int			i;
	float		result;

	for (i = 0; i + 4 <= dim; i += 4)
	{
		float32x4_t va = vld1q_f32(&a[i]);
		float32x4_t vb = vld1q_f32(&b[i]);

		sum = vmlaq_f32(sum, va, vb);
	}
	result = simd_hsum_f32_neon(sum);
	for (; i < dim; i++)
		result += a[i] * b[i];
	return result;
#else
	{
		float		sum = 0.0f;
		int			i;

		for (i = 0; i < dim; i++)
			sum += a[i] * b[i];
		return sum;
	}
#endif
}

/*
 * float32 L2 squared distance. Primary kernel for pgvector L2 / L2² ANN path.
 */
static inline float
simd_l2_squared_f32(const float *a, const float *b, int dim)
{
#ifdef HAVE_AVX2
	__m256		sum = _mm256_setzero_ps();
	int			i;
	float		result;

	for (i = 0; i + 8 <= dim; i += 8)
	{
		__m256		va = _mm256_loadu_ps(&a[i]);
		__m256		vb = _mm256_loadu_ps(&b[i]);
		__m256		diff = _mm256_sub_ps(va, vb);

#if defined(__FMA__)
		sum = _mm256_fmadd_ps(diff, diff, sum);
#else
		sum = _mm256_add_ps(sum, _mm256_mul_ps(diff, diff));
#endif
	}
	result = simd_hsum_f32_avx(sum);
	for (; i < dim; i++)
	{
		float		diff = a[i] - b[i];

		result += diff * diff;
	}
	return result;
#elif defined(HAVE_NEON)
	float32x4_t sum = vdupq_n_f32(0.0f);
	int			i;
	float		result;

	for (i = 0; i + 4 <= dim; i += 4)
	{
		float32x4_t va = vld1q_f32(&a[i]);
		float32x4_t vb = vld1q_f32(&b[i]);
		float32x4_t diff = vsubq_f32(va, vb);

		sum = vmlaq_f32(sum, diff, diff);
	}
	result = simd_hsum_f32_neon(sum);
	for (; i < dim; i++)
	{
		float		diff = a[i] - b[i];

		result += diff * diff;
	}
	return result;
#else
	{
		float		sum = 0.0f;
		int			i;

		for (i = 0; i < dim; i++)
		{
			float		diff = a[i] - b[i];

			sum += diff * diff;
		}
		return sum;
	}
#endif
}

/*
 * float32 L1 (Manhattan) distance.
 */
static inline float
simd_l1_distance_f32(const float *a, const float *b, int dim)
{
#ifdef HAVE_AVX2
	__m256		sum = _mm256_setzero_ps();
	__m256		sign = _mm256_set1_ps(-0.0f);
	int			i;
	float		result;

	for (i = 0; i + 8 <= dim; i += 8)
	{
		__m256		va = _mm256_loadu_ps(&a[i]);
		__m256		vb = _mm256_loadu_ps(&b[i]);
		__m256		diff = _mm256_sub_ps(va, vb);

		/* abs via clear sign bit */
		sum = _mm256_add_ps(sum, _mm256_andnot_ps(sign, diff));
	}
	result = simd_hsum_f32_avx(sum);
	for (; i < dim; i++)
		result += fabsf(a[i] - b[i]);
	return result;
#elif defined(HAVE_NEON)
	float32x4_t sum = vdupq_n_f32(0.0f);
	int			i;
	float		result;

	for (i = 0; i + 4 <= dim; i += 4)
	{
		float32x4_t va = vld1q_f32(&a[i]);
		float32x4_t vb = vld1q_f32(&b[i]);

		sum = vaddq_f32(sum, vabsq_f32(vsubq_f32(va, vb)));
	}
	result = simd_hsum_f32_neon(sum);
	for (; i < dim; i++)
		result += fabsf(a[i] - b[i]);
	return result;
#else
	{
		float		sum = 0.0f;
		int			i;

		for (i = 0; i < dim; i++)
			sum += fabsf(a[i] - b[i]);
		return sum;
	}
#endif
}

/*
 * SIMD dot product for vectors (double accumulator for general use).
 * Prefers float32 SIMD lanes then promotes; falls back to scalar.
 */
static inline double
simd_dot_product(const float *a, const float *b, int dim)
{
	return (double) simd_inner_product_f32(a, b, dim);
}

#endif							/* SIMD_H */
