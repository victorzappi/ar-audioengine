/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// cpu_perf.h
#ifndef CPU_PERF_H
#define CPU_PERF_H

#include <pthread.h>

#define CPU_AFFINITY_UNSET (-1)   // sentinel: no affinity requested (default)
#define MAX_TRACKED_CPUS 16       // headroom above QCS6490's 8 cores

// snapshot of each online CPU's pre-existing scaling_governor, so it can be
// restored later. changed[i] is true only for CPUs actually forced to
// "performance" (readable AND writable); restore_governors only touches those.
struct governor_snapshot {
    int  cpu_id[MAX_TRACKED_CPUS];
    char governor[MAX_TRACKED_CPUS][32];
    bool changed[MAX_TRACKED_CPUS];
    int  count;
};

// validate a CLI-supplied CPU index against currently-online CPUs.
// cpu_index == CPU_AFFINITY_UNSET always passes (affinity disabled).
// returns 0 if valid, -1 if out of range (prints the online CPU list to stderr).
int validate_cpu_affinity(int cpu_index);

// pin *attr to run on cpu_index, to be passed to pthread_create (attr-based,
// so the thread is pinned from its very first instruction). no-op (returns 0)
// if cpu_index == CPU_AFFINITY_UNSET. best-effort: if the underlying
// pthread_attr_setaffinity_np() call fails, warns and leaves *attr unpinned
// rather than propagating a hard error (an affinity problem must never block
// audio from starting).
int set_cpu_affinity_attr(pthread_attr_t *attr, int cpu_index);

// snapshot every online CPU's current scaling_governor into *snap, then force
// each one whose scaling_governor is readable+writable to "performance".
// best-effort: a CPU with no cpufreq driver, or not writable (e.g. not root),
// is warned about and left untouched; snap->changed[i] records which CPUs
// actually need restoring. Always returns 0 -- there is no fatal-error path
// by design.
int force_performance_governor(struct governor_snapshot *snap);

// restore every governor recorded in *snap where changed[i] is true. safe to
// call on a zeroed/never-forced snapshot (count == 0 -> no-op). best-effort;
// warns, does not fail, on a restore write error.
void restore_governors(const struct governor_snapshot *snap);

#endif // CPU_PERF_H
