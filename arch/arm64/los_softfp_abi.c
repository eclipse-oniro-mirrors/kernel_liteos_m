/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 *
 * Soft-float-ABI FP builtins for the AArch64 -mgeneral-regs-only build.
 *
 * The image is compiled with -mgeneral-regs-only (board config.gni), so a
 * DOUBLE expression lowers to compiler-rt builtins (__divdf3, ...) that
 * pass/return IEEE754 bit patterns in GENERAL registers.  The prebuilt
 * libclang_rt.builtins-aarch64, however, follows the hard-float AAPCS64
 * (arguments in d0/d1) - the registers never match, and every DOUBLE
 * operation silently computes garbage.
 *
 * Fix: provide these builtins here as pure integer code with the exact
 * general-register convention the callers emit; the object files precede
 * the toolchain runtime archive on the link line, so these definitions
 * win.  Scope: the builtins referenced by this image, plus __adddf3/
 * __subdf3 for completeness (any future double +/- would hit the same
 * mismatch).  fp128-only builtins pass everything in general registers
 * under BOTH ABIs and are not replaced.
 *
 * Conventions: a double is handled as its raw UINT64 IEEE754 bit pattern;
 * an fp128 is two UINT64s (lo bits[63:0], hi bits[127:64]); an fp128
 * result is returned as a two-UINT64 struct in (x0, x1), exactly like a
 * long double return value.
 */

#include "los_typedef.h"

#define DF_SIGN_SHIFT   63
#define DF_EXP_SHIFT    52
#define DF_FRAC_MASK    0x000FFFFFFFFFFFFFULL
#define DF_EXP_MASK     0x7FF0000000000000ULL
#define DF_INF_BITS     0x7FF0000000000000ULL
#define DF_QNAN_BITS    0x7FF8000000000000ULL
#define DF_BIAS         1023
#define DF_MANT_BITS    52          /* fraction bits; implicit 1 at bit 52 */
#define DF_MIN_EXP      (1 - DF_BIAS)
#define DF_MANT_IMPLICIT (1ULL << DF_MANT_BITS)

#define TF_EXP_SHIFT    48
#define TF_EXP_MASK     0x7FFF000000000000ULL
#define TF_FRAC_HI_MASK 0x0000FFFFFFFFFFFFULL
#define TF_BIAS         16383
#define TF_MANT_BITS    112

typedef struct {
    UINT64 lo;   /* bits [63:0] */
    UINT64 hi;   /* bits [127:64] */
} SoftFp128;

typedef struct {
    UINT32 sign;
    INT32 exp;       /* unbiased exponent field (1-bias for subnormals) */
    UINT64 frac;     /* fraction WITHOUT implicit bit */
    BOOL isNan;
    BOOL isInf;
    BOOL isSub;      /* exp field == 0 */
} SoftDecomp;

STATIC VOID SoftDfDecompose(UINT64 a, SoftDecomp *d)
{
    UINT32 biased;

    d->sign = (UINT32)(a >> DF_SIGN_SHIFT);
    d->isNan = FALSE;
    d->isInf = FALSE;
    d->isSub = FALSE;
    biased = (UINT32)((a & DF_EXP_MASK) >> DF_EXP_SHIFT);
    d->frac = a & DF_FRAC_MASK;
    if (biased == 0x7FFU) {
        if (d->frac != 0) {
            d->isNan = TRUE;
        } else {
            d->isInf = TRUE;
        }
        d->exp = 0;
        return;
    }
    if (biased == 0) {
        d->isSub = TRUE;
        d->exp = 1 - DF_BIAS;   /* value = frac * 2^(exp - 52) */
        return;
    }
    d->exp = (INT32)biased - DF_BIAS;
}

/* Compose from a normalized mantissa: `m` carries the implicit 1 at bit
 * DF_MANT_BITS and the fraction in bits [51:0]. */
STATIC UINT64 SoftDfCompose(UINT32 sign, INT32 exp, UINT64 m)
{
    return ((UINT64)sign << DF_SIGN_SHIFT) |
           ((UINT64)exp << DF_EXP_SHIFT) | (m & DF_FRAC_MASK);
}

/* Normalized 53-bit mantissa (implicit 1 at bit 52) + adjusted exponent:
 * value = m * 2^(exp - 52). Zero maps to m == 0. */
