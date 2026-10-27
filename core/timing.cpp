/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// per-block timing of the MMAP loop (see timing.h). only built with -DTIMING=ON.

#include "timing.h"
#include <stdio.h>
#include <string.h>

// histogram ranges, in us: intervals, work time and slack 0 .. 3999,
// detection jitter and detection delay -2000 .. 1999
#define JITTER_HIST_OFFSET_US (-TIMING_HIST_BINS / 2)

// the arm64 virtual counter, in ns: the CPU's view of the SoC's always-on counter,
// read with no system call
static inline int64_t counter_now_ns(void)
{
#if defined(__aarch64__)
    uint64_t cnt, frq;
    __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(cnt) : : "memory");
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    return (int64_t)((unsigned __int128)cnt * 1000000000u / frq);
#else
    return 0;
#endif
}

void timing_init(struct block_timing *t)
{
    memset(t, 0, sizeof(*t));
    t->detect_jitter.hist_offset_us = JITTER_HIST_OFFSET_US;
    t->detect_delay.hist_offset_us = JITTER_HIST_OFFSET_US;
}

static void record(struct timing_stat *s, int64_t value_ns)
{
    if (s->count == 0 || value_ns < s->min_ns)
        s->min_ns = value_ns;
    if (s->count == 0 || value_ns > s->max_ns)
        s->max_ns = value_ns;
    s->sum_ns += value_ns;
    s->count++;

    // floor to whole us, then clamp into the histogram
    int64_t us = value_ns >= 0 ? value_ns / 1000 : -((-value_ns + 999) / 1000);
    int64_t bin = us - s->hist_offset_us;
    if (bin < 0)
        bin = 0;
    else if (bin >= TIMING_HIST_BINS)
        bin = TIMING_HIST_BINS - 1;
    s->hist[bin]++;
}

void timing_update_seen(struct block_timing *t, int64_t now_ns, uint32_t counter,
                        uint32_t blocks, uint64_t dsp_us)
{
    record(&t->detect_delay, counter_now_ns() - (int64_t)dsp_us * 1000);

    if (t->seen_ns != 0) {
        if (blocks == 1) {
            int64_t cpu_ns = now_ns - t->seen_ns;
            int64_t dsp_ns = (int64_t)(dsp_us - t->seen_dsp_us) * 1000;
            record(&t->update_interval, cpu_ns);
            record(&t->dsp_interval, dsp_ns);
            record(&t->detect_jitter, cpu_ns - dsp_ns);
        } else {
            t->missed_updates += blocks - 1;
        }
    }

    if (t->written_ns != 0 && (int32_t)(counter - t->taken_counter) >= 0) {
        record(&t->slack, now_ns - t->written_ns);
        t->written_ns = 0;
    }

    t->seen_ns = now_ns;
    t->seen_dsp_us = dsp_us;
    t->seen_counter = counter;
}

void timing_write_done(struct block_timing *t, int64_t now_ns, uint32_t counter_now,
                       uint32_t updates_ahead)
{
    if (t->seen_ns == 0)
        return;
    record(&t->work, now_ns - t->seen_ns);

    // the DSP took the block before it was fully written if the update that takes
    // it has already happened. frame_counter is 0 while the DSP writes an update,
    // which is the one that takes the block if the block is just one update ahead
    uint32_t taken = t->seen_counter + updates_ahead;
    bool late = counter_now == 0 ? updates_ahead <= 1 : (int32_t)(counter_now - taken) >= 0;
    if (late) {
        t->late_writes++;
        t->written_ns = 0;
    } else {
        t->written_ns = now_ns;
        t->taken_counter = taken;
    }
}

static void print_stat(const char *label, const struct timing_stat *s)
{
    if (s->count == 0) {
        printf("  %-18s %9s\n", label, "-");
        return;
    }
    printf("  %-18s %9.3f %9.3f %9.3f %10llu\n", label, s->min_ns / 1e3,
           (double)s->sum_ns / s->count / 1e3, s->max_ns / 1e3, (unsigned long long)s->count);
}

static void write_hist(FILE *f, const char *label, const struct timing_stat *s)
{
    fprintf(f, "# %s: us count (the first and last bins also hold values out of range)\n", label);
    for (int i = 0; i < TIMING_HIST_BINS; i++)
        if (s->hist[i])
            fprintf(f, "%d %u\n", i + s->hist_offset_us, s->hist[i]);
    fprintf(f, "\n\n");  // gnuplot block separator
}

void timing_report(const struct block_timing *t, const char *name)
{
    printf("timing (%s), us:      min       avg       max      count\n", name);
    print_stat("update interval", &t->update_interval);
    print_stat("DSP interval", &t->dsp_interval);
    print_stat("detection jitter", &t->detect_jitter);
    print_stat("detection delay", &t->detect_delay);
    print_stat("work time", &t->work);
    print_stat("slack", &t->slack);
    printf("  missed updates: %llu, late writes: %llu\n",
           (unsigned long long)t->missed_updates, (unsigned long long)t->late_writes);

    char path[64];
    snprintf(path, sizeof(path), TIMING_HIST_FILE_FMT, name);
    FILE *f = fopen(path, "w");
    if (f == nullptr) {
        fprintf(stderr, "timing: could not write %s\n", path);
        return;
    }
    write_hist(f, "update interval", &t->update_interval);
    write_hist(f, "DSP interval", &t->dsp_interval);
    write_hist(f, "detection jitter", &t->detect_jitter);
    write_hist(f, "detection delay", &t->detect_delay);
    write_hist(f, "work time", &t->work);
    write_hist(f, "slack", &t->slack);
    fclose(f);
    printf("  histograms written to %s\n", path);
}
