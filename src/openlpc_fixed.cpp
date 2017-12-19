/*
  Fixed point OpenLPC codec
  Copyright (C) 2003-2005 Phil Frisbie, Jr. (phil@hawksoft.com)

  This is a major rewrite of the orginal floating point OpenLPC
  code from Future Dynamics. As such, a copywrite notice is not
  required to credit Future Dynamics.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
  Boston, MA  02111-1307, USA.

  Or go to http://www.gnu.org/copyleft/lgpl.html
*/
/************************************************************************\

  Low bitrate LPC CODEC derived from the public domain implementation
  of Ron Frederick.

  The basic design is preserved, except for several bug fixes and
  the following modifications:

  1. The pitch detector operates on the (downsampled) signal, not on
  the residue. This appears to yield better performances, and it
  lowers the processing load.
  2. Each frame is elongated by 50% prefixing it with the last half
  of the previous frame. This design, inherited from the original
  code for windowing purposes, is exploited in order to provide
  two independent pitch analyses: on the first 2/3, and on the
  second 2/3 of the elongated frame (of course, they overlap by
  half):

  last half old frame               new frame
  --------------------========================================
  <--------- first pitch region --------->
                      <--------- second pitch region  ------->

  Two voiced/unvoiced flags define the voicing status of each
  region; only one value for the period is retained (if both
  halves are voiced, the average is used).
  The two flags are used by the synthesizer for the halves of
  each frame to play back. Of course, this is non optimal but
  is good enough (a half-frame would be too short for measuring
  low pitches)
  3. The parameters (one float for the period (pitch), one for the
  gain, and ten for the LPC-10 filter) are quantized according
  this procedure:
  - the period is logarithmically compressed, then quantized
  as 8-bit unsigned int (6 would actually suffice)
  - the gain is logarithmically compressed (using a different
  formula), then quantized at 6-bit unsigned int. The two
  remaining bits are used for the voicing flags.
  - the first two LPC parameters (k[1] and k[2]) are multiplied
  by PI/2, and the arcsine of the result is quantized as
  6 and 5 bit signed integers. This has proved more effective
  than the log-area compression used by LPC-10.
  - the remaining eight LPC parameters (k[3]...k[10]) are
  quantized as, respectively, 5,4,4,3,3,3,3 and 2 bit signed
  integers.
  Finally, period and gain plus voicing flags are stored in the
  first two bytes of the 7-byte parameters block, and the quantized
  LPC parameters are packed into the remaining 5 bytes. Two bits
  remain unassigned, and can be used for error detection or other
  purposes.

  The frame lenght is actually variable, and is simply passed as
  initialization parameter to lpc_init(): this allows to experiment
  with various frame lengths. Long frames reduce the bitrate, but
  exceeding 320 samples (i.e. 40 ms, at 8000 samples/s) tend to
  deteriorate the speech, that sounds like spoken by a person
  affected by a stroke: the LPC parameters (modeling the vocal
  tract) can't change fast enough for a natural-sounding synthesis.
  25 ms per frame already yields a quite tight compression, corresponding
  to 1000/40 * 7 * 8 = 1400 bps. The quality improves little with
  frames shorter than 250 samples (32 frames/s), so this is a recommended
  compromise. The bitrate is 32 * 7 * 8 = 1792 bps.

  The synthesizer has been modified as well. For voiced subframes it
  now uses a sawtooth excitation, instead of the original pulse train.
  This idea, copied from MELP, reduces the buzzing-noise artifacts.
  In order to compensate the non-white spectrum of the sawtooth, a
  pre-emphasis is applied to the signal before the Durbin calculation.
  The filter has (in s-space) two zeroes at (640, 0) Hz and two poles
  at (3200, 0) Hz. These filters have been handoded, and may not be
  optimal. Two other filters (anti-hum high-pass with corner at 100 Hz,
  and pre-downsampling lowpass with corner at 300 Hz) are Butterworth
  designs produced by the MkFilter package by A.J. Fisher
  (http://www.cs.york.ac.uk/~fisher/mkfilter/).

\************************************************************************/

#ifdef _MSC_VER
#pragma warning (disable:4711) /* to disable automatic inline warning */
  #define M_PI (3.1415926535897932384626433832795)
#endif

#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include "openlpc.h"

#define fixed32         long

#if defined WIN32 || defined WIN64 || defined (_WIN32_WCE)
#define fixed64         __int64
#else
#define fixed64         long long
#endif

/* These are for development and debugging and should not be changed unless
you REALLY know what you are doing ;) */
#define IGNORE_OVERFLOW
#define FAST_FILTERS
#define PRECISION       20

