/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "Fir.h"

#include <algorithm>
#include <cstring>

Fir::Fir(unsigned int numTaps, unsigned int numChannels, unsigned int maxFrames)
{
	setup(numTaps, numChannels, maxFrames);
}

int Fir::setup(unsigned int numTaps, unsigned int numChannels,
               unsigned int maxFrames)
{
	if(numTaps == 0 || numChannels == 0 || maxFrames == 0)
		return -1;

	numTaps_ = numTaps;
	numChannels_ = numChannels;
	maxFrames_ = maxFrames;

	const size_t stride = (size_t)(numTaps_ - 1) + maxFrames_;

	coeffs_.assign(numTaps_, 0.0f);
	history_.assign((size_t)numChannels_ * stride, 0.0f);

	hist_.resize(numChannels_);
	for(unsigned int c = 0; c < numChannels_; ++c)
		hist_[c] = history_.data() + (size_t)c * stride;

	return 0;
}

int Fir::setCoefficients(const float *h, unsigned int n)
{
	if(h == nullptr || n != numTaps_)
		return -1;

	// y[n] = sum_k h[k] * x[n-k]. Reversing h turns that into a forward dot
	// product against the history window.
	for(unsigned int k = 0; k < numTaps_; ++k)
		coeffs_[k] = h[numTaps_ - 1 - k];

	return 0;
}

void Fir::reset()
{
	std::fill(history_.begin(), history_.end(), 0.0f);
}

void Fir::process(float *const *in, unsigned int frames)
{
	const unsigned int N = numTaps_;
	const unsigned int nch = numChannels_;
	const float *const hrev = coeffs_.data();

	if(N == 0 || nch == 0 || frames == 0 || frames > maxFrames_)
		return;

	const unsigned int tail = N - 1;

	for(unsigned int c = 0; c < nch; ++c)
	{
		float *x = in[c];
		float *h = hist_[c];

		// Append the incoming block after the retained tail.
		memcpy(h + tail, x, (size_t)frames * sizeof(float));

		unsigned int nb = 0;

		// Hot path: fixed-size output blocks, accumulators live in registers
		// across the whole tap loop.
		for(; nb + kOutBlock <= frames; nb += kOutBlock)
		{
			float acc[kOutBlock];
			for(unsigned int m = 0; m < kOutBlock; ++m)
				acc[m] = 0.0f;

			const float *base = h + nb;

			for(unsigned int j = 0; j < N; ++j)
			{
				const float cf = hrev[j];
				const float *w = base + j;

				for(unsigned int m = 0; m < kOutBlock; ++m)
					acc[m] += cf * w[m];
			}

			memcpy(x + nb, acc, kOutBlock * sizeof(float));
		}

		// Remainder, when frames is not a multiple of kOutBlock.
		for(; nb < frames; ++nb)
		{
			const float *w = h + nb;

			float a = 0.0f;
			for(unsigned int j = 0; j < N; ++j)
				a += hrev[j] * w[j];

			x[nb] = a;
		}

		// Retain the trailing numTaps-1 samples for the next block.
		memmove(h, h + frames, (size_t)tail * sizeof(float));
	}
}
