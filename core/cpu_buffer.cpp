/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// CPU-buffer route: mapping of the Codec DMA buffers and registers, and the sample
// conversion to and from the DMA's format. See cpu_buffer.h.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cpu_buffer.h"

// the ar_lpass_map kernel module: the mmap() offset is the physical address
#define CPU_BUFFER_MEM_DEV  "/dev/ar_lpass_mem"
#define CPU_BUFFER_REGS_DEV "/dev/ar_lpass_regs"

// CURR_ADDR register of an LPAIF DMA channel (upstream Linux:
// sound/soc/qcom/lpass-lpaif-reg.h, LPAIF_*DMACURR_REG)
#define CPU_BUFFER_CURR_ADDR_OFFSET 0x0C

// maps len bytes at physical address pa from dev; *map/*map_len get the whole pages
static void *map_phys(const char *dev, uint32_t pa, size_t len, bool writable,
                      void **map, size_t *map_len)
{
    long page = sysconf(_SC_PAGESIZE);
    uint32_t base = pa & ~(uint32_t)(page - 1);
    size_t off = pa - base;
    size_t total = (off + len + page - 1) & ~(size_t)(page - 1);

    int fd = open(dev, writable ? O_RDWR : O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "CPU buffer: cannot open %s (%s); load the ar_lpass_map kernel module "
                        "(kernel/ar_lpass_map)\n", dev, strerror(errno));
        return nullptr;
    }
    void *p = mmap(nullptr, total, writable ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd, base);
    int err = errno;
    close(fd);
    if (p == MAP_FAILED) {
        fprintf(stderr, "CPU buffer: cannot map 0x%08x + %zu from %s (%s)\n", pa, len, dev, strerror(err));
        return nullptr;
    }
    *map = p;
    *map_len = total;
    return (char *)p + off;
}

int cpu_buffer_map(struct cpu_buffer_dma *dma, const struct hw_ep_cpu_buffer *info, bool playback,
                   unsigned int period_frames, unsigned int channels)
{
    const char *name = playback ? "playback" : "capture";

    memset(dma, 0, sizeof(*dma));
    if (!info->active) {
        fprintf(stderr, "CPU buffer: the %s endpoint is not in CPU-buffer mode\n", name);
        return -1;
    }
    if (info->bytes_per_sample != 4 || info->num_periods < 2 ||
        info->buffer_size != info->num_periods * info->period_size) {
        fprintf(stderr, "CPU buffer: %s DMA buffer layout not supported (%u bytes/sample, "
                        "%u periods of %u bytes, %u bytes)\n", name, info->bytes_per_sample,
                info->num_periods, info->period_size, info->buffer_size);
        return -1;
    }
    if (info->num_channels != channels) {
        fprintf(stderr, "CPU buffer: the %s DMA has %u channel(s), the stream %u: run with -%c %u\n",
                name, info->num_channels, channels, playback ? 'n' : 'N', info->num_channels);
        return -1;
    }
    if (info->period_size != period_frames * channels * 4) {
        fprintf(stderr, "CPU buffer: the %s DMA period is %u frames, the stream's %u\n", name,
                info->period_size / (channels * 4), period_frames);
        return -1;
    }

    dma->buf_addr = info->buffer_addr;
    dma->num_periods = info->num_periods;
    dma->period_frames = period_frames;
    dma->channels = channels;
    dma->buffer_frames = info->num_periods * period_frames;
    dma->watermark = info->watermark;

    dma->buf = (volatile uint32_t *)map_phys(CPU_BUFFER_MEM_DEV, info->buffer_addr, info->buffer_size,
                                             playback, &dma->buf_map, &dma->buf_map_len);
    if (dma->buf == nullptr)
        return -1;
    const char *regs = (const char *)map_phys(CPU_BUFFER_REGS_DEV, info->regs_addr,
                                              CPU_BUFFER_CURR_ADDR_OFFSET + 4, false,
                                              &dma->regs_map, &dma->regs_map_len);
    if (regs == nullptr) {
        cpu_buffer_unmap(dma);
        return -1;
    }
    dma->curr = (volatile const uint32_t *)(regs + CPU_BUFFER_CURR_ADDR_OFFSET);

    // the DMA must be inside its buffer: else the registers are not this DMA's
    uint32_t curr = *dma->curr;
    if (curr < info->buffer_addr || curr >= info->buffer_addr + info->buffer_size) {
        fprintf(stderr, "CPU buffer: the %s DMA is at 0x%08x, outside its buffer 0x%08x + %u\n",
                name, curr, info->buffer_addr, info->buffer_size);
        cpu_buffer_unmap(dma);
        return -1;
    }
    return 0;
}

void cpu_buffer_unmap(struct cpu_buffer_dma *dma)
{
    if (dma->buf_map)
        munmap(dma->buf_map, dma->buf_map_len);
    if (dma->regs_map)
        munmap(dma->regs_map, dma->regs_map_len);
    dma->buf_map = dma->regs_map = nullptr;
    dma->buf = nullptr;
    dma->curr = nullptr;
}

void cpu_buffer_read(const struct cpu_buffer_dma *dma, unsigned int period, float *out)
{
    const unsigned int n = dma->period_frames * dma->channels;
    const volatile uint32_t *in = dma->buf + period * n;

    for (unsigned int i = 0; i < n; i++)
        out[i] = (float)(int32_t)in[i] * (1.0f / 2147483648.0f);
}

void cpu_buffer_write(struct cpu_buffer_dma *dma, unsigned int period, const float *in)
{
    const unsigned int n = dma->period_frames * dma->channels;
    volatile uint32_t *out = dma->buf + period * n;

    // 24 bits, MSB-aligned: exact in float, and more than the codec takes
    for (unsigned int i = 0; i < n; i++) {
        float x = in[i] > 1.0f ? 1.0f : (in[i] < -1.0f ? -1.0f : in[i]);
        out[i] = (uint32_t)((int32_t)(x * 8388607.0f) * 256);
    }
}