STATIC VOID SoftDfNormMant(const SoftDecomp *d, UINT64 *m, INT32 *exp)
{
    UINT64 f = d->frac;
    INT32 lead;

    *m = 0;
    *exp = d->exp;
    if (d->isSub) {
        if (f == 0) {
            return;                  /* true zero */
        }
        /* subnormal: shift the leading 1 up to bit 52 */
        lead = 63 - (INT32)__builtin_clzll(f);
        *m = f << (DF_MANT_BITS - lead);
        *exp = d->exp - (DF_MANT_BITS - lead);
    } else {
        *m = f | DF_MANT_IMPLICIT;   /* add the implicit leading 1 */
    }
}

/* Right-shift (hi:lo) by n, truncating, folding every shifted-out bit
 * into the result LSB (sticky). No rounding - for alignment only. */
STATIC VOID SoftShr128Sticky(UINT64 *hi, UINT64 *lo, UINT32 n)
{
    UINT64 lost;

    if (n == 0) {
        return;
    }
    if (n >= 128U) {
        *lo = ((*hi | *lo) != 0) ? 1 : 0;
        *hi = 0;
        return;
    }
    if (n >= 64U) {
        UINT32 k = n - 64U;
        lost = *lo | ((k != 0) ? (*hi & ((1ULL << k) - 1)) : 0);
        *lo = (k != 0) ? (*hi >> k) : *hi;
        *hi = 0;
    } else {
        lost = *lo & ((1ULL << n) - 1);
        *lo = (*lo >> n) | (*hi << (64 - n));
        *hi >>= n;
    }
    if (lost != 0) {
        *lo |= 1;
    }
}

/* Right-shift (hi:lo) by n with round-to-nearest-even (guard + sticky). */
STATIC VOID SoftShr128Rne(UINT64 *hi, UINT64 *lo, UINT32 n)
{
    UINT64 sticky;
    UINT64 guard;
    UINT64 nh;
    UINT64 nl;

    if (n == 0) {
        return;
    }
    if (n >= 128U) {
        *lo = ((*hi | *lo) != 0) ? 1 : 0;
        *hi = 0;
        return;
    }
    if (n >= 64U) {
        UINT32 k = n - 64U;
        if (k == 0) {
            /* shift by exactly 64: lo leaves, hi moves down; the guard
             * is lo's bit 63, sticky is everything below it */
            guard = (*lo >> 63) & 1ULL;
            sticky = *lo & 0x7FFFFFFFFFFFFFFFULL;
            nh = 0;
            nl = *hi;
        } else {
            /* guard: hi bit (k-1); sticky: all of lo plus hi bits below */
            guard = (*hi >> (k - 1)) & 1ULL;
            sticky = *lo | ((k >= 2) ? (*hi & ((1ULL << (k - 1)) - 1)) : 0);
            nh = 0;
            nl = *hi >> k;
        }
    } else {
        sticky = *lo & ((1ULL << (n - 1)) - 1);   /* bits below the guard */
        guard = (*lo >> (n - 1)) & 1ULL;
        nl = (*lo >> n) | (*hi << (64 - n));
        nh = *hi >> n;
    }
    if ((guard != 0) && ((sticky != 0) || ((nl & 1ULL) != 0))) {
        nl += 1;
        if (nl == 0) {
            nh += 1;
        }
    }
    *hi = nh;
    *lo = nl;
}

/* Left-shift (hi:lo) by n; bits shifted out of the top fold back as a
 * sticky LSB. */
STATIC VOID SoftShl128(UINT64 *hi, UINT64 *lo, UINT32 n)
{
    UINT64 sticky = 0;

    if (n == 0) {
        return;
    }
    if (n >= 128U) {
        *lo = ((*hi | *lo) != 0) ? 1 : 0;
        *hi = 0;
        return;
    }
    if (n >= 64U) {
        UINT32 k = n - 64U;
        if (k != 0) {
            sticky = *hi >> (64 - k);
        }
        if (*lo != 0) {
            sticky = 1;
        }
        *hi = *lo << k;
        *lo = 0;
    } else {
        sticky = *hi >> (64 - n);
        *hi = (*hi << n) | (*lo >> (64 - n));
        *lo <<= n;
    }
    if (sticky != 0) {
        *lo |= 1;
    }
}

