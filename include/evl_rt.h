/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// evl_rt.h
#ifndef EVL_RT_H
#define EVL_RT_H

// EVL (Xenomai 4) route for the audio thread, selected with --evl. compiled in with
// the EVL CMake option; libevl is loaded with dlopen() only when the route is used,
// so the same binary also runs on boards without EVL. without the CMake option,
// evl_rt_init() fails and the other functions do nothing.

// load libevl and check that the running kernel has the EVL core. call once,
// before the audio thread starts. returns 0, or -1 (error printed) if EVL is not
// available
int evl_rt_init(void);

// attach the calling thread to EVL, at the highest EVL SCHED_FIFO priority. the
// thread stays in-band until its first out-of-band call. returns 0, or -1 (error
// printed)
int evl_rt_attach(void);

// sleep out-of-band: the attached thread does not switch to in-band (Linux)
void evl_rt_usleep(unsigned int usecs);

// how many times the attached thread has switched to in-band so far, -1 on error.
// called out-of-band, the read itself does not switch the thread
long evl_rt_inband_switches(void);

// detach the calling thread from EVL
void evl_rt_detach(void);

#endif // EVL_RT_H
