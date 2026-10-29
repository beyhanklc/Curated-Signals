/*  Peakwise - HISE Third-Party node (single file: node + resonance suppressor DSP engine)
 *
 *  Developed by Beyhan KILIC - https://www.beyhankilic.com
 *  Copyright (C) 2026 Beyhan KILIC
 *
 *  This program is free software: you can redistribute it and/or modify it under the terms of the
 *  GNU General Public License as published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 *  even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with this program (LICENSE).
 *  If not, see <https://www.gnu.org/licenses/>.
 */

// Parameters: 0 Threshold, 1 Amount, 2 Attack, 3 Release, 4 Workers, 5 LowLim, 6 HighLim, 7 Hear.
// External data: SliderPack 0 = reduction meter (log 50 Hz .. 16 kHz, 0..1 of Limits::meterRangeDb).
// Keep this file at the root of DspNetworks/ThirdParty: HISE takes the class name from the file name.
// No include directives: HISE's generated project includes JUCE / hi_dsp_library (incl. <cmath>, SSE intrinsics)
// before this header.

#pragma once

// FMA contraction on (the output depends on it).
#if defined(__clang__)
 #pragma clang fp contract(on)
#endif

namespace pkws
{

// ==================================================================================================
// Utilities
// ==================================================================================================

// flush-to-zero for the scope of a process call (restores the caller's FP mode)
class ScopedDenormals
{
public:
    ScopedDenormals() noexcept
    {
      #if defined(__aarch64__) || defined(_M_ARM64)
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (mSaved) :: "memory");
        const unsigned long long fz = mSaved | (1ull << 24);
        __asm__ __volatile__ ("msr fpcr, %0" :: "r" (fz) : "memory");
      #elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        mSaved = (unsigned long long) _mm_getcsr();
        _mm_setcsr ((unsigned int) mSaved | 0x8040u);
      #else
        mSaved = 0;
      #endif
    }

    ~ScopedDenormals() noexcept
    {
      #if defined(__aarch64__) || defined(_M_ARM64)
        __asm__ __volatile__ ("msr fpcr, %0" :: "r" (mSaved) : "memory");
      #elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        _mm_setcsr ((unsigned int) mSaved);
      #endif
    }

    ScopedDenormals (const ScopedDenormals&) = delete;
    ScopedDenormals& operator= (const ScopedDenormals&) = delete;

private:
    unsigned long long mSaved = 0;
};

template <typename T>
struct Constants
{
    static constexpr T Pi = (T) 3.14159265358979323846;
};

namespace detail
{

template <typename T>
inline T clamp (T v, T lo, T hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

template <typename T>
inline T finiteOr (T v, T fallback) noexcept
{
    return std::isfinite (v) ? v : fallback;
}

template <typename T>
inline T flushDenormal (T v) noexcept
{
    static constexpr T Threshold = (T) 1.0e-20;
    return (v > -Threshold && v < Threshold) ? (T) 0 : v;
}

// bilinear prewarp, cutoff kept below 0.49 fs
template <typename T>
inline T prewarp (T cutoffHz, T sampleRate) noexcept
{
    const T nyquistLimit = sampleRate * (T) 0.49;
    const T fc = clamp (cutoffHz, (T) 1.0e-4, nyquistLimit);
    return std::tan (Constants<T>::Pi * fc / sampleRate);
}

}

// ==================================================================================================
// Half-band decimator (polyphase allpass, 6 coefficients, -63.5 dB stopband)
// ==================================================================================================

template <typename T>
struct AllpassSection
{
    T a = (T) 0;
    T x1 = (T) 0, y1 = (T) 0;

    void reset() noexcept { x1 = (T) 0; y1 = (T) 0; }

    inline T process (T x) noexcept
    {
        const T y = a * (x - y1) + x1;
        x1 = x;
        y1 = detail::flushDenormal (detail::finiteOr (y, (T) 0));
        return y1;
    }
};

template <typename T>
class HalfBandDecimator
{
public:
    static constexpr int kHalf = 3;

    void prepare() noexcept
    {
        static const double c[6] = { 0.084711466192, 0.286406560000, 0.510677166660,
                                     0.699797744566, 0.841782452031, 0.950273141129 };
        for (int i = 0; i < kHalf; ++i)
        {
            mDown0[i].a = (T) c[2 * i];
            mDown1[i].a = (T) c[2 * i + 1];
        }
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kHalf; ++i)
        {
            mDown0[i].reset(); mDown1[i].reset();
        }
    }

    // two input samples -> one output sample
    inline T down (T a, T b) noexcept
    {
        T p = b;
        for (int i = 0; i < kHalf; ++i) p = mDown0[i].process (p);
        T q = a;
        for (int i = 0; i < kHalf; ++i) q = mDown1[i].process (q);
        return (T) 0.5 * (p + q);
    }

private:
    AllpassSection<T> mDown0[kHalf], mDown1[kHalf];
};

// ==================================================================================================
// Real FFT (radix-2, N real samples as an N/2 complex transform)
// ==================================================================================================

template <typename T, int N>
class RealFFT
{
public:
    static_assert (N >= 8, "transform too short");
    static_assert ((N & (N - 1)) == 0, "radix-2: N must be a power of two");

    static constexpr int Half    = N / 2;
    static constexpr int NumBins = N / 2 + 1;

    void prepare() noexcept
    {
        for (int i = 0; i < Half; ++i)
        {
            int r = 0;
            for (int b = 1, s = Half >> 1; b < Half; b <<= 1, s >>= 1)
                if (i & b) r |= s;
            mRev[i] = r;
        }

        int idx = 0;
        for (int len = 2; len <= Half; len <<= 1)
        {
            const double step = -2.0 * Constants<double>::Pi / (double) len;
            for (int j = 0; j < len / 2; ++j)
            {
                mTwRe[idx] = (T) std::cos (step * (double) j);
                mTwIm[idx] = (T) std::sin (step * (double) j);
                ++idx;
            }
        }

        for (int k = 0; k <= Half / 2; ++k)
        {
            const double a = -2.0 * Constants<double>::Pi * (double) k / (double) N;
            mUnRe[k] = (T) std::cos (a);
            mUnIm[k] = (T) std::sin (a);
        }
    }

    void forward (const T* x, T* re, T* im) noexcept
    {
        for (int i = 0; i < Half; ++i)
        {
            const int r = mRev[i];
            mRe[r] = x[2 * i];
            mIm[r] = x[2 * i + 1];
        }

        int idx = 0;
        for (int len = 2; len <= Half; len <<= 1)
        {
            const int half = len >> 1;
            for (int i = 0; i < Half; i += len)
            {
                int t = idx;
                for (int j = 0; j < half; ++j, ++t)
                {
                    const T wr = mTwRe[t], wi = mTwIm[t];
                    const int a = i + j, b = a + half;
                    const T xr = mRe[b] * wr - mIm[b] * wi;
                    const T xi = mRe[b] * wi + mIm[b] * wr;
                    mRe[b] = mRe[a] - xr;  mIm[b] = mIm[a] - xi;
                    mRe[a] = mRe[a] + xr;  mIm[a] = mIm[a] + xi;
                }
            }
            idx += half;
        }

        // unpack the half-length complex transform into the real spectrum
        re[0]    = mRe[0] + mIm[0];   im[0]    = (T) 0;
        re[Half] = mRe[0] - mIm[0];   im[Half] = (T) 0;

        for (int k = 1; k <= Half / 2; ++k)
        {
            const int m = Half - k;

            const T zr = mRe[k], zi = mIm[k];
            const T mr = mRe[m], mi = mIm[m];

            const T er = (T) 0.5 * (zr + mr);
            const T ei = (T) 0.5 * (zi - mi);

            const T o_r = (T) 0.5 * (zi + mi);
            const T o_i = (T) -0.5 * (zr - mr);

            const T wr = mUnRe[k], wi = mUnIm[k];
            const T tr = o_r * wr - o_i * wi;
            const T ti = o_r * wi + o_i * wr;

            re[k] = er + tr;   im[k] = ei + ti;

            re[m] = er - tr;   im[m] = -(ei - ti);
        }
    }

    void powerSpectrum (const T* x, T* power) noexcept
    {
        forward (x, mOutRe, mOutIm);
        for (int k = 0; k < NumBins; ++k)
            power[k] = mOutRe[k] * mOutRe[k] + mOutIm[k] * mOutIm[k];
    }

private:
    int mRev[Half] = { 0 };
    T   mTwRe[Half] = { (T) 0 }, mTwIm[Half] = { (T) 0 };
    T   mUnRe[Half / 2 + 1] = { (T) 0 }, mUnIm[Half / 2 + 1] = { (T) 0 };
    T   mRe[Half] = { (T) 0 }, mIm[Half] = { (T) 0 };
    T   mOutRe[NumBins] = { (T) 0 }, mOutIm[NumBins] = { (T) 0 };
};