#define ftofix32(x)       ((fixed32)((x) * (float)(1 << PRECISION) + ((x) < 0 ? -0.5 : 0.5)))
#define itofix32(x)       ((x) << PRECISION)
#define fixtoi32(x)       ((x) >> PRECISION)
#define fixtof32(x)       (float)((float)(x) / (float)(1 << PRECISION))

static fixed32 fixmul32(fixed32 x, fixed32 y)
{
    fixed64 temp;

    temp = x;
    temp *= y;
    temp >>= PRECISION;
#ifndef IGNORE_OVERFLOW
    if(temp > 0x7fffffff)
    {
        return 0x7fffffff;
    }
    else if(temp < -0x7ffffffe)
    {
        return -0x7ffffffe;
    }
#endif
    return (fixed32)temp;
}

static fixed32 fixdiv32(fixed32 x, fixed32 y)
{
    fixed64 temp;

    if(x == 0)
        return 0;
    if(y == 0)
        return 0x7fffffff;
    temp = x;
    temp <<= PRECISION;
    return (fixed32)(temp / y);
}

static fixed32 fixsqrt32(fixed32 x)
{

    unsigned long r = 0, s, v = (unsigned long)x;

#define STEP(k) s = r + (1 << k * 2); r >>= 1; \
    if (s <= v) { v -= s; r |= (1 << k * 2); }

    STEP(15);
    STEP(14);
    STEP(13);
    STEP(12);
    STEP(11);
    STEP(10);
    STEP(9);
    STEP(8);
    STEP(7);
    STEP(6);
    STEP(5);
    STEP(4);
    STEP(3);
    STEP(2);
    STEP(1);
    STEP(0);

    return (fixed32)(r << (PRECISION / 2));
}

__inline static fixed32 fixexp32(fixed32 x)
{
    fixed64 result = ftofix32(1.f);
    fixed64 temp;
    int     sign = 1;

    /* reduce range to 0.0 to 1.0 */
    if(x < 0)
    {
        x = (fixed32)-x;
        sign = -1;
    }
    while(x > itofix32(1))
    {
        x -= itofix32(1);
        result *= ftofix32(2.718282f);
        result >>= PRECISION;
    }
    /* reduce range to 0.0 to 0.5 */
    if(x > ftofix32(0.5f))
    {
        x -= ftofix32(0.5f);
        result *= ftofix32(1.648721f);
        result >>= PRECISION;
    }
    if(result > 0x7fffffff)
    {
        return 0x7fffffff;
    }
    temp = ftofix32(0.00138888f) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(0.00833333f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(0.04166666f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(0.16666666f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(0.5f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(1.0f)) * x;
    temp >>= PRECISION;
    result *= (temp + ftofix32(1.f));
    result >>= PRECISION;
    if(sign == -1)
    {
        temp = 1;
        result = (temp << (PRECISION * 2)) / result;
    }
    if(result > 0x7fffffff)
    {
        return 0x7fffffff;
    }
    return (fixed32)result;
}

__inline static fixed32 fixlog32(fixed32 x)
{
    fixed64 result = 0;
    fixed64 temp;

    if(x == 0)
    {
        return -0x7ffffffe;
    }
    else if(x < 0)
    {
        return 0;
    }
    while(x > itofix32(2))
    {
        result += ftofix32(0.693147f);
        x /= 2;
    }
    while(x < itofix32(1))
    {
        result -= ftofix32(0.693147f);
        x *= 2;
    }
    x -= itofix32(1);
    temp = ftofix32(-.0064535442f) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.0360884937f)) * x;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.0953293897f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.1676540711f)) * x;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.2407338084f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.3317990258f)) * x;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.4998741238f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.9999964239f)) * x;
    temp >>= PRECISION;
    result += temp;

    return (fixed32)result;
}

__inline fixed32 fixsin32(fixed32 x)
{
    fixed64 x2, temp;
    int     sign = 1;

    if(x < 0)
    {
        sign = -1;
        x = -x;
    }
    while(x > ftofix32(M_PI))
    {
        x -= ftofix32(M_PI);
        sign = -sign;
    }
    if(x > ftofix32(M_PI/2))
    {
        x = ftofix32(M_PI) - x;
    }
    x2 = (fixed64)x * x;
    x2 >>= PRECISION;
    if(sign != 1)
    {
        x = -x;
    }
    temp = ftofix32(-.0000000239f) * x2;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.0000027526f)) * x2;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.0001984090f)) * x2;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.0083333315f)) * x2;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.1666666664f)) * x2;
    temp >>= PRECISION;
    temp += itofix32(1);
    temp = temp * x;
    temp >>= PRECISION;

    return  (fixed32)(temp);
}