/* Turn a raw 128-bit mantissa into the final double.
 * Input scale: value = (hi:lo) * 2^(exp - 52); the caller must have
 * folded sticky bits into the LSB if it pre-shifted. Handles:
 *   - normalize (leading 1 anywhere) to bit 52,
 *   - round-to-nearest-even at the final scale (single rounding),
 *   - subnormal results (shift at the FINAL scale, no double rounding),
 *   - overflow to infinity. */
STATIC UINT64 SoftFinish128(UINT32 sign, INT32 exp, UINT64 hi, UINT64 lo)
{
    INT32 lead;
    INT32 shift;
    UINT64 m;
    INT32 k;

    if ((hi | lo) == 0) {
        return (UINT64)sign << DF_SIGN_SHIFT;
    }
    if (hi != 0) {
        lead = 127 - (INT32)__builtin_clzll(hi);
    } else {
        lead = 63 - (INT32)__builtin_clzll(lo);
    }

    /* Subnormal-or-underflow: fraction = raw * 2^(exp - 52 + 1074)
     * = raw >> K, K = -(exp - 52 + 1074) = -exp - 1022. Result is
     * subnormal iff its leading bit lands strictly below bit 52. */
    k = -exp - 1022;
    if ((k > 0) && (lead - k < DF_MANT_BITS)) {
        if (k >= 129) {
            /* raw < 2^127 < 2^(k-1): strictly below half the smallest
             * subnormal - rounds to zero */
            return (UINT64)sign << DF_SIGN_SHIFT;
        }
        if (k == 128) {
            /* raw in [2^lead, 2^lead+1), lead <= 127: value = raw/2^128
             * in [0.5, 1) only when raw > 2^127; exactly 2^127 rounds
             * to even (zero) */
            BOOL above = (hi > (1ULL << 63)) || ((hi == (1ULL << 63)) && (lo != 0));
            if (above) {
                return ((UINT64)sign << DF_SIGN_SHIFT) | 1ULL;
            }
            return (UINT64)sign << DF_SIGN_SHIFT;
        }
        SoftShr128Rne(&hi, &lo, (UINT32)k);
        if (lo >= DF_MANT_IMPLICIT) {
            /* rounded up to the smallest normal */
            return SoftDfCompose(sign, 1, DF_MANT_IMPLICIT);
        }
        return ((UINT64)sign << DF_SIGN_SHIFT) | (lo & DF_FRAC_MASK);
    }

    /* Normal result: shift the leading 1 to bit 52 with RNE. Note the
     * result always ends up in LO: leading 1 at bit 52 < 64. */
    shift = lead - DF_MANT_BITS;
    if (shift > 0) {
        SoftShr128Rne(&hi, &lo, (UINT32)shift);
        exp += shift;
    } else if (shift < 0) {
        SoftShl128(&hi, &lo, (UINT32)(-shift));
        exp += shift;
    }
    m = lo;                            /* leading 1 at bit 52 (52 < 64) */
    if (m == (DF_MANT_IMPLICIT << 1)) {   /* carry: rounded up to 2^53 */
        m = DF_MANT_IMPLICIT;
        exp += 1;
    }
    if (exp >= (0x7FF - DF_BIAS)) {    /* unbiased >= 1024 -> infinity */
        return ((UINT64)sign << DF_SIGN_SHIFT) | DF_INF_BITS;
    }
    return SoftDfCompose(sign, exp + DF_BIAS, m);
}

/* TRUE for +/-0.0 (exp field 0 AND frac 0 - a finite power of two has
 * frac == 0 but a non-zero exp field and is NOT zero). */
STATIC BOOL SoftDfIsZero(const SoftDecomp *d)
{
    return (BOOL)(d->isSub && (d->frac == 0));
}

/* ----------------------------------------------------------------------------
 * u32/u64 -> double
 * ------------------------------------------------------------------------- */
UINT64 __floatunsidf(UINT32 a)
{
    if (a == 0) {
        return 0;
    }
    {
        INT32 e = 31 - (INT32)__builtin_clz(a);        /* 0..31 */
        UINT64 m = ((UINT64)a) << (DF_MANT_BITS - e);  /* implicit 1 at bit 52 */
        return SoftDfCompose(0, e + DF_BIAS, m);
    }
}

