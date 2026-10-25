/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// EVL (Xenomai 4) route for the audio thread (see evl_rt.h).

#include "evl_rt.h"
#include <stdio.h>

#ifdef AR_EVL

#include <dlfcn.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <evl/evl.h>

// soname of the libevl the headers come from (libevl r61)
#define LIBEVL_SONAME "libevl.so.9"

// libevl entry points, resolved by evl_rt_init()
static struct {
    decltype(&evl_init) init;
    decltype(&evl_attach_thread) attach_thread;
    decltype(&evl_set_schedattr) set_schedattr;
    decltype(&evl_usleep) usleep;
    decltype(&evl_get_state) get_state;
    decltype(&evl_detach_self) detach_self;
} libevl;

static int thread_efd = -1;

int evl_rt_init(void)
{
    void *handle = dlopen(LIBEVL_SONAME, RTLD_NOW);
    if (handle == nullptr) {
        fprintf(stderr, "EVL not available: %s\n", dlerror());
        return -1;
    }

    libevl.init = (decltype(libevl.init))dlsym(handle, "evl_init");
    libevl.attach_thread = (decltype(libevl.attach_thread))dlsym(handle, "evl_attach_thread");
    libevl.set_schedattr = (decltype(libevl.set_schedattr))dlsym(handle, "evl_set_schedattr");
    libevl.usleep = (decltype(libevl.usleep))dlsym(handle, "evl_usleep");
    libevl.get_state = (decltype(libevl.get_state))dlsym(handle, "evl_get_state");
    libevl.detach_self = (decltype(libevl.detach_self))dlsym(handle, "evl_detach_self");
    if (!libevl.init || !libevl.attach_thread || !libevl.set_schedattr || !libevl.usleep ||
        !libevl.get_state || !libevl.detach_self) {
        fprintf(stderr, "EVL not available: %s lacks some functions\n", LIBEVL_SONAME);
        dlclose(handle);
        return -1;
    }

    // fails if the kernel has no EVL core. on success, libevl has also locked the
    // process memory, so the audio thread takes no page faults out-of-band
    int ret = libevl.init();
    if (ret < 0) {
        fprintf(stderr, "EVL not available: evl_init() failed: %s\n", strerror(-ret));
        dlclose(handle);
        return -1;
    }

    return 0;
}

int evl_rt_attach(void)
{
    int efd = libevl.attach_thread(EVL_CLONE_PUBLIC, "ar_audioengine-%d", getpid());
    if (efd < 0) {
        fprintf(stderr, "failed to attach the audio thread to EVL: %s\n", strerror(-efd));
        return -1;
    }

    // EVL's highest SCHED_FIFO priority for user threads is the same as Linux's
    // (EVL_FIFO_MAX_PRIO = MAX_RT_PRIO - 1); only EVL's own kernel threads go higher
    struct evl_sched_attrs attrs = {};
    attrs.sched_policy = SCHED_FIFO;
    attrs.sched_priority = sched_get_priority_max(SCHED_FIFO);
    int ret = libevl.set_schedattr(efd, &attrs);
    if (ret < 0) {
        fprintf(stderr, "failed to set the EVL priority of the audio thread: %s\n", strerror(-ret));
        libevl.detach_self();
        return -1;
    }

    thread_efd = efd;
    return 0;
}

void evl_rt_usleep(unsigned int usecs)
{
    libevl.usleep(usecs);
}

long evl_rt_inband_switches(void)
{
    struct evl_thread_state state;

    if (thread_efd < 0 || libevl.get_state(thread_efd, &state) < 0)
        return -1;
    return state.isw;
}

void evl_rt_detach(void)
{
    if (thread_efd < 0)
        return;
    libevl.detach_self();
    thread_efd = -1;
}

#else // !AR_EVL

int evl_rt_init(void)
{
    fprintf(stderr, "EVL not available: this build has no EVL route (configure with -DEVL=ON)\n");
    return -1;
}

int evl_rt_attach(void)
{
    return -1;
}

void evl_rt_usleep(unsigned int usecs)
{
    (void)usecs;
}

long evl_rt_inband_switches(void)
{
    return -1;
}

void evl_rt_detach(void)
{
}

#endif // AR_EVL