__inline fixed32 fixasin32(fixed32 x)
{
    fixed64 temp;
    int     sign = 1;

    if(x > itofix32(1) || x < itofix32(-1))
    {
        return 0;
    }
    if(x < 0)
    {
        sign = -1;
        x = -x;
    }
    temp = 0;
    temp = ftofix32(-.0012624911f) * (fixed64)x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.0066700901f)) * x;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.0170881256f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.0308918810f)) * x;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.0501743046f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(.0889789874f)) * x;
    temp >>= PRECISION;
    temp = (temp - ftofix32(.2145988016f)) * x;
    temp >>= PRECISION;
    temp = (temp + ftofix32(1.570796305f)) * fixsqrt32(itofix32(1) - x);
    temp >>= PRECISION;

    return sign * (ftofix32(M_PI/2) - (fixed32)temp);
}

#define PREEMPH

#define bcopy(a, b, n)    memmove(b, a, n)

#define LPC_FILTORDER   10
#define FS              8000.0  /* Sampling rate */
#define MAXWINDOW       1000    /* Max analysis window length */


typedef struct openlpc_e_state{
    int     framelen, buflen;
    fixed32 s[MAXWINDOW], y[MAXWINDOW], h[MAXWINDOW];
    fixed32 xv1[3], yv1[3],
            xv2[2], yv2[2],
            xv3[1], yv3[3],
            xv4[2], yv4[2];
    fixed32 w[MAXWINDOW], r[LPC_FILTORDER+1];
} openlpc_e_state_t;

typedef struct openlpc_d_state{
    fixed32 Oldper, OldG, Oldk[LPC_FILTORDER + 1];
    fixed32 bp[LPC_FILTORDER+1];
    fixed32 exc;
    fixed32 gainadj;
    int pitchctr, framelen, buflen;
} openlpc_d_state_t;

#define FC      200.0   /* Pitch analyzer filter cutoff */
#define DOWN        5   /* Decimation for pitch analyzer */
#define MINPIT      40.0    /* Minimum pitch (observed: 74) */
#define MAXPIT      320.0   /* Maximum pitch (observed: 250) */

#define MINPER      (int)(FS / (DOWN * MAXPIT) + .5)    /* Minimum period  */
#define MAXPER      (int)(FS / (DOWN * MINPIT) + .5)    /* Maximum period  */

#define REAL_MINPER  (DOWN * MINPER) /* converted to samples units */

#define WSCALE      1.5863  /* Energy loss due to windowing */

#define BITS_FOR_LPC 38

#define ARCSIN_Q /* provides better quantization of first two k[] at low bitrates */

#if BITS_FOR_LPC == 38
/* (38 bit LPC-10, 2.7 Kbit/s @ 20ms, 2.4 Kbit/s @ 22.5 ms */
static int parambits[LPC_FILTORDER] = {6,5,5,4,4,3,3,3,3,2};
#elif BITS_FOR_LPC == 32
/* (32 bit LPC-10, 2.4 Kbit/s, not so good */
static int parambits[LPC_FILTORDER] = {5,5,5,4,3,3,2,2,2,1};
#else /* BITS_FOR_LPC == 80 */
/* 80-bit LPC10, 4.8 Kbit/s */
static int parambits[LPC_FILTORDER] = {8,8,8,8,8,8,8,8,8,8};
#endif

static fixed32 logmaxminper;
static int sizeofparm;  /* computed by lpc_init */

static void auto_correl1(fixed32 *w, int n, fixed32 *r)
{
    int i, k;
    fixed64 temp, temp2;

    for (k=0; k <= MAXPER; k++, n--) {
        temp = 0;
        for (i=0; i < n; i++) {
            temp2 = w[i];
            temp += temp2 * w[i+k];
        }
        r[k] = (fixed32)(temp >> PRECISION);
    }
}

static void auto_correl2(fixed32 *w, int n, fixed32 *r)
{
    int i, k;
    fixed64 temp, temp2;

    for (k=0; k <= LPC_FILTORDER; k++, n--) {
        temp = 0;
        for (i=0; i < n; i++) {
            temp2 = w[i];
            temp += temp2 * w[i+k];
        }
        r[k] = (fixed32)(temp >> PRECISION);
    }
}

