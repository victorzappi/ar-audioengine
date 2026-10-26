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
// - work time: from seeing an update to having written the next block (render() and
//   conversion)
// - slack: from having written a block to seeing the update in which the DSP took
//   it, i.e. the time to spare. a block the DSP took before it was fully written
//   counts as a late write instead
// intervals and jitter only use updates seen one block apart; updates the thread
// missed in between are counted as missed

#include <stdint.h>
#include <time.h>

#define TIMING_HIST_BINS 4000                    // 1 us bins
#define TIMING_HIST_FILE_FMT "timing_hist_%s.txt"  // written in the current directory

struct timing_stat {
    int64_t min_ns, max_ns, sum_ns;
    uint64_t count;
    int hist_offset_us;  // value of bin 0
    uint32_t hist[TIMING_HIST_BINS];
};

struct block_timing {
    struct timing_stat update_interval, dsp_interval, detect_jitter, work, slack;
    uint64_t missed_updates;
    uint64_t late_writes;
    // last update seen
    int64_t seen_ns;         // 0 before the first update
    uint64_t seen_dsp_us;
    uint32_t seen_counter;
    // last block written, until the DSP takes it
    int64_t written_ns;      // 0 if none
    uint32_t taken_counter;  // DSP frame_counter of the update that takes it
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
// blocks after the previous update, DSP timestamp dsp_us
void timing_update_seen(struct block_timing *t, int64_t now_ns, uint32_t counter,
                        uint32_t blocks, uint64_t dsp_us);

// the thread finished writing a playback block at now_ns. counter_now is the DSP's
// frame_counter read right after; the DSP takes the block updates_ahead updates
// after the last one seen
void timing_write_done(struct block_timing *t, int64_t now_ns, uint32_t counter_now,
                       uint32_t updates_ahead);

// print the results and write the histograms to TIMING_HIST_FILE_FMT (name)
void timing_report(const struct block_timing *t, const char *name);

#endif // TIMING_H