UINT64 __floatundidf(UINT64 a)
{
    if (a == 0) {
        return 0;
    }
    {
        INT32 lead = 63 - (INT32)__builtin_clzll(a);
        if (lead <= DF_MANT_BITS) {
            UINT64 m = a << (DF_MANT_BITS - lead);
            return SoftDfCompose(0, lead + DF_BIAS, m);
        }
        {
            /* value = a = (a >> (lead-52)) * 2^(lead-52); keep the raw
             * 64-bit mantissa in LO (SoftFinish128 128-bit convention) */
            UINT64 hi = 0;
            UINT64 lo = a;
            SoftShr128Rne(&hi, &lo, (UINT32)(lead - DF_MANT_BITS));
            return SoftFinish128(0, lead, hi, lo);
        }
    }
}

/* ----------------------------------------------------------------------------
 * double -> u64/i64
 * ------------------------------------------------------------------------- */
UINT64 __fixunsdfdi(UINT64 a)
{
    SoftDecomp d;
    UINT64 m;
    INT32 e;

    SoftDfDecompose(a, &d);
    if (d.isNan || (d.sign != 0)) {
        return 0;
    }
    if (d.isInf) {
        return ~0ULL;
    }
    SoftDfNormMant(&d, &m, &e);
    if (m == 0) {
        return 0;
    }
    if (e > 63) {
        return ~0ULL;
    }
    if (e < 0) {
        return 0;
    }
    if (e >= DF_MANT_BITS) {
        return m << (e - DF_MANT_BITS);
    }
    return m >> (DF_MANT_BITS - e);
}

INT64 __fixdfdi(UINT64 a)
{
    SoftDecomp d;
    UINT64 m;
    INT32 e;
    UINT64 mag;

    SoftDfDecompose(a, &d);
    if (d.isNan) {
        return 0;
    }
    if (d.isInf) {
        return (d.sign != 0) ? (INT64)0x8000000000000000ULL : (INT64)0x7FFFFFFFFFFFFFFFULL;
    }
    SoftDfNormMant(&d, &m, &e);
    if (m == 0) {
        return 0;
    }
    if (e > 63) {
        return (d.sign != 0) ? (INT64)0x8000000000000000ULL : (INT64)0x7FFFFFFFFFFFFFFFULL;
    }
    if (e < 0) {
        return 0;
    }
    if (e >= DF_MANT_BITS) {
        mag = m << (e - DF_MANT_BITS);
    } else {
        mag = m >> (DF_MANT_BITS - e);
    }
    if (d.sign != 0) {
        return -(INT64)mag;
    }
    return (INT64)mag;
}

/* ----------------------------------------------------------------------------
 * double compare family (AAPCS64 soft conventions)
 * ------------------------------------------------------------------------- */
/* returns -1/0/1, or 2 if unordered */
STATIC INT32 SoftDfCompare(UINT64 a, UINT64 b)
{
    SoftDecomp da;
    SoftDecomp db;
    UINT64 ma;
    UINT64 mb;
    INT32 ea;
    INT32 eb;
    INT32 mag;

    SoftDfDecompose(a, &da);
    SoftDfDecompose(b, &db);
    if (da.isNan || db.isNan) {
        return 2;
    }
    if (da.isInf || db.isInf) {
        if (da.isInf && db.isInf) {
            if (da.sign == db.sign) {
                return 0;
            }
            return (da.sign != 0) ? -1 : 1;
        }
        if (da.isInf) {
            return (da.sign != 0) ? -1 : 1;
        }
        return (db.sign != 0) ? 1 : -1;
    }
    SoftDfNormMant(&da, &ma, &ea);
    SoftDfNormMant(&db, &mb, &eb);
    if ((ma == 0) && (mb == 0)) {
        return 0;                      /* +/-0 == +/-0 */
    }
    if (da.sign != db.sign) {
        return (da.sign != 0) ? -1 : 1;
    }
    if (ma == 0) {
        mag = -1;
    } else if (mb == 0) {
        mag = 1;
    } else if (ea != eb) {
        mag = (ea > eb) ? 1 : -1;
    } else if (ma != mb) {
        mag = (ma > mb) ? 1 : -1;
    } else {
        mag = 0;
    }
    if (da.sign != 0) {
        mag = -mag;
    }
    return mag;
}

INT32 __eqdf2(UINT64 a, UINT64 b)
{
    return (SoftDfCompare(a, b) == 0) ? 0 : 1;
}

INT32 __nedf2(UINT64 a, UINT64 b)
{
    return __eqdf2(a, b);
}