// ==================================================================================================
// Bell filter bank: W parallel SVF cuts, output = x + sum(mix_i * band_i), mix ramped over 64 samples
// ==================================================================================================

template <typename T, int W>
class BellBank
{
public:
    static_assert (W >= 1, "empty bank");

    static constexpr int RampSamples = 64;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? (T) sampleRate : (T) 48000;
        for (int i = 0; i < W; ++i)
        {
            a1[i] = (T) 1; a2[i] = (T) 0; a3[i] = (T) 0;
            mix[i] = (T) 0; target[i] = (T) 0; step[i] = (T) 0;
        }
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < W; ++i)
        {
            s1[i] = (T) 0; s2[i] = (T) 0;
            mix[i] = (T) 0; step[i] = (T) 0;
        }
    }

    // amplitude = linear gain at the centre^(1/2) (Cytomic bell: A = 10^(dB / 40))
    void setWorker (int i, T freqHz, T q, T amplitude) noexcept
    {
        const T g  = detail::prewarp (freqHz, fs);
        const T qq = q < (T) 0.05 ? (T) 0.05 : q;
        const T a  = amplitude < (T) 1.0e-4 ? (T) 1.0e-4 : amplitude;
        const T k  = (T) 1 / (qq * a);

        a1[i] = (T) 1 / ((T) 1 + g * (g + k));
        a2[i] = g * a1[i];
        a3[i] = g * a2[i];

        const T t = k * (a * a - (T) 1);
        if (t != target[i])
        {
            target[i] = t;
            step[i] = (t - mix[i]) / (T) RampSamples;
        }
    }

    void releaseWorker (int i) noexcept
    {
        if (target[i] != (T) 0)
        {
            target[i] = (T) 0;
            step[i] = -mix[i] / (T) RampSamples;
        }
    }

    void clearWorker (int i) noexcept { s1[i] = (T) 0; s2[i] = (T) 0; }

    void processBlock (T* x, int n) noexcept
    {
        if (n <= 0)
            return;

        int nActive = 0;
        for (int i = 0; i < W; ++i)
            if (! (target[i] == (T) 0 && mix[i] == (T) 0))
                nActive = i + 1;

        if (nActive == 0)
            return;

        for (int s = 0; s < n; ++s)
        {
            const T in = x[s];
            T acc = (T) 0;

            for (int i = 0; i < nActive; ++i)
            {
                const T v3 = in - s2[i];
                const T v1 = a1[i] * s1[i] + a2[i] * v3;
                const T v2 = s2[i] + a2[i] * s1[i] + a3[i] * v3;
                s1[i] = (T) 2 * v1 - s1[i];
                s2[i] = (T) 2 * v2 - s2[i];

                if (mix[i] != target[i])
                {
                    mix[i] += step[i];
                    if ((step[i] > (T) 0 && mix[i] > target[i])
                        || (step[i] < (T) 0 && mix[i] < target[i]))
                        mix[i] = target[i];
                }
                acc += mix[i] * v1;
            }

            x[s] = in + acc;
        }

        // reset a filter whose state became non-finite
        for (int i = 0; i < nActive; ++i)
        {
            if (! (s1[i] == s1[i] && s2[i] == s2[i]
                   && s1[i] - s1[i] == (T) 0 && s2[i] - s2[i] == (T) 0))
            {
                s1[i] = (T) 0; s2[i] = (T) 0;
            }
        }
    }

private:
    T fs = (T) 48000;
    T a1[W] = { (T) 1 }, a2[W] = { (T) 0 }, a3[W] = { (T) 0 };
    T mix[W] = { (T) 0 }, target[W] = { (T) 0 };
    T s1[W] = { (T) 0 }, s2[W] = { (T) 0 };
    T step[W] = { (T) 0 };
};

// ==================================================================================================
// Spectral analyser: decimates to <= 64 kHz, Hann-windowed FFT every N / HopDiv samples, band powers on a
// log grid (bandsPerOct, lowHz .. highHz)
// ==================================================================================================

template <typename T, int N, int HopDiv, int MaxBands>
class Analyser
{
public:
    static constexpr int Hop     = N / HopDiv;
    static constexpr int NumBins = N / 2 + 1;

    void prepare (double hostRate, double bandsPerOct, double lowHz, double highHz) noexcept
    {
        const double rate = hostRate > 0.0 ? hostRate : 48000.0;

        mDecim = 1;
        double r = rate;
        while (r > 64000.0 && mDecim < 4) { r *= 0.5; mDecim *= 2; }
        mAnalysisRate = r;

        mHalf[0].prepare();
        mHalf[1].prepare();

        mFft.prepare();

        for (int i = 0; i < N; ++i)
            mWindow[i] = (T) (0.5 - 0.5 * std::cos (2.0 * Constants<double>::Pi
                                                    * (double) i / (double) N));

        double wsum = 0.0;
        for (int i = 0; i < N; ++i) wsum += (double) mWindow[i];
        const T scale = (T) (2.0 / wsum);
        for (int i = 0; i < N; ++i) mWindow[i] *= scale;

        buildGrid (bandsPerOct, lowHz, highHz);
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < N; ++i) mRing[i] = (T) 0;
        mWrite = 0;
        mSinceHop = 0;
        mPhase = 0;
        mPending = (T) 0;
        mPending2 = (T) 0;
        mPhase2 = 0;
        mHalf[0].reset();
        mHalf[1].reset();
        mPrimed = 0;
    }

    int    numBands() const noexcept { return mNumBands; }
    double analysisRate() const noexcept { return mAnalysisRate; }
    T      bandCentre (int b) const noexcept { return mCentre[b]; }
    T      bandWidthOct (int b) const noexcept { return mWidthOct[b]; }

    // calls onFrame (bandPower, numBands) once per hop after the first full frame
    template <typename Fn>
    void pushBlock (const T* mono, int n, Fn&& onFrame) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            T s = mono[i];

            if (mDecim >= 2)
            {
                if (mPhase == 0) { mPending = s; mPhase = 1; continue; }
                s = mHalf[0].down (mPending, s);
                mPhase = 0;

                if (mDecim >= 4)
                {
                    if (mPhase2 == 0) { mPending2 = s; mPhase2 = 1; continue; }
                    s = mHalf[1].down (mPending2, s);
                    mPhase2 = 0;
                }
            }

            mRing[mWrite] = s;
            mWrite = (mWrite + 1) & (N - 1);
            if (mPrimed < N) ++mPrimed;

            if (++mSinceHop >= Hop)
            {
                mSinceHop = 0;
                if (mPrimed >= N)
                {
                    analyseFrame();
                    onFrame (mBandPower, mNumBands);
                }
            }
        }
    }

private:
    void buildGrid (double bandsPerOct, double lowHz, double highHz) noexcept
    {
        const double binHz = mAnalysisRate / (double) N;
        const double ratio = std::pow (2.0, 1.0 / (bandsPerOct > 0.5 ? bandsPerOct : 0.5));
        const double top   = highHz < mAnalysisRate * 0.45 ? highHz : mAnalysisRate * 0.45;

        mNumBands = 0;
        double edge = lowHz > binHz ? lowHz : binHz;

        while (mNumBands < MaxBands)
        {
            double next = edge * ratio;
            if (next < edge + binHz) next = edge + binHz;
            if (next > top) break;

            int lo = (int) std::ceil (edge / binHz);
            int hi = (int) std::floor (next / binHz);
            if (lo < 1) lo = 1;
            if (hi > NumBins - 1) hi = NumBins - 1;
            if (hi < lo) hi = lo;

            mLo[mNumBands] = lo;
            mHi[mNumBands] = hi;
            mCentre[mNumBands]   = (T) std::sqrt (edge * next);
            mWidthOct[mNumBands] = (T) (std::log2 (next / edge));
            ++mNumBands;

            edge = next;
        }
    }

    void analyseFrame() noexcept
    {
        const int start = mWrite;
        for (int i = 0; i < N; ++i)
            mFrame[i] = mRing[(start + i) & (N - 1)] * mWindow[i];

        mFft.powerSpectrum (mFrame, mBinPower);

        for (int b = 0; b < mNumBands; ++b)
        {
            T sum = (T) 0;
            const int hi = mHi[b];
            for (int k = mLo[b]; k <= hi; ++k)
                sum += mBinPower[k];
            mBandPower[b] = sum;
        }
    }

    RealFFT<T, N>         mFft;
    HalfBandDecimator<T>  mHalf[2];

    double mAnalysisRate = 48000.0;
    int    mDecim        = 1;

    T   mWindow[N]   = { (T) 0 };
    T   mRing[N]     = { (T) 0 };
    T   mFrame[N]    = { (T) 0 };
    T   mBinPower[NumBins] = { (T) 0 };

    int mWrite = 0, mSinceHop = 0, mPrimed = 0;
    int mPhase = 0, mPhase2 = 0;
    T   mPending = (T) 0, mPending2 = (T) 0;

    int mNumBands = 0;
    int mLo[MaxBands] = { 0 }, mHi[MaxBands] = { 0 };
    T   mCentre[MaxBands] = { (T) 0 }, mWidthOct[MaxBands] = { (T) 0 };
    T   mBandPower[MaxBands] = { (T) 0 };
};

