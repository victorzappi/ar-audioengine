/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// timing.h
#ifndef TIMING_H
#define TIMING_H

// per-block timing of the MMAP loop: a test tool, compiled in only with the TIMING
// CMake option (AR_TIMING). normal builds contain none of it.
//
// for each DSP position update, the audio thread records when it saw the update and
// the DSP's own timestamp of it; after writing a playback block, when it finished.
// results, in us:
// - update interval: between two updates, as the thread saw them
// - DSP interval: between the DSP's timestamps of two updates
// - detection jitter: update interval minus DSP interval, i.e. how much the delay
//   from a DSP update to the thread seeing it changed from one update to the next
// - detection delay: the CPU's arch timer counter (the SoC's 19.2 MHz counter) when
//   the thread saw an update, minus the DSP's timestamp of it. if the DSP stamps its
//   updates with the same counter, this is the delay from a DSP update to the thread
//   seeing it. the first update is skipped: it may predate the polling
// - work time: from seeing an update to having written the next block (render() and
//   conversion)
// - render: render() alone
// - conversion: converting the rendered block into its slot in the ring alone
// - slack: from having written a block to seeing the update in which the DSP took
//   it, i.e. the time to spare. a block the DSP took before it was fully written
//   counts as a late write instead
// intervals and jitter only use updates seen one block apart; updates the thread
// missed in between are counted as missed.
// while the thread detects an update, renders and writes a block, the tool only
// reads clocks and stores them; it computes and records the results once the block
// is written, so that its own memory accesses stay out of the measured times

#include <stdint.h>
#include <time.h>

#define TIMING_HIST_BINS 4000                    // 1 us bins
#define TIMING_HIST_FILE_FMT "timing_hist_%s.txt"  // written in the current directory
#define TIMING_MAX_PENDING 8                     // updates stored between two blocks

// a DSP position update as the thread saw it, until it is recorded
struct timing_update {
    int64_t seen_ns;          // CLOCK_MONOTONIC when the thread saw it
    uint64_t counter_ticks;   // arch timer counter at the same moment
    uint64_t dsp_us;          // the DSP's timestamp of it
    uint32_t counter;         // DSP frame_counter
    uint32_t blocks;          // DSP blocks after the previous update
};

struct timing_stat {
    int64_t min_ns, max_ns, sum_ns;
    uint64_t count;
    int hist_offset_us;  // value of bin 0
    uint32_t hist[TIMING_HIST_BINS];
};

struct block_timing {
    struct timing_stat update_interval, dsp_interval, detect_jitter, detect_delay, work,
        render, convert, slack;
    uint64_t missed_updates;
    uint64_t late_writes;
    uint64_t dropped_updates;  // seen beyond TIMING_MAX_PENDING, not recorded
    uint64_t counter_hz;       // arch timer frequency
    // updates seen and not recorded yet
    struct timing_update pending[TIMING_MAX_PENDING];
    unsigned int pending_count;
    // latest update seen
    int64_t seen_ns;           // 0 before the first update
    uint32_t seen_counter;
    // latest update recorded
    int64_t prev_ns;           // 0 before the first update
    uint64_t prev_dsp_us;
    // last block written, until the DSP takes it
    int64_t written_ns;        // 0 if none
    uint32_t taken_counter;    // DSP frame_counter of the update that takes it
};

// CPU time for the measurements. CLOCK_MONOTONIC is read through the vDSO, with no
// system call, so it does not switch an EVL thread to in-band
static inline int64_t timing_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

// reset t. call before the streams start
void timing_init(struct block_timing *t);

// the thread saw a DSP position update at now_ns: frame_counter counter, blocks DSP
// blocks after the previous update, DSP timestamp dsp_us. only stores it
void timing_update_seen(struct block_timing *t, int64_t now_ns, uint32_t counter,
                        uint32_t blocks, uint64_t dsp_us);

// the thread finished writing a playback block at now_ns. counter_now is the DSP's
// frame_counter read right after; the DSP takes the block updates_ahead updates
// after the last one seen. also records the updates stored so far
void timing_write_done(struct block_timing *t, int64_t now_ns, uint32_t counter_now,
                       uint32_t updates_ahead);

// record the updates stored so far. for a stream that writes no blocks (capture),
// call it after the playback block is written
void timing_flush(struct block_timing *t);

// how long the block just written took to render and to convert into the ring
void timing_steps(struct block_timing *t, int64_t render_ns, int64_t convert_ns);

// print the results and write the histograms to TIMING_HIST_FILE_FMT (name)
void timing_report(const struct block_timing *t, const char *name);

#endif // TIMING_H