INT32 __ltdf2(UINT64 a, UINT64 b)
{
    INT32 c = SoftDfCompare(a, b);
    if (c == 2) {
        return 1;                      /* NaN: not less */
    }
    return (c < 0) ? -1 : c;
}

INT32 __ledf2(UINT64 a, UINT64 b)
{
    INT32 c = SoftDfCompare(a, b);
    if (c == 2) {
        return 1;
    }
    return (c <= 0) ? c : 1;
}

INT32 __gtdf2(UINT64 a, UINT64 b)
{
    INT32 c = SoftDfCompare(a, b);
    if (c == 2) {
        return -1;                     /* NaN: not greater */
    }
    return (c > 0) ? 1 : c;
}

INT32 __gedf2(UINT64 a, UINT64 b)
{
    INT32 c = SoftDfCompare(a, b);
    if (c == 2) {
        return -1;
    }
    return (c >= 0) ? c : -1;
}

INT32 __cmpdf2(UINT64 a, UINT64 b)
{
    return __ledf2(a, b);
}

INT32 __unorddf2(UINT64 a, UINT64 b)
{
    SoftDecomp da;
    SoftDecomp db;

    SoftDfDecompose(a, &da);
    SoftDfDecompose(b, &db);
    return (da.isNan || db.isNan) ? 1 : 0;
}

/* ----------------------------------------------------------------------------
 * add / sub
 * ------------------------------------------------------------------------- */
STATIC UINT64 SoftDfAddSub(UINT64 a, UINT64 b, UINT32 flipB)
{
    SoftDecomp da;
    SoftDecomp db;
    UINT64 ma;
    UINT64 mb;
    INT32 ea;
    INT32 eb;
    INT32 e;
    UINT64 aHi;
    UINT64 aLo;
    UINT64 bHi;
    UINT64 bLo;
    UINT64 sHi;
    UINT64 sLo;
    UINT32 sign;

    SoftDfDecompose(a, &da);
    SoftDfDecompose(b, &db);
    if (flipB != 0) {
        db.sign ^= 1U;
    }
    if (da.isNan || db.isNan) {
        return DF_QNAN_BITS;
    }
    if (da.isInf || db.isInf) {
        if (da.isInf && db.isInf && (da.sign != db.sign)) {
            return DF_QNAN_BITS;       /* inf - inf */
        }
        if (da.isInf) {
            return a & (DF_INF_BITS | (1ULL << DF_SIGN_SHIFT));
        }
        /* db is Inf: db.sign was flipped above when flipB - the flipped
         * sign is exactly the result's sign. */
        return ((UINT64)db.sign << DF_SIGN_SHIFT) | DF_INF_BITS;
    }
    SoftDfNormMant(&da, &ma, &ea);
    SoftDfNormMant(&db, &mb, &eb);
    if ((ma == 0) && (mb == 0)) {
        /* both zero: -0 only when both effective signs are negative
         * (-0 + -0 = -0; +0 + -0 = +0) */
        return ((da.sign & db.sign) != 0) ? (1ULL << DF_SIGN_SHIFT) : 0;
    }
    /* Align at the larger exponent with 11 guard bits on top:
     * raw = m * 2^(exp - e + 11); value = raw * 2^(e - 52 - 11).
     * ma << 11 fits u64 exactly (m < 2^53). */
    e = (ea > eb) ? ea : eb;
    aHi = 0;
    aLo = ma << 11;
    bHi = 0;
    bLo = mb << 11;
    SoftShr128Sticky(&aHi, &aLo, (UINT32)(e - ea));
    SoftShr128Sticky(&bHi, &bLo, (UINT32)(e - eb));
    if (da.sign == db.sign) {
        UINT64 oldALo = aLo;
        sLo = aLo + bLo;
        sHi = aHi + bHi + ((sLo < oldALo) ? 1 : 0);
        sign = da.sign;
    } else {
        BOOL aBigger;
        if (aHi != bHi) {
            aBigger = (aHi > bHi);
        } else {
            aBigger = (aLo >= bLo);
        }
        if (aBigger) {
            UINT64 oldALo = aLo;
            sLo = aLo - bLo;
            sHi = aHi - bHi - ((sLo > oldALo) ? 1 : 0);
            sign = da.sign;
        } else {
            UINT64 oldBLo = bLo;
            sLo = bLo - aLo;
            sHi = bHi - aHi - ((sLo > oldBLo) ? 1 : 0);
            sign = db.sign;
        }
    }
    if ((sHi | sLo) == 0) {
        return 0;
    }
    return SoftFinish128(sign, e - 11, sHi, sLo);
}