// ==================================================================================================
// Resonance detector: per-band level vs. a smoothed spectral envelope ("excess"), discounted for tonal,
// harmonic and comb-like (musical) content, then ranked notch candidates inside the user window
// ==================================================================================================

template <typename T>
struct Tuning
{
    T envelopeOct      = (T) 0.85;      // envelope smoothing half-width
    T envelopeClipDb   = (T) 6;

    T edgeMarginOctLow  = (T) 0.85;     // no candidates this close to the analysis grid edges
    T edgeMarginOctHigh = (T) 0.11;

    T riseMs           = (T) 380;       // band average persistence
    T fallMs           = (T) 400;
    T floorBelowFrameDb= (T) 80;
    T silenceRelax     = (T) 0.25;

    T toneFloorDb      = (T) 18;        // strong isolated peaks are treated as tones
    T toneCeilDb       = (T) 28;
    T toneShadowDbOct  = (T) 45;

    T harmonicFullFrac = (T) 0.60;
    T harmonicNoneFrac = (T) 0.25;
    T harmonicMinDb    = (T) 3;
    T harmonicStrength = (T) 0.7;

    T combNeighbourOct = (T) 0.7;
    T combFrac         = (T) 0.45;
    T combMinDb        = (T) 3;
    T combSpacingTol   = (T) 0.15;
    T combMinPartials  = (T) 3;
    T combStrength     = (T) 0.85;

    T maxWidthOct      = (T) 0.75;      // wider bumps are not resonances
    T minQ             = (T) 1.6;
    T maxQ             = (T) 22;
    T qScale           = (T) 0.7;
    T minSeparationOct = (T) 0.33;

    T weightLowHz      = (T) 160;       // audibility weighting corners
    T weightHighHz     = (T) 11000;

    T candEdgeMarginOct = (T) 0.0833333;  // candidates may sit this far outside the user window

    T minCandidateHz   = (T) 60;
    T maxCandidateHz   = (T) 17000;

    T silenceDb        = (T) -75;
    T transientDb      = (T) 3;
};

template <typename T>
struct Candidate
{
    T frequency = (T) 0;
    T q         = (T) 4;
    T severity  = (T) 0;
    T evidence  = (T) 0;
};

template <typename T, int MaxBands, int MaxCand>
class Detector
{
public:
    const Tuning<T>& tuning() const noexcept { return mT; }

    void prepare (const T* centres, const T* widthsOct, int numBands,
                  double frameRate) noexcept
    {
        mNumBands  = numBands < MaxBands ? numBands : MaxBands;
        mFrameRate = frameRate > 1.0 ? frameRate : 1.0;

        for (int b = 0; b < mNumBands; ++b)
        {
            mCentre[b] = centres[b];
            mWidth[b]  = widthsOct[b];
            mWeight[b] = audibility (centres[b]);
        }

        T octPerBand = (T) 0.04;
        if (mNumBands > 4)
        {
            const T span = (T) std::log2 ((double) mCentre[mNumBands - 1]
                                        / (double) mCentre[0]);
            octPerBand = span / (T) (mNumBands - 1);
        }
        mHalfWin = (int) ((double) mT.envelopeOct / (double) octPerBand + 0.5);
        if (mHalfWin < 1) mHalfWin = 1;
        if (mHalfWin > mNumBands / 2) mHalfWin = mNumBands / 2;

        mRise = alphaFor (mT.riseMs);
        mFall = alphaFor (mT.fallMs);

        mShadowStep = (T) ((double) mT.toneShadowDbOct * (double) octPerBand);

        reset();
    }

    void reset() noexcept
    {
        for (int b = 0; b < MaxBands; ++b)
        {
            mLevel[b] = (T) -160;
            mAvg[b]   = (T) -160;
            mEnv[b]   = (T) -160;
            mExcess[b]= (T) 0;
            mNear[b]  = (T) 0;
            mEvidence[b] = (T) 0;
        }
        mNumCandidates = 0;
        mFrameLevelDb  = (T) -160;
        mPrevFrameDb   = (T) -160;
        mTransient     = false;
    }

    void setCandidateRange (T loHz, T hiHz) noexcept
    {
        mUserLoHz = loHz;
        mUserHiHz = hiHz;
    }

    void processFrame (const T* bandPower, int numBands, T thresholdDb) noexcept
    {
        const int B = numBands < mNumBands ? numBands : mNumBands;
        if (B < 8)
            return;

        T total = (T) 0;
        for (int b = 0; b < B; ++b)
        {
            total += bandPower[b];
            mLevel[b] = (T) (10.0 * std::log10 ((double) bandPower[b] + 1.0e-30));
        }
        mFrameLevelDb = (T) (10.0 * std::log10 ((double) total + 1.0e-30));

        const T rise = mFrameLevelDb - mPrevFrameDb;
        mPrevFrameDb = mFrameLevelDb;
        mTransient = (rise > mT.transientDb);

        const bool silent = (mFrameLevelDb < mT.silenceDb);

        // learn the band averages (frozen on transients, relaxed to the envelope in silence)
        if (! silent && ! mTransient)
        {
            const T floorDb = mFrameLevelDb - mT.floorBelowFrameDb;
            for (int b = 0; b < B; ++b)
            {
                const T l = mLevel[b] > floorDb ? mLevel[b] : floorDb;
                const T a = l > mAvg[b] ? mRise : mFall;
                mAvg[b] += (l - mAvg[b]) * a;
            }
        }
        else if (silent)
        {
            const T a = mFall * mT.silenceRelax;
            for (int b = 0; b < B; ++b)
                mAvg[b] += (mEnv[b] - mAvg[b]) * a;
        }

        // envelope: smooth, clip peaks to envelope + envelopeClipDb, smooth again
        smoothInto (mAvg, mEnv, B, mHalfWin);
        for (int b = 0; b < B; ++b)
        {
            const T cap = mEnv[b] + mT.envelopeClipDb;
            mClipped[b] = mAvg[b] < cap ? mAvg[b] : cap;
        }
        smoothInto (mClipped, mEnv, B, mHalfWin);

        for (int b = 0; b < B; ++b)
        {
            const T x = mAvg[b] - mEnv[b];
            mExcess[b] = x > (T) 0 ? x : (T) 0;
        }
        spreadMax (mExcess, mNear, B);

        const T floorDb = thresholdDb * (T) 0.5;
        for (int b = 0; b < B; ++b)
        {
            const T x = mExcess[b];
            if (x <= floorDb) { mEvidence[b] = x; continue; }

            const T h = harmonicSupport (b, B);
            const T c = combSupport (b, B);
            const T musical = h > c ? h : c;
            mEvidence[b] = x * toneDiscount (mNear[b]) * ((T) 1 - musical);
        }

        buildCandidates (B, thresholdDb);
    }

    int numCandidates() const noexcept { return mNumCandidates; }
    const Candidate<T>& candidate (int i) const noexcept { return mCand[i]; }

private:
    T alphaFor (T ms) const noexcept
    {
        const double tau = (double) ms * 0.001;
        if (tau <= 0.0) return (T) 1;
        return (T) (1.0 - std::exp (-1.0 / (tau * mFrameRate)));
    }

    T audibility (T f) const noexcept
    {
        const double lo = (double) f / (double) mT.weightLowHz;
        const double hi = (double) mT.weightHighHz / (double) f;
        const double wl = lo * lo / (1.0 + lo * lo);
        const double wh = hi * hi / (1.0 + hi * hi);
        return (T) (0.15 + 0.85 * wl * wh);
    }

    T toneDiscount (T excessDb) const noexcept
    {
        if (excessDb <= mT.toneFloorDb) return (T) 1;
        if (excessDb >= mT.toneCeilDb)  return (T) 0;
        const T u = (excessDb - mT.toneFloorDb) / (mT.toneCeilDb - mT.toneFloorDb);
        return (T) 1 - u * u * ((T) 3 - (T) 2 * u);
    }

