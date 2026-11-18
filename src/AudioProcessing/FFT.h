#pragma once

#include <vector>
#include <complex>
#include <cstddef>
#include <cmath>
#include <utility>

// W3 (Session 304): a small, self-contained radix-2 fast Fourier transform.
//
// The noise reducer's forward/inverse transforms were written as textbook
// O(N^2) DFTs on the live dictation path. At a 512-sample frame that is ~262k
// complex operations per transform, twice per frame, continuously. This header
// provides an iterative radix-2 Cooley-Tukey FFT (O(N log N)) that returns the
// same spectrum; only the speed changes. No third-party dependency is needed.
//
// For inputs whose length is a power of two (the live path always uses 512) the
// radix-2 routine runs. For any other length a direct DFT is used so the result
// is identical to the original code for every possible input size.
namespace dictationpro {

inline bool isPowerOfTwo(std::size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

// In-place iterative radix-2 FFT. n MUST be a power of two.
// inverse == false -> forward (exp(-i...)); inverse == true -> inverse
// (exp(+i...), without the 1/n scaling, applied by the caller).
inline void fftRadix2(std::vector<std::complex<float>>& a, bool inverse)
{
    const std::size_t n = a.size();
    if (n <= 1)
        return;

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }

    const float kPi = 3.14159265358979323846f;
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float ang = (inverse ? 2.0f : -2.0f) * kPi / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Direct DFT fallback for non-power-of-two sizes (identical formula to the
// original naive transform, so behaviour is preserved for any length).
inline std::vector<std::complex<float>> dftDirect(const std::vector<float>& input)
{
    const int N = static_cast<int>(input.size());
    std::vector<std::complex<float>> output(static_cast<std::size_t>(N));
    const float kPi = 3.14159265358979323846f;
    for (int k = 0; k < N; ++k) {
        std::complex<float> acc(0.0f, 0.0f);
        for (int nn = 0; nn < N; ++nn) {
            const float angle = -2.0f * kPi * static_cast<float>(k) * static_cast<float>(nn)
                / static_cast<float>(N);
            acc += input[static_cast<std::size_t>(nn)]
                * std::complex<float>(std::cos(angle), std::sin(angle));
        }
        output[static_cast<std::size_t>(k)] = acc;
    }
    return output;
}

inline std::vector<float> idftDirect(const std::vector<std::complex<float>>& input)
{
    const int N = static_cast<int>(input.size());
    std::vector<float> output(static_cast<std::size_t>(N));
    const float kPi = 3.14159265358979323846f;
    for (int nn = 0; nn < N; ++nn) {
        std::complex<float> sum(0.0f, 0.0f);
        for (int k = 0; k < N; ++k) {
            const float angle = 2.0f * kPi * static_cast<float>(k) * static_cast<float>(nn)
                / static_cast<float>(N);
            sum += input[static_cast<std::size_t>(k)]
                * std::complex<float>(std::cos(angle), std::sin(angle));
        }
        output[static_cast<std::size_t>(nn)] = sum.real() / static_cast<float>(N);
    }
    return output;
}

// Forward real -> complex transform.
inline std::vector<std::complex<float>> fftForward(const std::vector<float>& input)
{
    const std::size_t N = input.size();
    if (!isPowerOfTwo(N))
        return dftDirect(input);

    std::vector<std::complex<float>> a(N);
    for (std::size_t i = 0; i < N; ++i)
        a[i] = std::complex<float>(input[i], 0.0f);
    fftRadix2(a, /*inverse=*/false);
    return a;
}

// Inverse complex -> real transform; returns the real parts scaled by 1/N,
// matching the original computeIFFT contract.
inline std::vector<float> fftInverseReal(const std::vector<std::complex<float>>& input)
{
    const std::size_t N = input.size();
    if (!isPowerOfTwo(N))
        return idftDirect(input);

    std::vector<std::complex<float>> a = input;
    fftRadix2(a, /*inverse=*/true);
    std::vector<float> output(N);
    for (std::size_t i = 0; i < N; ++i)
        output[i] = a[i].real() / static_cast<float>(N);
    return output;
}

} // namespace dictationpro