UINT64 __adddf3(UINT64 a, UINT64 b)
{
    return SoftDfAddSub(a, b, 0);
}

UINT64 __subdf3(UINT64 a, UINT64 b)
{
    return SoftDfAddSub(a, b, 1);
}

/* ----------------------------------------------------------------------------
 * mul / div
 * ------------------------------------------------------------------------- */
UINT64 __muldf3(UINT64 a, UINT64 b)
{
    SoftDecomp da;
    SoftDecomp db;
    UINT64 ma;
    UINT64 mb;
    INT32 ea;
    INT32 eb;
    UINT32 sign;
    UINT64 aLo;
    UINT64 aHi;
    UINT64 bLo;
    UINT64 bHi;
    UINT64 p00;
    UINT64 p01;
    UINT64 p10;
    UINT64 p11;
    UINT64 mid;
    UINT64 mid2;
    UINT64 rLo;
    UINT64 rHi;

    SoftDfDecompose(a, &da);
    SoftDfDecompose(b, &db);
    if (da.isNan || db.isNan) {
        return DF_QNAN_BITS;
    }
    sign = da.sign ^ db.sign;
    if (da.isInf || db.isInf) {
        if (SoftDfIsZero(&da) || SoftDfIsZero(&db)) {
            return DF_QNAN_BITS;       /* inf * 0 */
        }
        return ((UINT64)sign << DF_SIGN_SHIFT) | DF_INF_BITS;
    }
    SoftDfNormMant(&da, &ma, &ea);
    SoftDfNormMant(&db, &mb, &eb);
    if ((ma == 0) || (mb == 0)) {
        return (UINT64)sign << DF_SIGN_SHIFT;
    }
    /* value = (ma * mb) * 2^((ea - 52) + (eb - 52)) = raw * 2^(ea+eb-104);
     * SoftFinish128 scale raw * 2^(exp - 52) -> exp = ea + eb - 52. */
    aLo = ma & 0xFFFFFFFFULL;
    aHi = ma >> 32;
    bLo = mb & 0xFFFFFFFFULL;
    bHi = mb >> 32;
    p00 = aLo * bLo;
    p01 = aLo * bHi;
    p10 = aHi * bLo;
    p11 = aHi * bHi;
    mid = p01 + (p00 >> 32);
    if (mid < p01) {
        p11 += 1ULL << 32;
    }
    mid2 = p10 + (mid & 0xFFFFFFFFULL);
    if (mid2 < p10) {
        p11 += 1ULL << 32;
    }
    rLo = (p00 & 0xFFFFFFFFULL) | (mid2 << 32);
    rHi = p11 + (mid >> 32) + (mid2 >> 32);
    return SoftFinish128(sign, ea + eb - 52, rHi, rLo);
}

UINT64 __divdf3(UINT64 a, UINT64 b)
{
    SoftDecomp da;
    SoftDecomp db;
    UINT64 ma;
    UINT64 mb;
    INT32 ea;
    INT32 eb;
    UINT32 sign;
    UINT64 q;
    UINT64 r;
    INT32 i;

    SoftDfDecompose(a, &da);
    SoftDfDecompose(b, &db);
    if (da.isNan || db.isNan) {
        return DF_QNAN_BITS;
    }
    sign = da.sign ^ db.sign;
    if (da.isInf) {
        if (db.isInf) {
            return DF_QNAN_BITS;
        }
        return ((UINT64)sign << DF_SIGN_SHIFT) | DF_INF_BITS;
    }
    if (db.isInf) {
        return (UINT64)sign << DF_SIGN_SHIFT;
    }
    SoftDfNormMant(&da, &ma, &ea);
    SoftDfNormMant(&db, &mb, &eb);
    if (SoftDfIsZero(&db)) {
        if (SoftDfIsZero(&da)) {
            return DF_QNAN_BITS;       /* 0/0 */
        }
        return ((UINT64)sign << DF_SIGN_SHIFT) | DF_INF_BITS;
    }
    if (SoftDfIsZero(&da)) {
        return (UINT64)sign << DF_SIGN_SHIFT;
    }
    /* restoring long division: q = floor((ma << 54) / mb).
     * ma, mb in [2^52, 2^53) so the first quotient bit is 0 or 1. */
    q = (ma >= mb) ? 1ULL : 0ULL;
    r = ma - q * mb;
    for (i = 0; i < 54; i++) {
        r <<= 1;
        q <<= 1;
        if (r >= mb) {
            r -= mb;
            q |= 1ULL;
        }
    }
    /* value = (q + r/(mb*2^54)) * 2^(ea - eb - 54). Park the mantissa one
     * bit higher (q << 1) so the sticky bit lands strictly below the
     * guard: raw = q*2 + (r != 0), value = raw * 2^(ea - eb - 55);
     * SoftFinish128 scale exp - 52 = ea - eb - 55 -> exp = ea - eb - 3. */
    {
        UINT64 lo = (q << 1) | ((r != 0) ? 1ULL : 0ULL);
        return SoftFinish128(sign, ea - eb - 3, 0, lo);
    }
}