static void durbin(fixed32 r[], int p, fixed32 k[], fixed32 *g)
{
    int i, j;
    fixed32 a[LPC_FILTORDER+1], at[LPC_FILTORDER+1], e;

    for (i=0; i <= p; i++)
        a[i] = at[i] = 0;

    e = r[0];
    for (i=1; i <= p; i++) {
        k[i] = -r[i];
        for (j=1; j < i; j++) {
            at[j] = a[j];
            k[i] -= fixmul32(a[j], r[i-j]);
        }
        if (e == 0) {  /* fix by John Walker */
            *g = 0;
            return;
        }
        k[i] = fixdiv32(k[i], e);
        a[i] = k[i];
        for (j=1; j < i; j++)
            a[j] = at[j] + fixmul32(k[i], at[i-j]);
        e = fixmul32(e, (itofix32(1) - fixmul32(k[i], k[i])));
    }
    if (e < 0) {
        e = 0; /* fix by John Walker */
    }
    *g = fixsqrt32(e);
}

static void calc_pitch(fixed32 w[], int len, fixed32 *per)
{
    int i, j, rpos;
    fixed32 d[MAXWINDOW / DOWN], r[MAXPER + 1], rmax;
    fixed32 rval, rm, rp;
    fixed32 x, y;
    fixed32 vthresh;

    /* decimation */
    for (i=0, j=0; i < len; i+=DOWN)
        d[j++] = w[i];

    auto_correl1(d, len / DOWN, r);

    /* find peak between MINPER and MAXPER */
    x = itofix32(1);
    rpos = 0;
    rmax = 0;

    for (i = 1; i <= MAXPER; i++) {
        rm = r[i-1];
        rp = r[i+1];
        y = rm+r[i]+rp; /* find max of integral from i-1 to i+1 */
        if (y > rmax && r[i] > rm && r[i] > rp &&  i > MINPER) {
            rmax = y;
            rpos = i;
        }
    }

    /* consider adjacent values */
    rm = r[rpos-1];
    rp = r[rpos+1];

    if(rpos > 0) {
        x = fixdiv32(((rpos-1) * rm + rpos * r[rpos] + (rpos+1) * rp), (rm+r[rpos]+rp));
    }
    /* normalize, so that 0. < rval < 1. */
    rval = (r[0] == 0 ? 0 : fixdiv32(r[rpos], r[0]));


    /* periods near the low boundary and at low volumes
    are usually spurious and
    manifest themselves as annoying mosquito buzzes */

    *per = 0;   /* default: unvoiced */
    if ( x > itofix32(MINPER) &&  /* x could be < MINPER or even < 0 if rpos == MINPER */
        x < itofix32(MAXPER + 1) /* same story */
        ) {

        vthresh = ftofix32(0.6);
        if(r[0] > ftofix32(0.002))      /* at low volumes (< 0.002), prefer unvoiced */
            vthresh = ftofix32(0.25);    /* drop threshold at high volumes */

        if(rval > vthresh)
            *per = x * DOWN;
    }
}

/* Initialization of various parameters */
openlpc_encoder_state *create_openlpc_encoder_state(void)
{
    openlpc_encoder_state *state;

    state = (openlpc_encoder_state *)malloc(sizeof(openlpc_encoder_state));

    return state;

}


void init_openlpc_encoder_state(openlpc_encoder_state *st, int framelen)
{
    int i, j;

    st->framelen = framelen;
    memset(st->y, 0, sizeof(st->y));
    st->buflen = framelen * 3 / 2;
    /*  (st->buflen > MAXWINDOW) return -1;*/

    for(i=0, j=0; i<sizeof(parambits)/sizeof(parambits[0]); i++) {
        j += parambits[i];
    }
    sizeofparm = (j + 7) / 8 + 2;
    for (i = 0; i < st->buflen; i++) {
        st->s[i] = 0;
        /* this is only calculated once, but used each frame, */
        /* so we will use floating point for accuracy */
        st->h[i] = ftofix32(WSCALE*(0.54 - 0.46 * cos(2 * M_PI * i / (st->buflen-1.0))));
    }
    /* init the filters */
    st->xv1[0] = st->xv1[1] = st->xv1[2] = st->yv1[0] = st->yv1[1] = st->yv1[2] = 0;
    st->xv2[0] = st->xv2[1] = st->yv2[0] = st->yv2[1] = 0;
    st->xv3[0] = st->yv3[0] = st->yv3[1] = st->yv3[2] = 0;
    st->xv4[0] = st->xv4[1] = st->yv4[0] = st->yv4[1] = 0;

    logmaxminper = fixlog32(fixdiv32(itofix32(MAXPER), itofix32(MINPER)));
}

