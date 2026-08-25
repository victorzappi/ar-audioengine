/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// CPU affinity for the audio thread, and best-effort forcing/restoring of
// the "performance" cpufreq governor. Both are opt-in (see cpu_perf.h).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE   // pthread_attr_setaffinity_np, CPU_ZERO/CPU_SET are GNU extensions
#endif

#include "cpu_perf.h"
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CPU_ONLINE_PATH_FMT   "/sys/devices/system/cpu/cpu%d/online"
#define CPU_GOVERNOR_PATH_FMT "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor"
#define GOVERNOR_PERFORMANCE  "performance"

static bool g_verbose = false;

void set_cpu_perf_verbose(bool verbose)
{
    g_verbose = verbose;
}

// ---------------------------------------------------------------------------
// CPU enumeration
// ---------------------------------------------------------------------------

// a cpuN with no "online" file is always online (true for CPUs the kernel
// can't hot-unplug, e.g. cpu0).
static bool cpu_is_online(int cpu)
{
    char path[64];
    snprintf(path, sizeof(path), CPU_ONLINE_PATH_FMT, cpu);

    FILE *f = fopen(path, "r");
    if (!f)
        return true;

    char c = '0';
    int scanned = fscanf(f, " %c", &c);
    fclose(f);

    return scanned == 1 && c == '1';
}

// enumerate online CPUs into ids[], up to max entries. returns the count found.
static int enumerate_online_cpus(int *ids, int max)
{
    long configured = sysconf(_SC_NPROCESSORS_CONF);
    if (configured <= 0 || configured > max)
        configured = max;

    int count = 0;
    for (int cpu = 0; cpu < configured; cpu++) {
        if (cpu_is_online(cpu))
            ids[count++] = cpu;
    }
    return count;
}

// ---------------------------------------------------------------------------
// CPU affinity
// ---------------------------------------------------------------------------

int validate_cpu_affinity(int cpu_index)
{
    if (cpu_index == CPU_AFFINITY_UNSET)
        return 0;

    int ids[MAX_TRACKED_CPUS];
    int count = enumerate_online_cpus(ids, MAX_TRACKED_CPUS);

    for (int i = 0; i < count; i++) {
        if (ids[i] == cpu_index)
            return 0;
    }

    fprintf(stderr, "cpu affinity index %d is not an online CPU. online CPUs are:", cpu_index);
    for (int i = 0; i < count; i++)
        fprintf(stderr, " %d", ids[i]);
    fprintf(stderr, "\n");
    return -1;
}

int set_cpu_affinity_attr(pthread_attr_t *attr, int cpu_index)
{
    if (cpu_index == CPU_AFFINITY_UNSET)
        return 0;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_index, &cpuset);

    int ret = pthread_attr_setaffinity_np(attr, sizeof(cpuset), &cpuset);
    if (ret != 0) {
        fprintf(stderr, "warning: failed to set cpu affinity to CPU %d (%s); thread will not be pinned\n",
                cpu_index, strerror(ret));
        return -1;
    }

    if (g_verbose)
        printf("Pinning audio thread to CPU %d\n", cpu_index);
    return 0;
}

// ---------------------------------------------------------------------------
// performance governor
// ---------------------------------------------------------------------------

static int read_governor(int cpu, char *out, size_t out_size)
{
    char path[80];
    snprintf(path, sizeof(path), CPU_GOVERNOR_PATH_FMT, cpu);

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    bool ok = (fgets(out, (int)out_size, f) != nullptr);
    fclose(f);
    if (!ok)
        return -1;

    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n')
        out[len - 1] = '\0';

    return 0;
}

static int write_governor(int cpu, const char *value)
{
    char path[80];
    snprintf(path, sizeof(path), CPU_GOVERNOR_PATH_FMT, cpu);

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    int ret = (fputs(value, f) >= 0) ? 0 : -1;
    fclose(f);
    return ret;
}

int force_performance_governor(struct governor_snapshot *snap)
{
    snap->count = 0;

    int ids[MAX_TRACKED_CPUS];
    int num_cpus = enumerate_online_cpus(ids, MAX_TRACKED_CPUS);

    for (int i = 0; i < num_cpus; i++) {
        int cpu = ids[i];
        int idx = snap->count++;

        snap->cpu_id[idx] = cpu;
        snap->changed[idx] = false;
        snap->governor[idx][0] = '\0';

        if (read_governor(cpu, snap->governor[idx], sizeof(snap->governor[idx])) != 0) {
            fprintf(stderr, "warning: could not read scaling_governor for CPU %d (no cpufreq driver?); skipping\n", cpu);
            continue;
        }

        if (strcmp(snap->governor[idx], GOVERNOR_PERFORMANCE) == 0)
            continue; // already performance, nothing to force or restore

        if (write_governor(cpu, GOVERNOR_PERFORMANCE) != 0) {
            fprintf(stderr, "warning: could not set CPU %d governor to performance (permission denied?); leaving as '%s'\n",
                    cpu, snap->governor[idx]);
            continue;
        }

        if (g_verbose)
            printf("Forcing performance governor on CPU %d (was: %s)\n", cpu, snap->governor[idx]);
        snap->changed[idx] = true;
    }

    return 0;
}

void restore_governors(const struct governor_snapshot *snap)
{
    for (int i = 0; i < snap->count; i++) {
        if (!snap->changed[i])
            continue;

        if (write_governor(snap->cpu_id[i], snap->governor[i]) != 0)
            fprintf(stderr, "warning: could not restore CPU %d governor to '%s'\n", snap->cpu_id[i], snap->governor[i]);
        else if (g_verbose)
            printf("Restoring CPU %d governor to %s\n", snap->cpu_id[i], snap->governor[i]);
    }
}