    // protection for a peak with peaks at f / 2, f / 3 or at both 2 f and 3 f
    T harmonicSupport (int b, int B) const noexcept
    {
        const T x = mExcess[b];
        if (x <= (T) 1)
            return (T) 0;

        T best = (T) 0;

        for (int n = 2; n <= 3; ++n)
        {
            const T e = excessPeakAt (mCentre[b] / (T) n, B);
            if (e > best) best = e;
        }

        {
            const T e2 = excessPeakAt (mCentre[b] * (T) 2, B);
            const T e3 = excessPeakAt (mCentre[b] * (T) 3, B);
            const T pair = e2 < e3 ? e2 : e3;
            if (pair > best) best = pair;
        }

        if (best < mT.harmonicMinDb)
            return (T) 0;

        const T frac = best / x;
        if (frac <= mT.harmonicNoneFrac) return (T) 0;
        if (frac >= mT.harmonicFullFrac) return mT.harmonicStrength;

        const T u = (frac - mT.harmonicNoneFrac)
                  / (mT.harmonicFullFrac - mT.harmonicNoneFrac);
        return u * u * ((T) 3 - (T) 2 * u) * mT.harmonicStrength;
    }

    int bandOf (T f, int B) const noexcept
    {
        if (f <= mCentre[0] || f >= mCentre[B - 1])
            return -1;
        int lo = 0, hi = B - 1;
        while (hi - lo > 1)
        {
            const int mid = (lo + hi) >> 1;
            if (mCentre[mid] < f) lo = mid; else hi = mid;
        }
        return (f - mCentre[lo] < mCentre[hi] - f) ? lo : hi;
    }

    void smoothInto (const T* in, T* out, int B, int half) noexcept
    {
        mPrefix[0] = (T) 0;
        for (int b = 0; b < B; ++b)
            mPrefix[b + 1] = mPrefix[b] + in[b];

        for (int b = 0; b < B; ++b)
        {
            int lo = b - half; if (lo < 0) lo = 0;
            int hi = b + half; if (hi > B - 1) hi = B - 1;
            out[b] = (mPrefix[hi + 1] - mPrefix[lo]) / (T) (hi - lo + 1);
        }
    }

    // running maximum of the excess, decaying by mShadowStep per band in both directions
    void spreadMax (const T* in, T* out, int B) noexcept
    {
        T run = (T) 0;
        for (int b = 0; b < B; ++b)
        {
            run -= mShadowStep;
            if (run < (T) 0) run = (T) 0;
            if (in[b] > run) run = in[b];
            out[b] = run;
        }
        run = (T) 0;
        for (int b = B - 1; b >= 0; --b)
        {
            run -= mShadowStep;
            if (run < (T) 0) run = (T) 0;
            if (in[b] > run) run = in[b];
            if (run > out[b]) out[b] = run;
        }
    }

    bool isPeak (int i, int B, T minDb) const noexcept
    {
        if (i < 1 || i > B - 2)
            return false;
        return mExcess[i] >= minDb
            && mExcess[i] >= mExcess[i - 1]
            && mExcess[i] >= mExcess[i + 1];
    }

    T excessPeakAt (T f, int B) const noexcept
    {
        const int b = bandOf (f, B);
        if (b < 1 || b >= B - 1)
            return (T) 0;
        T m = (T) 0;
        for (int i = b - 1; i <= b + 1; ++i)
            if (isPeak (i, B, mT.harmonicMinDb) && mExcess[i] > m)
                m = mExcess[i];
        return m;
    }

    // protection for a peak inside an evenly spaced family of neighbouring peaks
    T combSupport (int b, int B) const noexcept
    {
        const T x = mExcess[b];
        if (x <= (T) 1)
            return (T) 0;

        T need = x * mT.combFrac;
        if (need < mT.combMinDb) need = mT.combMinDb;

        const T f  = mCentre[b];
        const T lo = f * (T) std::pow (2.0, -(double) mT.combNeighbourOct);
        const T hi = f * (T) std::pow (2.0,  (double) mT.combNeighbourOct);

        T f4[4] = { (T) 0, (T) 0, (T) 0, (T) 0 };
        {
            int found = 0;
            for (int i = b - 2; i >= 1 && found < 2; --i)
            {
                if (mCentre[i] < lo) break;
                if (mExcess[i] >= need && isPeak (i, B, mT.combMinDb))
                { f4[1 - found] = mCentre[i]; ++found; }
            }
            found = 0;
            for (int i = b + 2; i <= B - 2 && found < 2; ++i)
            {
                if (mCentre[i] > hi) break;
                if (mExcess[i] >= need && isPeak (i, B, mT.combMinDb))
                { f4[2 + found] = mCentre[i]; ++found; }
            }
        }

        T fam[5]; int nf = 0;
        if (f4[0] > (T) 0) fam[nf++] = f4[0];
        if (f4[1] > (T) 0) fam[nf++] = f4[1];
        fam[nf++] = f;
        if (f4[2] > (T) 0) fam[nf++] = f4[2];
        if (f4[3] > (T) 0) fam[nf++] = f4[3];

        if (nf < (int) mT.combMinPartials)
            return (T) 0;

        int agree = 0;
        for (int i = 0; i + 2 < nf; ++i)
        {
            const T d1 = fam[i + 1] - fam[i];
            const T d2 = fam[i + 2] - fam[i + 1];
            if (d1 <= (T) 0 || d2 <= (T) 0) continue;
            const T big = d1 > d2 ? d1 : d2;
            if ((T) std::fabs ((double) (d1 - d2)) <= big * mT.combSpacingTol)
                ++agree;
        }

        if (agree == 0)
            return (T) 0;

        return mT.combStrength;
    }

    // local evidence maxima above threshold, narrow enough, inside the user window -> ranked candidates
    void buildCandidates (int B, T thresholdDb) noexcept
    {
        mNumCandidates = 0;

        int first = 1, last = B - 2;

        while (first < B - 2
               && (T) std::log2 ((double) mCentre[first] / (double) mCentre[0]) < mT.edgeMarginOctLow)
            ++first;

        while (last > 1
               && (T) std::log2 ((double) mCentre[B - 1] / (double) mCentre[last]) < mT.edgeMarginOctHigh)
            --last;

        if (first < 1) first = 1;
        if (last > B - 2) last = B - 2;

        for (int b = first; b <= last; ++b)
        {
            const T m    = (T) std::pow (2.0, (double) mT.candEdgeMarginOct);
            const T uLo  = mUserLoHz / m;
            const T uHi  = mUserHiHz * m;

            const T loHz = uLo > mT.minCandidateHz ? uLo : mT.minCandidateHz;
            const T hiHz = uHi < mT.maxCandidateHz ? uHi : mT.maxCandidateHz;

            if (mCentre[b] < loHz || mCentre[b] > hiHz)
                continue;

            const T e = mEvidence[b];
            if (e <= thresholdDb)
                continue;
            if (e < mEvidence[b - 1] || e < mEvidence[b + 1])
                continue;

            const T halfE = e * (T) 0.5;
            int lo = b, hi = b;
            while (lo > 0      && mEvidence[lo] > halfE) --lo;
            while (hi < B - 1  && mEvidence[hi] > halfE) ++hi;

            const T fLo = mCentre[lo], fHi = mCentre[hi];
            const T wOct = (T) std::log2 ((double) fHi / (double) (fLo > (T) 1 ? fLo : (T) 1));

            const T widthLimit = mT.maxWidthOct + mWidth[b] * (T) 2;
            if (wOct > widthLimit)
                continue;

            // parabolic peak interpolation
            const T y0 = mEvidence[b - 1], y1 = e, y2 = mEvidence[b + 1];
            const T den = y0 - (T) 2 * y1 + y2;
            T shift = (T) 0;
            if (den < (T) -1.0e-9)
                shift = (T) 0.5 * (y0 - y2) / den;
            if (shift > (T) 1) shift = (T) 1;
            if (shift < (T) -1) shift = (T) -1;

            const T fc = (shift >= (T) 0)
                       ? mCentre[b] + shift * (mCentre[b + 1] - mCentre[b])
                       : mCentre[b] + shift * (mCentre[b] - mCentre[b - 1]);

            T w = wOct;
            if (w < mWidth[b] * (T) 2) w = mWidth[b] * (T) 2;

            T q = (T) (1.0 / (std::pow (2.0, (double) w * 0.5)
                            - std::pow (2.0, (double) -w * 0.5)));
            q *= mT.qScale;
            if (q < mT.minQ) q = mT.minQ;
            if (q > mT.maxQ) q = mT.maxQ;

            const T sev = (e - thresholdDb) * mWeight[b];
            if (sev <= (T) 0)
                continue;

            insertRanked (fc, q, sev, e);
        }
    }