void destroy_openlpc_encoder_state(openlpc_encoder_state *st)
{
    if(st != NULL)
    {
        free(st);
        st = NULL;
    }
}

/* LPC Analysis (compression) */

int openlpc_encode(const short *buf, unsigned char *parm, openlpc_encoder_state *st)
{
    int i, j;
    fixed32 per, gain, k[LPC_FILTORDER+1];
    fixed32 per1, per2;
    fixed32 xv10, xv11, xv12, yv10, yv11, yv12, xv30, yv30, yv31, yv32;
#ifdef PREEMPH
    fixed32 xv20, xv21, yv20, yv21, xv40, xv41, yv40, yv41;
#endif

    xv10 = st->xv1[0];
    xv11 = st->xv1[1];
    xv12 = st->xv1[2];
    yv10 = st->yv1[0];
    yv11 = st->yv1[1];
    yv12 = st->yv1[2];
    xv30 = st->xv3[0];
    yv30 = st->yv3[0];
    yv31 = st->yv3[1];
    yv32 = st->yv3[2];
    /* convert short data in buf[] to signed lin. data in s[] and prefilter */
    for (i=0, j=st->buflen - st->framelen; i < st->framelen; i++, j++) {

        /* special handling here for the intitial conversion */
        fixed32 u = (fixed32)(buf[i] << (PRECISION - 15));

        /* Anti-hum 2nd order Butterworth high-pass, 100 Hz corner frequency */
        /* Digital filter designed by mkfilter/mkshape/gencode   A.J. Fisher
        mkfilter -Bu -Hp -o 2 -a 0.0125 -l -z */

        xv10 = xv11;
        xv11 = xv12;
#ifdef FAST_FILTERS
        xv12 = ((u * 15) >> 4) + (u >> 7) + ((u * 11) >> 14); /* /GAIN */
        yv10 = yv11;
        yv11 = yv12;
        yv12 = (fixed32)((xv10 + xv12) - (xv11 + xv11)
            - ((yv10 * 7) >> 3) - ((yv10 * 5) >> 8)
            + ((yv11 * 15) >> 3) + (yv11 >> 6) );
#else
        xv12 = fixmul32(u, ftofix32(0.94597831)); /* /GAIN */
        yv10 = yv11;
        yv11 = yv12;
        yv12 = (fixed32)((xv10 + xv12) - (xv11 + xv11)
            + fixmul32(ftofix32(-0.8948742499), yv10) + fixmul32(ftofix32(1.8890389823), yv11));
#endif
        u = st->s[j] = yv12; /* also affects input of next stage, to the LPC filter synth */

        /* low-pass filter s[] -> y[] before computing pitch */
        /* second-order Butterworth low-pass filter, corner at 300 Hz */
        /* Digital filter designed by mkfilter/mkshape/gencode   A.J. Fisher
        MKFILTER.EXE -Bu -Lp -o 2 -a 0.0375 -l -z */
#ifdef FAST_FILTERS
        xv30 = ((u * 3) >> 6) + (u >> 13); /* GAIN */
        yv30 = yv31;
        yv31 = yv32;
        yv32 = xv30 - ((yv30 * 23) >> 5) + (yv30 >> 9)
            + ((yv31 * 107) >> 6) - (yv31 >> 9);
#else
        xv30 = fixmul32(u, ftofix32(0.04699658)); /* GAIN */
        yv30 = yv31;
        yv31 = yv32;
        yv32 = xv30 + fixmul32(ftofix32(-0.7166152306), yv30) + fixmul32(ftofix32(1.6696186545), yv31);
#endif
        st->y[j] = yv32;
    }
    st->xv1[0] = xv10;
    st->xv1[1] = xv11;
    st->xv1[2] = xv12;
    st->yv1[0] = yv10;
    st->yv1[1] = yv11;
    st->yv1[2] = yv12;
    st->xv3[0] = xv30;
    st->yv3[0] = yv30;
    st->yv3[1] = yv31;
    st->yv3[2] = yv32;
#ifdef PREEMPH
    /* operate optional preemphasis s[] -> s[] on the newly arrived frame */
    xv20 = st->xv2[0];
    xv21 = st->xv2[1];
    yv20 = st->yv2[0];
    yv21 = st->yv2[1];
    xv40 = st->xv4[0];
    xv41 = st->xv4[1];
    yv40 = st->yv4[0];
    yv41 = st->yv4[1];
    for (j=st->buflen - st->framelen; j < st->buflen; j++) {
        fixed32 u = st->s[j];

        /* handcoded filter: 1 zero at 640 Hz, 1 pole at 3200 */
#define TAU (FS / 3200.f)
#define RHO (0.1f)
        xv20 = xv21;    /* e(n-1) */
#ifdef FAST_FILTERS
        xv21 = ((u * 3) >> 1) +((u * 43) >> 9);     /* e(n) , add 4 dB to compensate attenuation */
        yv20 = yv21;
        yv21 = ((yv20 * 11) >> 4) + ((yv20 * 7) >> 10)   /* u(n) */
            + ((xv21 * 23) >> 5) + ((xv21 * 7) >> 11)
            - ((xv20 * 11) >> 4) - ((xv20 * 7) >> 10);
#else
        xv21 = fixmul32(u, ftofix32(1.584));        /* e(n) , add 4 dB to compensate attenuation */
        yv20 = yv21;
        yv21 = fixmul32(ftofix32(TAU/(1.0f+RHO+TAU)), yv20)      /* u(n) */
            + fixmul32(ftofix32((RHO+TAU)/(1.0f+RHO+TAU)), xv21)
            - fixmul32(ftofix32(TAU/(1.0f+RHO+TAU)), xv20);
#endif
        u = yv21;

        /* cascaded copy of handcoded filter: 1 zero at 640 Hz, 1 pole at 3200 */
        xv40 = xv41;
#ifdef FAST_FILTERS
        xv41 = ((u * 3) >> 1) +((u * 43) >> 9);     /* e(n) , add 4 dB to compensate attenuation */
        yv40 = yv41;
        yv41 = ((yv40 * 11) >> 4) + ((yv40 * 7) >> 10)   /* u(n) */
            + ((xv41 * 23) >> 5) + ((xv41 * 7) >> 11)
            - ((xv40 * 11) >> 4) - ((xv40 * 7) >> 10);
#else
        xv41 = fixmul32(u, ftofix32(1.584));
        yv40 = yv41;
        yv41 = fixmul32(ftofix32(TAU/(1.0f+RHO+TAU)), yv40)
            + fixmul32(ftofix32((RHO+TAU)/(1.0f+RHO+TAU)), xv41)
            - fixmul32(ftofix32(TAU/(1.0f+RHO+TAU)), xv40);
#endif
        u = yv41;

        st->s[j] = u;
    }
    st->xv2[0] = xv20;
    st->xv2[1] = xv21;
    st->yv2[0] = yv20;
    st->yv2[1] = yv21;
    st->xv4[0] = xv40;
    st->xv4[1] = xv41;
    st->yv4[0] = yv40;
    st->yv4[1] = yv41;
#endif

    /* operate windowing s[] -> w[] */

    for (i=0; i < st->buflen; i++)
        st->w[i] = fixmul32(st->s[i], st->h[i]);

    /* compute LPC coeff. from autocorrelation (first 11 values) of windowed data */
    auto_correl2(st->w, st->buflen, st->r);
    durbin(st->r, LPC_FILTORDER, k, &gain);

    /* calculate pitch */
    calc_pitch(st->y, st->framelen, &per1);                 /* first 2/3 of buffer */
    calc_pitch(st->y + st->buflen - st->framelen, st->framelen, &per2); /* last 2/3 of buffer */
    if(per1 > 0 && per2 > 0)
        per = (per1+per2) / 2;
    else if(per1 > 0)
        per = per1;
    else if(per2 > 0)
        per = per2;
    else
        per = 0;

    /* logarithmic q.: 0 = MINPER, 256 = MAXPER */
    parm[0] = (unsigned char)(per == 0? 0 : (unsigned char)fixtoi32(fixdiv32(fixlog32(fixdiv32(per, itofix32(REAL_MINPER))), logmaxminper) * 256));

#ifdef LINEAR_G_Q
    i = fixtoi32(gain * 128);
    if(i > 255)
        i = 255;
#else
    i = fixtoi32(256 * fixlog32(itofix32(1) + fixmul32(ftofix32((2.718-1.f)/10.f), gain))); /* deriv = 5.82 allowing to reserve 2 bits */
    if(i > 255) i = 255;  /* reached when gain = 10 */
    i = (i+2) & 0xfc;
#endif

    parm[1] = (unsigned char)i;

    if(per1 > 0)
        parm[1] |= 1;
    if(per2 > 0)
        parm[1] |= 2;

    for(j=2; j < sizeofparm; j++)
        parm[j] = 0;

    for (i=0; i < LPC_FILTORDER; i++) {
        int bitamount = parambits[i];
        int bitc8 = 8-bitamount;
        int q = (1 << bitc8);  /* quantum: 1, 2, 4... */
        fixed32 u = k[i+1];
        int iu;

#ifdef ARCSIN_Q
        if(i < 2) u = fixmul32(fixasin32(u), ftofix32(2.f/M_PI));
#endif
        u *= 127;
        if(u < 0)
            u += ftofix32(0.6) * q;
        else
            u += ftofix32(0.4) * q; /* highly empirical! */

        iu = fixtoi32(u);
        iu = iu & 0xff; /* keep only 8 bits */

        /* make room at the left of parm array shifting left */
        for(j=sizeofparm-1; j >= 3; j--) {
            parm[j] = (unsigned char)((parm[j] << bitamount) | (parm[j-1] >> bitc8));
        }
        parm[2] = (unsigned char)((parm[2] << bitamount) | (iu >> bitc8)); /* parm[2] */
    }
    bcopy(st->s + st->framelen, st->s, (st->buflen - st->framelen)*sizeof(st->s[0]));
    bcopy(st->y + st->framelen, st->y, (st->buflen - st->framelen)*sizeof(st->y[0]));

    return sizeofparm;
}

