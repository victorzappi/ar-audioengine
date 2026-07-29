/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <vector>

/**
 * Direct-form FIR filter, float32, multi-channel with shared coefficients.
 *
 * Mirrors the structure of the AudioReach FIR module: one coefficient set is
 * applied to every channel, and each channel keeps its own history buffer.
 *
 * Buffers are de-interleaved -- process() takes an array of per-channel
 * pointers and filters in place.
 */
class Fir
{
public:
	Fir() = default;
	Fir(unsigned int numTaps, unsigned int numChannels);
	~Fir() = default;

	/** Allocates coefficient and history storage. Returns 0 on success. */
	int setup(unsigned int numTaps, unsigned int numChannels);

	/** Copies n coefficients. n must equal numTaps. Returns 0 on success. */
	int setCoefficients(const float *h, unsigned int n);

	/** In-place, de-interleaved. in[c] holds `frames` samples for channel c. */
	void process(float *const *in, unsigned int frames);

	/** Clears the history buffers. */
	void reset();

	unsigned int getNumTaps() const { return numTaps_; }
	unsigned int getNumChannels() const { return numChannels_; }

private:
	unsigned int numTaps_ = 0;
	unsigned int numChannels_ = 0;
	unsigned int pos_ = 0;

	// Coefficients are stored time-reversed, so the inner loop reads both
	// coefficients and history in ascending order.
	std::vector<float> coeffs_;

	// 2 * numTaps per channel. Every input sample is written at both pos and
	// pos + numTaps, so any numTaps-long window is contiguous: no modulo in
	// the inner loop, which is what lets the compiler vectorise it.
	std::vector<float> history_;
	std::vector<float *> hist_;
};