    // by severity; a weaker candidate within minSeparationOct of another is dropped
    void insertRanked (T f, T q, T sev, T ev) noexcept
    {
        for (int i = 0; i < mNumCandidates; ++i)
        {
            const T sep = (T) std::fabs (std::log2 ((double) f / (double) mCand[i].frequency));
            if (sep < mT.minSeparationOct)
            {
                if (sev <= mCand[i].severity)
                    return;
                for (int j = i; j < mNumCandidates - 1; ++j) mCand[j] = mCand[j + 1];
                --mNumCandidates;
                break;
            }
        }

        int pos = mNumCandidates;
        while (pos > 0 && mCand[pos - 1].severity < sev) --pos;

        if (pos >= MaxCand)
            return;

        const int last = (mNumCandidates < MaxCand ? mNumCandidates : MaxCand - 1);
        for (int j = last; j > pos; --j) mCand[j] = mCand[j - 1];

        mCand[pos].frequency = f;
        mCand[pos].q         = q;
        mCand[pos].severity  = sev;
        mCand[pos].evidence  = ev;

        if (mNumCandidates < MaxCand) ++mNumCandidates;
    }

    Tuning<T> mT;

    int    mNumBands  = 0;
    double mFrameRate = 93.75;
    int    mHalfWin   = 8;
    T      mUserLoHz = (T) 0;
    T      mUserHiHz = (T) 1.0e9;
    T      mRise = (T) 0.02, mFall = (T) 0.04;

    T mCentre[MaxBands]  = { (T) 0 };
    T mWidth[MaxBands]   = { (T) 0 };
    T mWeight[MaxBands]  = { (T) 1 };
    T mLevel[MaxBands]   = { (T) -160 };
    T mAvg[MaxBands]     = { (T) -160 };
    T mNear[MaxBands]    = { (T) 0 };
    T mEnv[MaxBands]     = { (T) -160 };
    T mClipped[MaxBands] = { (T) -160 };
    T mExcess[MaxBands]  = { (T) 0 };
    T mEvidence[MaxBands]= { (T) 0 };
    T mPrefix[MaxBands + 1] = { (T) 0 };

    Candidate<T> mCand[MaxCand];
    int          mNumCandidates = 0;

    T    mFrameLevelDb = (T) -160;
    T    mPrevFrameDb  = (T) -160;
    T    mShadowStep   = (T) 2.6;
    bool mTransient    = false;
};

// ==================================================================================================
// Ranges, defaults and fixed settings
// ==================================================================================================

template <typename T>
struct Limits
{
    static constexpr T thresholdMinDb()     { return (T) 2; }
    static constexpr T thresholdMaxDb()     { return (T) 15; }
    static constexpr T thresholdDefaultDb() { return (T) 6; }

    static constexpr T amountMax()          { return (T) 2; }
    static constexpr T amountDefault()      { return (T) 0.5; }

    // notch depth: evidence x depthSlope, capped at depthCeilingDb, x gate x Amount, capped at depthMaxDb.
    // Twice the original Cleaner depths (0.9 / 18 / 24 / 0.02 dB): every worker's depth is exactly doubled.
    static constexpr T depthSlope()         { return (T) 1.8; }
    static constexpr T depthCeilingDb()     { return (T) 36; }
    static constexpr T depthMaxDb()         { return (T) 48; }
    static constexpr T workerOffDb()        { return (T) 0.04; }   // a released notch below this frees its worker
    static constexpr T kneeDb()             { return (T) 3; }      // gate fade-in above threshold

    static constexpr T attackMs()           { return (T) 22; }
    static constexpr T releaseMs()          { return (T) 92; }
    static constexpr T attackMinMs()        { return (T) 20; }
    static constexpr T attackMaxMs()        { return (T) 500; }
    static constexpr T releaseMinMs()       { return (T) 60; }
    static constexpr T releaseMaxMs()       { return (T) 2000; }

    static constexpr T limMinHz()           { return (T) 60; }
    static constexpr T limMaxHz()           { return (T) 17000; }
    static constexpr T lowLimDefaultHz()    { return (T) 350; }
    static constexpr T highLimDefaultHz()   { return (T) 12500; }

    static constexpr T hearDefault()        { return (T) 0; }
    static constexpr T hearFadeMs()         { return (T) 12; }

    static constexpr T holdMs()             { return (T) 250; }    // a worker keeps its notch this long without a match
    static constexpr T trackOct()           { return (T) 0.20; }   // a worker follows a candidate within this distance
    static constexpr T trackGlide()         { return (T) 0.25; }   // per-frame frequency / Q glide

    // adaptive Q: detector Q x qWidth, clamped
    static constexpr T qWidth()             { return (T) 12; }
    static constexpr T qFinalMin()          { return (T) 0.3; }
    static constexpr T qFinalMax()          { return (T) 120; }

    static constexpr int workersMin()       { return 1; }
    static constexpr int workersMax()       { return 32; }
    static constexpr int workersDefault()   { return 8; }

    static constexpr double meterRangeDb()  { return 48.0; }     // = depthMaxDb (script: kMeterMaxDb)
};

// ==================================================================================================
// Reduction meter: the combined magnitude response of all notches (with a 90 ms visual release) on
// Bins log-spaced points, 0..1 of Limits::meterRangeDb
// ==================================================================================================

template <typename T, int NBins>
class Meter
{
public:
    static constexpr int Bins = NBins;

    static constexpr double LoHz = 50.0;
    static constexpr double HiHz = 16000.0;
    static constexpr double RangeDb = Limits<T>::meterRangeDb();

    static constexpr double MinDrawPoints = 3.0;                 // narrowest drawn notch, in points
    static constexpr double SpanOctaves = 8.321928094887362;     // log2 (HiHz / LoHz)

    static constexpr double MaxDrawQ()
    { return 1.0 / (MinDrawPoints * (SpanOctaves / (double) (Bins - 1))); }

    static constexpr double VisReleaseSeconds = 0.09;
    static constexpr double VisFloorDb = 0.1;                    // dB, scales with RangeDb

    void prepare() noexcept
    {
        const double ratio = HiHz / LoHz;
        for (int i = 0; i < Bins; ++i)
        {
            const double u = (double) i / (double) (Bins - 1);
            mFreq[i] = (T) (LoHz * std::pow (ratio, u));
        }
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < Bins; ++i)
            mCurve[i] = (T) 0;
        for (int w = 0; w < MaxWorkers; ++w)
        {
            mVisDepth[w] = (T) 0;
            mVisFreq[w]  = (T) 0;
            mVisQ[w]     = (T) 1;
        }
        ++mGeneration;
    }

    void render (const T* freq, const T* q, const T* depthDb,
                 const bool* active, int n, double dtSeconds) noexcept
    {
        if (n > MaxWorkers) n = MaxWorkers;

        const double decay = (dtSeconds > 0.0 && VisReleaseSeconds > 0.0)
                           ? std::exp (-dtSeconds / VisReleaseSeconds)
                           : 0.0;

        for (int w = 0; w < n; ++w)
        {
            const T real = (active[w] && depthDb[w] > (T) 0) ? depthDb[w] : (T) 0;

            if (real > mVisDepth[w])
            {
                mVisDepth[w] = real;
            }
            else
            {
                const T floored = (T) ((double) mVisDepth[w] * decay);
                mVisDepth[w] = (real > floored) ? real : floored;
            }

            if (mVisDepth[w] < (T) VisFloorDb)
                mVisDepth[w] = (T) 0;

            if (active[w] && freq[w] > (T) 1)
            {
                mVisFreq[w] = freq[w];
                mVisQ[w]    = q[w];
            }
        }

        // complex sum of the analog bell responses
        for (int i = 0; i < Bins; ++i) { mRe[i] = 1.0; mIm[i] = 0.0; }

        for (int w = 0; w < n; ++w)
        {
            const double d = (double) mVisDepth[w];
            if (! (d > 0.0))
                continue;

            const double f0 = (double) mVisFreq[w];
            if (! (f0 > 1.0))
                continue;

            double qq = (double) mVisQ[w];
            if (qq < 0.05) qq = 0.05;
            if (qq > MaxDrawQ()) qq = MaxDrawQ();

            const double A  = std::pow (10.0, -d * 0.025);
            const double k  = 1.0 / (qq * A);
            const double mix = k * (A * A - 1.0);

            for (int i = 0; i < Bins; ++i)
            {
                const double om = (double) mFreq[i] / f0;
                const double u  = 1.0 - om * om;
                const double v  = om * k;
                const double den = u * u + v * v;
                if (den < 1.0e-300)
                    continue;

                const double inv = 1.0 / den;
                mRe[i] += mix * (om * v * inv);
                mIm[i] += mix * (om * u * inv);
            }
        }

        for (int i = 0; i < Bins; ++i)
        {
            const double mag2 = mRe[i] * mRe[i] + mIm[i] * mIm[i];
            double dB = 0.0;
            if (mag2 > 0.0 && mag2 < 1.0)
                dB = -10.0 * std::log10 (mag2);
            mCurve[i] = norm ((T) dB);
        }

        // each notch's own depth at its nearest point (narrow notches fall between points)
        for (int w = 0; w < n; ++w)
        {
            if (! (mVisDepth[w] > (T) 0) || ! (mVisFreq[w] > (T) 1))
                continue;
            const int i = nearestBin (mVisFreq[w]);
            if (i < 0)
                continue;
            const T v = norm (mVisDepth[w]);
            if (v > mCurve[i])
                mCurve[i] = v;
        }

        ++mGeneration;
    }

    const T* curve() const noexcept { return mCurve; }

    unsigned generation() const noexcept { return mGeneration; }

    T peakReductionDb() const noexcept
    {
        T m = (T) 0;
        for (int i = 0; i < Bins; ++i)
            if (mCurve[i] > m) m = mCurve[i];
        return m * (T) RangeDb;
    }