openlpc_decoder_state *create_openlpc_decoder_state(void)
{
    openlpc_decoder_state *state;

    state = (openlpc_decoder_state *)malloc(sizeof(openlpc_decoder_state));

    return state;
}

void init_openlpc_decoder_state(openlpc_decoder_state *st, int framelen)
{
    int i, j;

    st->Oldper = 0;
    st->OldG = 0;
    for (i = 0; i <= LPC_FILTORDER; i++) {
        st->Oldk[i] = 0;
        st->bp[i] = 0;
    }
    st->pitchctr = 0;
    st->exc = 0;
    logmaxminper = fixlog32(fixdiv32(itofix32(MAXPER), itofix32(MINPER)));

    for(i=0, j=0; i<sizeof(parambits) / sizeof(parambits[0]); i++) {
        j += parambits[i];
    }
    sizeofparm = (j + 7) / 8 + 2;

    /* test for a valid frame len? */
    st->framelen = framelen;
    st->buflen = framelen * 3 / 2;
    st->gainadj = fixsqrt32(itofix32(3) / st->buflen);
}

#define MIDTAP 1
#define MAXTAP 4
static short y[MAXTAP+1]={-21161, -8478, 30892,-10216, 16950};
static int j=MIDTAP, k=MAXTAP;

__inline int random16 (void)
{
    int the_random;

    y[k] = (short)(y[k] + y[j]);

    the_random = y[k];
    k--;
    if (k < 0) k = MAXTAP;
    j--;
    if (j < 0) j = MAXTAP;

    return(the_random);
}

