/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// cpu_buffer.h
#ifndef CPU_BUFFER_H
#define CPU_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include "agm_mixer.h"

// CPU-buffer route (--cpu-buffer): the engine reads the capture Codec DMA's buffer and
// writes the playback Codec DMA's buffer itself, in LPASS on-chip memory, and follows
// each DMA through its current-address register (CURR_ADDR). The DSP does nothing per
// block (custom Codec DMA firmware, PARAM_ID_CODEC_DMA_CPU_BUFFER). The buffers and
// registers are mapped through the ar_lpass_map kernel module (kernel/ar_lpass_map).
//
// Buffer layout: num_periods periods (2: a double buffer) of interleaved samples, one
// 32-bit word per sample, MSB-aligned (the codec takes the upper 16 or 24 bits).

struct cpu_buffer_dma {
    volatile uint32_t *buf;          // the DMA buffer, all periods
    volatile const uint32_t *curr;   // the DMA's CURR_ADDR register: next address it reads/writes
    uint32_t buf_addr;               // physical address of buf
    unsigned int num_periods;
    unsigned int period_frames;
    unsigned int channels;
    unsigned int buffer_frames;
    unsigned int watermark;          // DMA FIFO watermark, 32-bit words
    void *buf_map;                   // the mappings, for cpu_buffer_unmap
    size_t buf_map_len;
    void *regs_map;
    size_t regs_map_len;
};

// map the buffer (writable for playback) and the registers of a running endpoint's DMA,
// as get_agm_hw_ep_cpu_buffer() reported them. checks the layout against the stream:
// period_frames frames of channels samples. returns 0, or -1 (error printed)
int cpu_buffer_map(struct cpu_buffer_dma *dma, const struct hw_ep_cpu_buffer *info, bool playback,
                   unsigned int period_frames, unsigned int channels);
void cpu_buffer_unmap(struct cpu_buffer_dma *dma);

// the period the DMA reads (playback) or writes (capture) now
static inline unsigned int cpu_buffer_period(const struct cpu_buffer_dma *dma)
{
    uint32_t frame = (*dma->curr - dma->buf_addr) / (4 * dma->channels);
    return (frame % dma->buffer_frames) / dma->period_frames;
}

// order the buffer reads after the position read that found the period complete, and
// the buffer writes before the position read that checks they were in time
static inline void cpu_buffer_read_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dmb oshld" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

static inline void cpu_buffer_write_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb st" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

// capture period -> normalized float samples (num_samples = period_frames * channels)
void cpu_buffer_read(const struct cpu_buffer_dma *dma, unsigned int period, float *out);

// float samples -> playback period, clipped to [-1, 1]
void cpu_buffer_write(struct cpu_buffer_dma *dma, unsigned int period, const float *in);

#endif // CPU_BUFFER_H