private:
    T norm (T dB) noexcept
    {
        const T v = dB * (T) (1.0 / RangeDb);
        return v < (T) 0 ? (T) 0 : (v > (T) 1 ? (T) 1 : v);
    }

    int nearestBin (T f) const noexcept
    {
        if (! (f > (T) 0))
            return -1;
        const double u = std::log ((double) f / LoHz) / std::log (HiHz / LoHz);
        int i = (int) (u * (double) (Bins - 1) + 0.5);
        if (i < 0) i = 0;
        if (i > Bins - 1) i = Bins - 1;
        return i;
    }

    static constexpr int MaxWorkers = 64;

    T mFreq[Bins]  = { (T) 0 };
    T mCurve[Bins] = { (T) 0 };

    double mRe[Bins] = { 1.0 };
    double mIm[Bins] = { 0.0 };

    T mVisDepth[MaxWorkers] = { (T) 0 };
    T mVisFreq[MaxWorkers]  = { (T) 0 };
    T mVisQ[MaxWorkers]     = { (T) 1 };

    unsigned mGeneration = 0;
};

// ns curve points -> n display points (nearest when upsampling, per-point maximum when downsampling)
template <typename T>
inline void resampleMeterCurve (const T* src, int ns, float* dst, int n) noexcept
{
    if (src == nullptr || dst == nullptr || ns <= 0 || n <= 0)
        return;

    if (n == 1) { dst[0] = (float) src[0]; return; }
    if (ns == 1) { for (int i = 0; i < n; ++i) dst[i] = (float) src[0]; return; }

    if (n >= ns)
    {
        for (int i = 0; i < n; ++i)
        {
            const double u = (double) i / (double) (n - 1);
            int j = (int) (u * (double) (ns - 1) + 0.5);
            if (j < 0) j = 0;
            if (j > ns - 1) j = ns - 1;
            dst[i] = (float) src[j];
        }
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        const double c  = (double) i * (double) (ns - 1) / (double) (n - 1);
        const double hw = 0.5 * (double) (ns - 1) / (double) (n - 1);
        int lo = (int) std::ceil  (c - hw);
        int hi = (int) std::floor (c + hw);
        if (lo < 0) lo = 0;
        if (hi > ns - 1) hi = ns - 1;
        if (hi < lo) { lo = hi = (int) (c + 0.5); if (lo < 0) lo = 0; if (lo > ns - 1) lo = ns - 1; hi = lo; }

        T m = src[lo];
        for (int j = lo + 1; j <= hi; ++j) if (src[j] > m) m = src[j];
        dst[i] = (float) m;
    }
}

// ==================================================================================================
// Engine: mid-signal analysis -> detector -> worker allocation -> per-channel bell banks
// ==================================================================================================

template <typename T>
struct Params
{
    T thresholdDb = (T) 6;
    T amount      = (T) 0.5;
    T attackMs    = (T) 22;
    T releaseMs   = (T) 92;

    T lowLimHz    = (T) 350;
    T highLimHz   = (T) 12500;

    int workers = 8;

    T hear = (T) 0;          // 0 = output, 1 = removed signal (dry - processed)
};

template <typename T, int NumWorkers, int FftSize>
class Engine
{
public:
    static constexpr int MaxBands = 224;

    using AnalyserType = Analyser<T, FftSize, 4, MaxBands>;
    using DetectorType = Detector<T, MaxBands, NumWorkers * 2>;
    using BankType     = BellBank<T, NumWorkers>;
    using MeterType    = Meter<T, 256>;
    using Lim          = Limits<T>;

    void prepare (double sampleRate) noexcept
    {
        mRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        mAnalyser.prepare (mRate, 24.0, 45.0, 19000.0);

        T centres[MaxBands], widths[MaxBands];
        const int nb = mAnalyser.numBands();
        for (int b = 0; b < nb; ++b)
        {
            centres[b] = mAnalyser.bandCentre (b);
            widths[b]  = mAnalyser.bandWidthOct (b);
        }

        mFrameRate = mAnalyser.analysisRate() / (double) AnalyserType::Hop;
        mDetector.prepare (centres, widths, nb, mFrameRate);

        mBank[0].prepare (mRate);
        mBank[1].prepare (mRate);

        mMeter.prepare();

        mAttack  = frameAlpha (mParams.attackMs);
        mRelease = frameAlpha (mParams.releaseMs);
        mHoldFrames = (int) (mFrameRate * (double) Lim::holdMs() * 0.001 + 0.5);

        {
            const double n = mRate * (double) Lim::hearFadeMs() * 0.001;
            mHearStep = (T) (n > 1.0 ? 1.0 / n : 1.0);
        }

        reset();
    }

    void reset() noexcept
    {
        mAnalyser.reset();
        mDetector.reset();
        mBank[0].reset();
        mBank[1].reset();

        mMeter.reset();
        mMeterCountdown = 0;
        mMeterFrames    = 0;

        for (int i = 0; i < NumWorkers; ++i)
            mWorker[i] = Worker();

        mHearGain = mParams.hear;

        mSampleCount = 0;
    }

    // sanitises (NaN -> default, clamps) and applies a full parameter set
    void setParameters (const Params<T>& p) noexcept
    {
        Params<T> v = p;
        if (! (v.thresholdDb == v.thresholdDb)) v.thresholdDb = Lim::thresholdDefaultDb();
        if (! (v.amount == v.amount))           v.amount = (T) 0;
        if (! (v.attackMs == v.attackMs))   v.attackMs  = Lim::attackMs();
        if (! (v.releaseMs == v.releaseMs)) v.releaseMs = Lim::releaseMs();

        v.thresholdDb = clamp (v.thresholdDb, Lim::thresholdMinDb(), Lim::thresholdMaxDb());
        v.amount      = clamp (v.amount, (T) 0, Lim::amountMax());
        v.attackMs    = clamp (v.attackMs,  Lim::attackMinMs(),  Lim::attackMaxMs());
        v.releaseMs   = clamp (v.releaseMs, Lim::releaseMinMs(), Lim::releaseMaxMs());

        if (! (v.lowLimHz  == v.lowLimHz))  v.lowLimHz  = Lim::lowLimDefaultHz();
        if (! (v.highLimHz == v.highLimHz)) v.highLimHz = Lim::highLimDefaultHz();
        v.lowLimHz  = clamp (v.lowLimHz,  Lim::limMinHz(), Lim::limMaxHz());
        v.highLimHz = clamp (v.highLimHz, Lim::limMinHz(), Lim::limMaxHz());

        if (v.lowLimHz > v.highLimHz) v.lowLimHz = v.highLimHz;

        mDetector.setCandidateRange (v.lowLimHz, v.highLimHz);

        if (v.workers < Lim::workersMin()) v.workers = Lim::workersMin();
        if (v.workers > NumWorkers)        v.workers = NumWorkers;

        if (v.attackMs != mParams.attackMs || mAttack <= (T) 0)
            mAttack = frameAlpha (v.attackMs);
        if (v.releaseMs != mParams.releaseMs || mRelease <= (T) 0)
            mRelease = frameAlpha (v.releaseMs);

        if (! (v.hear == v.hear)) v.hear = (T) 0;
        v.hear = clamp (v.hear, (T) 0, (T) 1);

        mParams = v;
    }