/* LPC Synthesis (decoding) */

int openlpc_decode(unsigned char *parm, short *buf, openlpc_decoder_state *st)
{
    int i, j, flen=st->framelen;
    fixed32 per, gain, k[LPC_FILTORDER+1];
    fixed32 u, NewG, Ginc, Newper, perinc;
    fixed32 Newk[LPC_FILTORDER+1], kinc[LPC_FILTORDER+1];
    fixed32 gainadj;
    int hframe;
    fixed32 hper[2];
    int ii;
    fixed32 bp0, bp1, bp2, bp3, bp4, bp5, bp6, bp7, bp8, bp9, bp10;
    fixed32 stgain;

    bp0 = st->bp[0];
    bp1 = st->bp[1];
    bp2 = st->bp[2];
    bp3 = st->bp[3];
    bp4 = st->bp[4];
    bp5 = st->bp[5];
    bp6 = st->bp[6];
    bp7 = st->bp[7];
    bp8 = st->bp[8];
    bp9 = st->bp[9];
    bp10 = st->bp[10];
    stgain = st->gainadj;

    per = itofix32(parm[0]);

    per = (fixed32)(per == 0? 0: REAL_MINPER * fixexp32(fixmul32(per/256, logmaxminper)));

    hper[0] = hper[1] = per;

    if((parm[1] & 0x1) == 0) hper[0] = 0;
    if((parm[1] & 0x2) == 0) hper[1] = 0;

#ifdef LINEAR_G_Q
    gain = itofix32(parm[1]) / 128;
#else
    gain = itofix32(parm[1]) / 256;
    gain = fixdiv32((fixexp32(gain) - itofix32(1)), ftofix32((2.718-1.f)/10));
#endif

    k[0] = 0;

    for (i=LPC_FILTORDER-1; i >= 0; i--) {
        int bitamount = parambits[i];
        int bitc8 = 8-bitamount;
        /* casting to char should set the sign properly */
        char c = (char)(parm[2] << bitc8);

        for(j=2; j<sizeofparm; j++)
            parm[j] = (unsigned char)((parm[j] >> bitamount) | (parm[j+1] << bitc8));

        k[i+1] = itofix32(c) / 128;
#ifdef ARCSIN_Q
        if(i<2) k[i+1] = fixsin32(fixmul32(ftofix32(M_PI/2), k[i+1]));
#endif
    }

    /* k[] are the same in the two subframes */
    for (i=1; i <= LPC_FILTORDER; i++) {
        Newk[i] = st->Oldk[i];
        kinc[i] = (k[i] - st->Oldk[i]) / flen;
    }

    /* Loop on two half frames */

    for(hframe=0, ii=0; hframe<2; hframe++) {

        Newper = st->Oldper;
        NewG = st->OldG;

        Ginc = (gain - st->OldG) / (flen / 2);
        per = hper[hframe];

        if (per == 0) {          /* if unvoiced */
            gainadj = stgain;
        } else {
            gainadj = fixsqrt32(per / st->buflen);
        }

        /* Interpolate period ONLY if both old and new subframes are voiced, gain and K always */

        if (st->Oldper != 0 && per != 0) {
            perinc = (per - st->Oldper) / (flen / 2);
        } else {
            perinc = 0;
            Newper = per;
        }

        if (Newper == 0) st->pitchctr = 0;

        for (i=0; i < flen / 2; i++, ii++) {
            fixed32 kj;

            if (Newper == 0) {
                u = fixmul32((random16() << (PRECISION - 15 - 1)), fixmul32(NewG, gainadj));
            } else {            /* voiced: send a delta every per samples */
                /* triangular excitation */
                if (st->pitchctr == 0) {
                    st->exc = fixmul32(NewG, gainadj >> 2);
                    st->pitchctr = fixtoi32(Newper);
                } else {
                    st->exc -= fixmul32(fixdiv32(NewG, Newper), gainadj >> 1);
                    st->pitchctr--;
                }
                u = st->exc;
            }
            /* excitation */
            kj = Newk[10];
            u -= fixmul32(kj, bp9);
            bp10 = bp9 + fixmul32(kj, u);

            kj = Newk[9];
            u -= fixmul32(kj, bp8);
            bp9 = bp8 + fixmul32(kj, u);

            kj = Newk[8];
            u -= fixmul32(kj, bp7);
            bp8 = bp7 + fixmul32(kj, u);

            kj = Newk[7];
            u -= fixmul32(kj, bp6);
            bp7 = bp6 + fixmul32(kj, u);

            kj = Newk[6];
            u -= fixmul32(kj, bp5);
            bp6 = bp5 + fixmul32(kj, u);

            kj = Newk[5];
            u -= fixmul32(kj, bp4);
            bp5 = bp4 + fixmul32(kj, u);

            kj = Newk[4];
            u -= fixmul32(kj, bp3);
            bp4 = bp3 + fixmul32(kj, u);

            kj = Newk[3];
            u -= fixmul32(kj, bp2);
            bp3 = bp2 + fixmul32(kj, u);

            kj = Newk[2];
            u -= fixmul32(kj, bp1);
            bp2 = bp1 + fixmul32(kj, u);

            kj = Newk[1];
            u -= fixmul32(kj, bp0);
            bp1 = bp0 + fixmul32(kj, u);

            bp0 = u;

            if (u  < ftofix32(-0.9999)) {
                u = ftofix32(-0.9999);
            } else if (u > ftofix32(0.9999)) {
                u = ftofix32(0.9999);
            }
            buf[ii] = (short)(u >> (PRECISION - 15));

            Newper += perinc;
            NewG += Ginc;
            for (j=1; j <= LPC_FILTORDER; j++) Newk[j] += kinc[j];

        }

        st->Oldper = per;
        st->OldG = gain;
    }
    st->bp[0] = bp0;
    st->bp[1] = bp1;
    st->bp[2] = bp2;
    st->bp[3] = bp3;
    st->bp[4] = bp4;
    st->bp[5] = bp5;
    st->bp[6] = bp6;
    st->bp[7] = bp7;
    st->bp[8] = bp8;
    st->bp[9] = bp9;
    st->bp[10] = bp10;

    for (j=1; j <= LPC_FILTORDER; j++) st->Oldk[j] = k[j];

    return flen;
}

void destroy_openlpc_decoder_state(openlpc_decoder_state *st)
{
    if(st != NULL)
    {
        free(st);
        st = NULL;
    }
}
