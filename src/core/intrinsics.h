#pragma once

#if (_M_IX86_FP == 2 || defined(_M_AMD64) || defined(_M_X64))
#include <emmintrin.h>
#include <xmmintrin.h>
#define WB_HAS_SSE   1
#define WB_V4F_TYPE  __m128
#define WB_V4I_TYPE  __m128i
#define WB_V4FM_TYPE __m128
#define WB_V4IM_TYPE __m128i
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm64_neon.h>
#define WB_HAS_NEON  1
#define WB_V4F_TYPE  float32x4_t
#define WB_V4I_TYPE  int32x4_t
#define WB_V4FM_TYPE int32x4_t
#define WB_V4IM_TYPE int32x4_t
#endif

// Primitive type
using v4f = WB_V4F_TYPE;
using v4i = WB_V4I_TYPE;

// Mask type
using v4fm = WB_V4FM_TYPE;
using v4im = WB_V4IM_TYPE;

#if defined(WB_HAS_SSE)
// FP arithmetics
#define v4f_add(a, b)  _mm_add_ps((a), (b))
#define v4f_sub(a, b)  _mm_sub_ps((a), (b))
#define v4f_mul(a, b)  _mm_mul_ps((a), (b))
#define v4f_div(a, b)  _mm_div_ps((a), (b))
#define v4f_max(a, b)  _mm_max_ps((a), (b))
#define v4f_min(a, b)  _mm_min_ps((a), (b))
#define v4f_sqrt(a, b) _mm_sqrt_ps((a), (b))

// Integer arithmetics
#define v4i_add(a, b) _mm_add_epi32(a, b)
#define v4i_sub(a, b) _mm_sub_epi32(a, b)

// Integer bitwise operation
#define v4i_and(a, b)     _mm_and_si128((a), (b))
#define v4i_or(a, b)      _mm_or_si128((a), (b))
#define v4i_sll(a, count) _mm_sll_epi32((a), (count))
#define v4i_srl(a, count) _mm_srl_epi32((a), (count))
#define v4i_slli(a, imm)  _mm_slli_epi32((a), (imm))
#define v4i_srli(a, imm)  _mm_srli_epi32((a), (imm))

// Mask operation
#define v4f_and_mask(x, mask)    _mm_and_ps((a), (mask))
#define v4f_or_mask(x, mask)     _mm_or_ps((a), (mask))
#define v4f_sel_mask(a, b, mask) _mm_or_ps(_mm_and_ps((a), (cond)), _mm_andnot_ps((cond), (b)))
#define v4i_and_mask(x, mask)    _mm_and_si128((a), (mask))
#define v4i_or_mask(x, mask)     _mm_or_si128((a), (mask))
#define v4i_sel_mask(a, b, mask) _mm_or_si128(_mm_and_si128((a), (cond)), _mm_andnot_si128((cond), (b)))

// Comparisons
#define v4f_ceq(a, b) _mm_cmpeq_ps(a, b)
#define v4f_clt(a, b) _mm_cmplt_ps(a, b)
#define v4f_cle(a, b) _mm_cmple_ps(a, b)
#define v4f_cgt(a, b) _mm_cmpgt_ps(a, b)
#define v4f_cge(a, b) _mm_cmpge_ps(a, b)
#endif