    // non-finite input samples are zeroed; 64-sample chunks aligned to the running sample count
    void processBlock (T* const* ch, int numChannels, int n) noexcept
    {
        if (n <= 0 || numChannels <= 0)
            return;

        ScopedDenormals denormalGuard;

        const bool stereo = (numChannels >= 2);

        for (int c = 0; c < (stereo ? 2 : 1); ++c)
        {
            T* p = ch[c];
            for (int i = 0; i < n; ++i)
                if (! (p[i] - p[i] == (T) 0))
                    p[i] = (T) 0;
        }

        int done = 0;
        while (done < n)
        {
            const int untilBoundary = kChunk - (int) (mSampleCount % (long long) kChunk);
            int take = (n - done) < untilBoundary ? (n - done) : untilBoundary;
            if (take > kChunk) take = kChunk;

            if (stereo)
                for (int i = 0; i < take; ++i)
                    mMid[i] = (T) 0.5 * (ch[0][done + i] + ch[1][done + i]);
            else
                for (int i = 0; i < take; ++i)
                    mMid[i] = ch[0][done + i];

            const bool hearOn = (mHearGain > (T) 0 || mParams.hear > (T) 0);
            if (hearOn)
            {
                for (int i = 0; i < take; ++i) mDry[0][i] = ch[0][done + i];
                if (stereo)
                    for (int i = 0; i < take; ++i) mDry[1][i] = ch[1][done + i];
            }

            mBank[0].processBlock (ch[0] + done, take);
            if (stereo)
                mBank[1].processBlock (ch[1] + done, take);

            if (hearOn)
                mixHear (ch, stereo, done, take);

            mAnalyser.pushBlock (mMid, take,
                [this] (const T* bandPower, int numBands) noexcept
                {
                    mDetector.processFrame (bandPower, numBands, mParams.thresholdDb);
                    allocate();
                });

            mSampleCount += take;
            done += take;
        }
    }

    const T* meterCurve() const noexcept        { return mMeter.curve(); }
    static constexpr int meterBins()            { return MeterType::Bins; }
    unsigned meterGeneration() const noexcept   { return mMeter.generation(); }
    T peakReductionDb() const noexcept          { return mMeter.peakReductionDb(); }

private:
    static constexpr int kChunk = 64;
    static constexpr int kMeterDivider = 3;     // meter renders every 3rd analysis frame

    struct Worker
    {
        bool active   = false;
        T    freq     = (T) 1000;
        T    q        = (T) 4;
        T    depthDb  = (T) 0;
        T    targetDb = (T) 0;
        int  misses   = 0;
    };

    static T clamp (T v, T lo, T hi) noexcept
    { return v < lo ? lo : (v > hi ? hi : v); }

    // crossfade (12 ms) between the processed and the removed (dry - processed) signal
    void mixHear (T* const* ch, bool stereo, int done, int take) noexcept
    {
        const T target = mParams.hear;
        const T step   = mHearStep;

        T g = mHearGain;
        for (int i = 0; i < take; ++i)
        {
            if (g < target)      { g += step; if (g > target) g = target; }
            else if (g > target) { g -= step; if (g < target) g = target; }

            const T p0 = ch[0][done + i];
            const T r0 = mDry[0][i] - p0;
            ch[0][done + i] = p0 + g * (r0 - p0);

            if (stereo)
            {
                const T p1 = ch[1][done + i];
                const T r1 = mDry[1][i] - p1;
                ch[1][done + i] = p1 + g * (r1 - p1);
            }
        }
        mHearGain = g;
    }

    T frameAlpha (T ms) const noexcept
    {
        const double tau = (double) ms * 0.001;
        if (tau <= 0.0) return (T) 1;
        return (T) (1.0 - std::exp (-1.0 / (tau * mFrameRate)));
    }

    // once per analysis frame: match workers to candidates, start new workers, smooth depths, update filters
    void allocate() noexcept
    {
        const int nc = mDetector.numCandidates();

        const int limit = mParams.workers < NumWorkers ? mParams.workers : NumWorkers;
        for (int w = limit; w < NumWorkers; ++w)
            if (mWorker[w].active)
                mWorker[w].targetDb = (T) 0;

        bool taken[NumWorkers * 2];
        for (int i = 0; i < nc; ++i) taken[i] = false;

        // active workers follow their nearest free candidate, or release after holdMs
        for (int w = 0; w < limit; ++w)
        {
            if (! mWorker[w].active)
                continue;

            int best = -1;
            T bestSep = Lim::trackOct();
            for (int i = 0; i < nc; ++i)
            {
                if (taken[i]) continue;
                const T sep = (T) std::fabs (std::log2 ((double) mDetector.candidate (i).frequency
                                                      / (double) mWorker[w].freq));
                if (sep < bestSep) { bestSep = sep; best = i; }
            }

            if (best >= 0)
            {
                taken[best] = true;
                const auto& c = mDetector.candidate (best);
                mWorker[w].freq += (c.frequency - mWorker[w].freq) * Lim::trackGlide();

                mWorker[w].q    += (finalQFor (c) - mWorker[w].q) * Lim::trackGlide();
                mWorker[w].targetDb = depthFor (c);
                mWorker[w].misses = 0;
            }
            else
            {
                if (++mWorker[w].misses > mHoldFrames)
                    mWorker[w].targetDb = (T) 0;
            }
        }

        // remaining candidates start idle workers (not too close to an active one)
        for (int i = 0; i < nc; ++i)
        {
            if (taken[i]) continue;
            const auto& c = mDetector.candidate (i);

            bool clash = false;
            for (int w = 0; w < NumWorkers && ! clash; ++w)
                if (mWorker[w].active)
                    clash = std::fabs (std::log2 ((double) c.frequency
                                                / (double) mWorker[w].freq))
                            < (double) mDetector.tuning().minSeparationOct;
            if (clash) continue;

            for (int w = 0; w < limit; ++w)
            {
                if (mWorker[w].active) continue;
                mWorker[w].active   = true;
                mWorker[w].freq     = c.frequency;
                mWorker[w].q        = finalQFor (c);
                mWorker[w].depthDb  = (T) 0;
                mWorker[w].targetDb = depthFor (c);
                mWorker[w].misses   = 0;
                mBank[0].clearWorker (w);
                mBank[1].clearWorker (w);
                break;
            }
        }

        // depth smoothing (attack / release), filter update
        for (int w = 0; w < NumWorkers; ++w)
        {
            Worker& k = mWorker[w];
            if (! k.active)
                continue;

            const T a = (k.targetDb > k.depthDb) ? mAttack : mRelease;
            k.depthDb += (k.targetDb - k.depthDb) * a;

            if (k.targetDb <= (T) 0 && k.depthDb < Lim::workerOffDb())
            {
                k.active  = false;
                k.depthDb = (T) 0;
                mBank[0].releaseWorker (w);
                mBank[1].releaseWorker (w);
                continue;
            }

            const T amplitude = (T) std::pow (10.0, (double) -k.depthDb * 0.025);
            mBank[0].setWorker (w, k.freq, k.q, amplitude);
            mBank[1].setWorker (w, k.freq, k.q, amplitude);
        }

        renderMeter();
    }

    void renderMeter() noexcept
    {
        ++mMeterFrames;

        if (--mMeterCountdown > 0)
            return;
        mMeterCountdown = kMeterDivider;

        const double dt = (mFrameRate > 0.0)
                        ? (double) mMeterFrames / mFrameRate : 0.0;
        mMeterFrames = 0;

        T f[NumWorkers], q[NumWorkers], d[NumWorkers];
        bool a[NumWorkers];
        for (int w = 0; w < NumWorkers; ++w)
        {
            f[w] = mWorker[w].freq;
            q[w] = mWorker[w].q;
            d[w] = mWorker[w].depthDb;
            a[w] = mWorker[w].active;
        }
        mMeter.render (f, q, d, a, NumWorkers, dt);
    }

    T finalQFor (const Candidate<T>& c) const noexcept
    {
        const T q = c.q * Lim::qWidth();
        const T lo = Lim::qFinalMin(), hi = Lim::qFinalMax();
        return q < lo ? lo : (q > hi ? hi : q);
    }

    T depthFor (const Candidate<T>& c) const noexcept
    {
        const T ev = c.evidence;
        if (ev <= (T) 0)
            return (T) 0;

        const T qualifying = ev * weightOf (c);
        const T over = qualifying - mParams.thresholdDb;
        if (over <= (T) 0)
            return (T) 0;

        T gate = over / Lim::kneeDb();
        if (gate > (T) 1) gate = (T) 1;
        gate = gate * gate * ((T) 3 - (T) 2 * gate);

        T d = ev * Lim::depthSlope();
        const T ceiling = Lim::depthCeilingDb();
        if (d > ceiling) d = ceiling;

        d = d * gate * mParams.amount;

        const T hard = Lim::depthMaxDb();
        return d > hard ? hard : d;
    }

    // audibility-weighted share of the evidence (severity = (evidence - threshold) x weight)
    T weightOf (const Candidate<T>& c) const noexcept
    {
        if (c.evidence <= (T) 0)
            return (T) 1;
        const T w = (c.severity + mParams.thresholdDb) / c.evidence;
        return w > (T) 0 ? (w < (T) 1 ? w : (T) 1) : (T) 0;
    }