/* ----------------------------------------------------------------------------
 * double <-> fp128
 * ------------------------------------------------------------------------- */
SoftFp128 __extenddftf2(UINT64 a)
{
    SoftFp128 out;
    SoftDecomp d;
    UINT64 m;
    INT32 e;

    SoftDfDecompose(a, &d);
    if (d.isNan) {
        out.lo = 0;
        out.hi = 0x7FFF800000000000ULL;
        return out;
    }
    if (d.isInf) {
        out.lo = 0;
        out.hi = ((UINT64)d.sign << 63) | 0x7FFF000000000000ULL;
        return out;
    }
    SoftDfNormMant(&d, &m, &e);
    if (m == 0) {
        out.lo = 0;
        out.hi = (UINT64)d.sign << 63;
        return out;
    }
    /* fp128 value = m * 2^(e - 52) with the 53 mantissa bits at the TOP
     * of the 112-bit fraction field: the double's 52 fraction bits land
     * at fp128 bits 111..60. */
    out.hi = ((UINT64)d.sign << 63) |
             ((UINT64)(e + TF_BIAS) << TF_EXP_SHIFT) |
             ((m & DF_FRAC_MASK) >> 4);
    out.lo = (m & DF_FRAC_MASK) << 60;
    return out;
}

UINT64 __trunctfdf2(UINT64 lo, UINT64 hi)
{
    UINT32 sign = (UINT32)(hi >> 63);
    UINT32 biased = (UINT32)((hi & TF_EXP_MASK) >> TF_EXP_SHIFT);
    UINT64 fracHi = hi & TF_FRAC_HI_MASK;
    UINT64 fracLo = lo;
    INT32 exp;
    INT32 lead;
    UINT64 mHi;
    UINT64 mLo;

    if (biased == 0x7FFFU) {
        if ((fracHi | fracLo) != 0) {
            return DF_QNAN_BITS;
        }
        return ((UINT64)sign << DF_SIGN_SHIFT) | DF_INF_BITS;
    }
    if ((biased == 0) && ((fracHi | fracLo) == 0)) {
        return (UINT64)sign << DF_SIGN_SHIFT;
    }
    if (biased == 0) {
        /* subnormal fp128: normalize. value = F * 2^(1 - TF_BIAS - 112);
         * with the leading 1 of F at bit `lead`, shifting F up so the
         * leading 1 hits bit 112 yields value = M * 2^(exp - 112) with
         * exp = lead + 1 - TF_BIAS - 112. */
        if (fracHi != 0) {
            lead = 111 - (63 - (INT32)__builtin_clzll(fracHi));
        } else {
            lead = 63 - (63 - (INT32)__builtin_clzll(fracLo));
        }
        exp = lead + 1 - TF_BIAS - TF_MANT_BITS;
        mHi = fracHi;
        mLo = fracLo;
        {
            INT32 sh = TF_MANT_BITS - lead;   /* >= 1 */
            SoftShl128(&mHi, &mLo, (UINT32)sh);
        }
    } else {
        /* normal fp128: implicit 1 at bit 112 -> hi bit 48 */
        exp = (INT32)biased - TF_BIAS;
        mHi = fracHi | (1ULL << 48);
        mLo = fracLo;
    }
    /* value = (mHi:mLo) * 2^(exp - 112); SoftFinish128 wants
     * raw * 2^(E - 52) -> E = exp - 112 + 52 = exp - 60. */
    return SoftFinish128(sign, exp - 60, mHi, mLo);
}
