/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <vector>

/**
 * Direct-form FIR filter, float32, multi-channel with shared coefficients.
 *
 * One coefficient set is applied to every channel; each channel keeps its own
 * history. Buffers are de-interleaved -- process() takes an array of
 * per-channel pointers and filters in place.
 *
 * Structure notes, which matter at long tap counts:
 *
 * The naive form (one output sample at a time, inner loop over taps) re-reads
 * the entire coefficient array and history window for every output sample. At
 * 32k taps that is hundreds of KB per sample and tens of GB/s of memory
 * traffic -- far past what the memory system can supply, regardless of how
 * cheap the multiply-accumulates are.
 *
 * So the loops are inverted: taps outer, output frames inner, with the output
 * block accumulated in registers. Each coefficient is loaded once per block of
 * kOutBlock outputs, and the history window is walked sequentially.
 *
 * History layout, per channel, of size (numTaps - 1) + maxFrames:
 *
 *   [ 0 .. numTaps-2 ]                 retained tail from previous blocks
 *   [ numTaps-1 .. numTaps-2+frames ]  the incoming block
 *
 * With coefficients stored time-reversed, output n is the dot product of the
 * coefficients against history[n .. n+numTaps-1] -- both walked forwards, unit
 * stride, no modulo, which is what allows the inner loop to vectorise.
 */
class Fir
{
public:
	Fir() = default;
	Fir(unsigned int numTaps, unsigned int numChannels, unsigned int maxFrames);
	~Fir() = default;

	/**
	 * Allocates coefficient and history storage.
	 *
	 * @param maxFrames largest block that will ever be passed to process();
	 *                  pass the audio period size.
	 * @return 0 on success.
	 */
	int setup(unsigned int numTaps, unsigned int numChannels,
	          unsigned int maxFrames);

	/** Copies n coefficients. n must equal numTaps. Returns 0 on success. */
	int setCoefficients(const float *h, unsigned int n);

	/**
	 * In-place, de-interleaved. in[c] holds `frames` samples for channel c.
	 * `frames` must not exceed the maxFrames given to setup().
	 */
	void process(float *const *in, unsigned int frames);

	/** Clears the history buffers. */
	void reset();

	unsigned int getNumTaps() const { return numTaps_; }
	unsigned int getNumChannels() const { return numChannels_; }
	unsigned int getMaxFrames() const { return maxFrames_; }

private:
	// Outputs accumulated per pass over the coefficients. Larger values cut
	// coefficient re-reads proportionally, at the cost of vector registers
	// held live across the tap loop. 32 outputs is 8 128-bit accumulators.
	static const unsigned int kOutBlock = 32;

	unsigned int numTaps_ = 0;
	unsigned int numChannels_ = 0;
	unsigned int maxFrames_ = 0;

	std::vector<float> coeffs_;      // time-reversed
	std::vector<float> history_;
	std::vector<float *> hist_;      // per-channel views into history_
};