    AnalyserType mAnalyser;
    DetectorType mDetector;
    BankType     mBank[2];
    MeterType    mMeter;
    int          mMeterCountdown = 0;
    int          mMeterFrames = 0;
    Worker       mWorker[NumWorkers];
    Params<T>    mParams;

    double mRate = 48000.0, mFrameRate = 93.75;
    int    mHoldFrames = 24;
    T      mAttack = (T) 0.1, mRelease = (T) 0.04;

    T   mMid[kChunk] = { (T) 0 };
    T   mDry[2][kChunk] = { { (T) 0 }, { (T) 0 } };
    T   mHearGain = (T) 0;
    T   mHearStep = (T) 1;

    long long mSampleCount = 0;
};

}

// ==================================================================================================
// HISE node
// ==================================================================================================

namespace project
{
using namespace juce;
using namespace hise;
using namespace scriptnode;

template <int NV> struct BK_PeakwiseSupressor: public data::base
{
	SNEX_NODE(BK_PeakwiseSupressor);

	struct MetadataClass
	{
		SN_NODE_ID("BK_PeakwiseSupressor");
	};

	static constexpr bool isModNode() { return false; };
	static constexpr bool isPolyphonic() { return NV > 1; };
	static constexpr bool hasTail() { return true; };
	static constexpr bool isSuspendedOnSilence() { return false; };

	static constexpr int NumChannels = 2;
	static constexpr int getFixChannelAmount() { return NumChannels; };

	static constexpr int NumTables = 0;
	static constexpr int NumSliderPacks = 1;
	static constexpr int NumAudioFiles = 0;
	static constexpr int NumDisplayBuffers = 0;
	static constexpr int NumFilters = 0;

	static constexpr int NumWorkers = 32;

	using EngineType = pkws::Engine<float, NumWorkers, 2048>;
	using Lim        = pkws::Limits<float>;

	BK_PeakwiseSupressor()
	{
		params.thresholdDb = Lim::thresholdDefaultDb();
		params.amount      = Lim::amountDefault();
		params.attackMs    = Lim::attackMs();
		params.releaseMs   = Lim::releaseMs();
		params.workers     = Lim::workersDefault();
		params.lowLimHz    = Lim::lowLimDefaultHz();
		params.highLimHz   = Lim::highLimDefaultHz();
		params.hear        = Lim::hearDefault();
		engine.setParameters(params);
	}

	void prepare(PrepareSpecs specs)
	{
		if (specs.numChannels != getFixChannelAmount())
		{
#if THROW_SCRIPTNODE_ERRORS == 0
			Error::throwError(Error::ChannelMismatch, getFixChannelAmount(), specs.numChannels);
#else
			jassertfalse;
#endif
			return;
		}

		if (specs.sampleRate <= 0.0 || specs.blockSize <= 0)
		{
			jassertfalse;
			return;
		}

		engine.prepare(specs.sampleRate);
		engine.setParameters(params);
		engine.reset();
	}

	void reset()
	{
		engine.reset();
	}

	void handleHiseEvent(HiseEvent& e)
	{
		ignoreUnused(e);
	}

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		auto& fixData = data.template as<ProcessData<NumChannels>>();
		auto** ch = fixData.getRawChannelPointers();
		const int n = fixData.getNumSamples();

		engine.processBlock(ch, NumChannels, n);

		publishMeter();
	}

	template <typename FrameDataType> void processFrame(FrameDataType& data)
	{
		auto* d = data.begin();
		const int n = (int) data.size();
		if (n <= 0)
			return;

		float l = d[0];
		float r = (n > 1) ? d[1] : l;
		float* ch[2] = { &l, &r };

		engine.processBlock(ch, (n > 1) ? 2 : 1, 1);

		d[0] = l;
		if (n > 1)
			d[1] = r;
	}

	enum Parameters
	{
		Threshold,
		Amount,
		Attack,
		Release,
		Workers,
		LowLim,
		HighLim,
		Hear,
		numParameters
	};

	template <int P> void setParameter(double v)
	{
		if constexpr (P == Threshold)
			params.thresholdDb = clampRange(v, (double) Lim::thresholdMinDb(), (double) Lim::thresholdMaxDb(),
			                                (double) Lim::thresholdDefaultDb());

		if constexpr (P == Amount)
			params.amount = clampRange(v, 0.0, (double) Lim::amountMax(), (double) Lim::amountDefault());

		if constexpr (P == Attack)
			params.attackMs = clampRange(v, (double) Lim::attackMinMs(), (double) Lim::attackMaxMs(),
			                             (double) Lim::attackMs());

		if constexpr (P == Release)
			params.releaseMs = clampRange(v, (double) Lim::releaseMinMs(), (double) Lim::releaseMaxMs(),
			                              (double) Lim::releaseMs());

		// worker count = rounded value + 1 (kept for preset compatibility)
		if constexpr (P == Workers)
			params.workers = steppedIndex(v, Lim::workersMax() - Lim::workersMin() + 1) + Lim::workersMin();

		if constexpr (P == LowLim)
			params.lowLimHz = clampRange(v, (double) Lim::limMinHz(), (double) Lim::limMaxHz(),
			                             (double) Lim::lowLimDefaultHz());

		if constexpr (P == HighLim)
			params.highLimHz = clampRange(v, (double) Lim::limMinHz(), (double) Lim::limMaxHz(),
			                              (double) Lim::highLimDefaultHz());

		if constexpr (P == Hear)
			params.hear = clampRange(v, 0.0, 1.0, (double) Lim::hearDefault());

		engine.setParameters(params);
	}

	void createParameters(ParameterDataList& data)
	{
		addParameter<Threshold>(data, "Threshold", Lim::thresholdMinDb(), Lim::thresholdMaxDb(), Lim::thresholdDefaultDb());
		addParameter<Amount>(data, "Amount", 0.0, Lim::amountMax(), Lim::amountDefault());
		addParameter<Attack>(data, "Attack", Lim::attackMinMs(), Lim::attackMaxMs(), Lim::attackMs());
		addParameter<Release>(data, "Release", Lim::releaseMinMs(), Lim::releaseMaxMs(), Lim::releaseMs());
		addParameter<Workers>(data, "Workers", Lim::workersMin(), Lim::workersMax(), Lim::workersDefault());
		addParameter<LowLim>(data, "LowLim", Lim::limMinHz(), Lim::limMaxHz(), Lim::lowLimDefaultHz());
		addParameter<HighLim>(data, "HighLim", Lim::limMinHz(), Lim::limMaxHz(), Lim::highLimDefaultHz());

		parameter::data p("Hear", { 0.0, 1.0 });
		registerCallback<Hear>(p);
		p.setParameterValueNames({ "Output", "Removed" });
		p.setDefaultValue((double) Lim::hearDefault());
		data.add(std::move(p));
	}

	void setExternalData(const ExternalData& d, int index)
	{
		if (index == 0)
		{
			meterData = d;
			d.referBlockTo(meterBlock, 0);
		}
	}

	// copies a new meter curve into SliderPack 0 (display value = peak reduction in dB)
	void publishMeter()
	{
		const unsigned gen = engine.meterGeneration();
		if (gen == publishedGeneration)
			return;

		if (meterData.isEmpty())
			return;

		DataReadLock lock(meterData, true);
		if (!lock)
			return;

		const int n = (int) meterBlock.size();
		if (n <= 0)
			return;

		publishedGeneration = gen;

		pkws::resampleMeterCurve (engine.meterCurve(), EngineType::meterBins(), meterBlock.begin(), n);

		meterData.setDisplayedValue((double) engine.peakReductionDb());
	}

private:

	template <int P> void addParameter(ParameterDataList& data, const char* name, double lo, double hi, double def)
	{
		parameter::data p(name, { lo, hi });
		registerCallback<P>(p);
		p.setDefaultValue(def);
		data.add(std::move(p));
	}

	static int steppedIndex(double v, int count) noexcept
	{
		if (!(v == v))
			return 0;
		if (v <= 0.0)
			return 0;
		if (v >= (double) (count - 1))
			return count - 1;
		const int i = (int) (v + 0.5);
		return i < 0 ? 0 : (i >= count ? count - 1 : i);
	}

	static double clampRange(double v, double lo, double hi, double onNaN) noexcept
	{
		if (!(v == v)) return onNaN;
		return v < lo ? lo : (v > hi ? hi : v);
	}

	EngineType                 engine;
	pkws::Params<float>        params;

	ExternalData meterData;
	block        meterBlock;
	unsigned     publishedGeneration = 0;
};

}
