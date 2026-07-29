/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "Fir.h"

#include <algorithm>

Fir::Fir(unsigned int numTaps, unsigned int numChannels)
{
	setup(numTaps, numChannels);
}

int Fir::setup(unsigned int numTaps, unsigned int numChannels)
{
	if(numTaps == 0 || numChannels == 0)
		return -1;

	numTaps_ = numTaps;
	numChannels_ = numChannels;
	pos_ = 0;

	coeffs_.assign(numTaps_, 0.0f);
	history_.assign((size_t)numChannels_ * 2 * numTaps_, 0.0f);

	hist_.resize(numChannels_);
	for(unsigned int c = 0; c < numChannels_; ++c)
		hist_[c] = history_.data() + (size_t)c * 2 * numTaps_;

	return 0;
}

int Fir::setCoefficients(const float *h, unsigned int n)
{
	if(h == nullptr || n != numTaps_)
		return -1;

	// y[n] = sum_k h[k] * x[n-k]. Storing h reversed lets the inner loop walk
	// the history window forwards instead of backwards.
	for(unsigned int k = 0; k < numTaps_; ++k)
		coeffs_[k] = h[numTaps_ - 1 - k];

	return 0;
}

void Fir::reset()
{
	std::fill(history_.begin(), history_.end(), 0.0f);
	pos_ = 0;
}

void Fir::process(float *const *in, unsigned int frames)
{
	const unsigned int N = numTaps_;
	const unsigned int nch = numChannels_;
	const float *const hrev = coeffs_.data();

	if(N == 0 || nch == 0)
		return;

	if(nch == 2)
	{
		// Fused stereo path: taps in the outer loop, channels unrolled inside,
		// so each coefficient is loaded once and feeds both accumulators.
		float *x0 = in[0];
		float *x1 = in[1];
		float *h0 = hist_[0];
		float *h1 = hist_[1];
		unsigned int p = pos_;

		for(unsigned int n = 0; n < frames; ++n)
		{
			const float s0 = x0[n];
			const float s1 = x1[n];
			h0[p] = s0;
			h0[p + N] = s0;
			h1[p] = s1;
			h1[p + N] = s1;

			const float *w0 = h0 + p + 1;
			const float *w1 = h1 + p + 1;

			float a0 = 0.0f;
			float a1 = 0.0f;
			for(unsigned int k = 0; k < N; ++k)
			{
				const float c = hrev[k];
				a0 += c * w0[k];
				a1 += c * w1[k];
			}

			x0[n] = a0;
			x1[n] = a1;

			if(++p == N)
				p = 0;
		}

		pos_ = p;
		return;
	}

	// Generic path (mono, or more than two channels). Channel-outer, so the
	// coefficients are re-read once per channel.
	const unsigned int startPos = pos_;
	unsigned int p = startPos;

	for(unsigned int c = 0; c < nch; ++c)
	{
		float *x = in[c];
		float *hc = hist_[c];
		p = startPos;

		for(unsigned int n = 0; n < frames; ++n)
		{
			const float s = x[n];
			hc[p] = s;
			hc[p + N] = s;

			const float *w = hc + p + 1;

			float a = 0.0f;
			for(unsigned int k = 0; k < N; ++k)
				a += hrev[k] * w[k];

			x[n] = a;

			if(++p == N)
				p = 0;
		}
	}

	pos_ = p;
}
