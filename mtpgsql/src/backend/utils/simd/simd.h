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
 *-------------------------------------------------------------------------
 */

#ifndef SIMD_H
#define SIMD_H

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
#define SIMD_INT32_WIDTH  8
#elif HAVE_NEON
#define SIMD_FLOAT8_WIDTH 2
#define SIMD_INT32_WIDTH  4
#else
#define SIMD_FLOAT8_WIDTH 1
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
    __m256d sum = _mm256_setzero_pd();
    int i;
    for (i = 0; i + SIMD_FLOAT8_WIDTH <= n; i += SIMD_FLOAT8_WIDTH) {
        __m256d v = _mm256_loadu_pd(&values[i]);
        sum = _mm256_add_pd(sum, v);
    }
    /* Horizontal sum */
    double tmp[4];
    _mm256_storeu_pd(tmp, sum);
    *result = tmp[0] + tmp[1] + tmp[2] + tmp[3];
    /* Handle tail */
    for (; i < n; i++) {
        *result += values[i];
    }
#elif HAVE_NEON
    float64x2_t sum = vdupq_n_f64(0.0);
    int i;
    for (i = 0; i + SIMD_FLOAT8_WIDTH <= n; i += SIMD_FLOAT8_WIDTH) {
        float64x2_t v = vld1q_f64(&values[i]);
        sum = vaddq_f64(sum, v);
    }
    *result = vaddvq_f64(sum);  /* NEON has horizontal add in newer */
    for (; i < n; i++) {
        *result += values[i];
    }
#else
    /* Scalar fallback */
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += values[i];
    }
    *result = sum;
#endif
}

/*
 * SIMD dot product for vectors (high impact for AI/vector search).
 * Assumes aligned or uses unaligned loads.
 */
static inline double
simd_dot_product(const float *a, const float *b, int dim)
{
#ifdef HAVE_AVX2
    __m256 sum = _mm256_setzero_ps();
    int i;
    for (i = 0; i + 8 <= dim; i += 8) {
        __m256 va = _mm256_loadu_ps(&a[i]);
        __m256 vb = _mm256_loadu_ps(&b[i]);
        sum = _mm256_fmadd_ps(va, vb, sum);  /* fused multiply add */
    }
    float tmp[8];
    _mm256_storeu_ps(tmp, sum);
    double result = tmp[0]+tmp[1]+tmp[2]+tmp[3]+tmp[4]+tmp[5]+tmp[6]+tmp[7];
    for (; i < dim; i++) {
        result += (double)a[i] * b[i];
    }
    return result;
#elif HAVE_NEON
    float32x4_t sum = vdupq_n_f32(0.0f);
    int i;
    for (i = 0; i + 4 <= dim; i += 4) {
        float32x4_t va = vld1q_f32(&a[i]);
        float32x4_t vb = vld1q_f32(&b[i]);
        sum = vmlaq_f32(sum, va, vb);
    }
    float32x2_t sum2 = vadd_f32(vget_low_f32(sum), vget_high_f32(sum));
    double result = vget_lane_f32(vpadd_f32(sum2, sum2), 0);
    for (; i < dim; i++) {
        result += (double)a[i] * b[i];
    }
    return result;
#else
    double sum = 0.0;
    for (int i = 0; i < dim; i++) {
        sum += (double)a[i] * b[i];
    }
    return sum;
#endif
}

/* Add more primitives as needed: compare, hash, etc. */

#endif /* SIMD_H */