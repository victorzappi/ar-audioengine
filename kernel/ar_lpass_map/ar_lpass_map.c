// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause-Clear
/*
 * Copyright 2026 Victor Zappi
 */

/*
 * ar_lpass_map: maps the Codec DMA buffers and DMA registers of the LPASS (audio
 * subsystem) into ar-audioengine, for its CPU-buffer route. There the
 * engine reads and writes the Codec DMAs' own double buffers, in LPASS on-chip memory,
 * and follows each DMA through its current-address register; the DSP does nothing per
 * block. The kernel has no /dev/mem on this platform, hence this module.
 *
 * Two devices, both root only. The mmap() offset is the physical address to map,
 * page-aligned:
 *   /dev/ar_lpass_mem   read/write, normal non-cacheable (write-combined) memory, only
 *                       inside the mem windows (the Codec DMA buffer memories)
 *   /dev/ar_lpass_regs  read only, device memory, only inside the regs window (the
 *                       LPASS register space)
 * The module itself never reads or writes the hardware.
 *
 * Defaults are for the QCS6490 (Kodiak): mem windows lpass-rxtx-cdc-dma-lpm and
 * lpass-va-cdc-dma-lpm, regs window the LPASS block. Other SoCs: set the parameters.
 *   insmod ar_lpass_map.ko [mem_base=a,b mem_size=a,b] [regs_base=a regs_size=a]
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>

#define AR_LPASS_MAX_MEM 4

static unsigned long mem_base[AR_LPASS_MAX_MEM] = { 0x03260000, 0x0336c000 };
static unsigned long mem_size[AR_LPASS_MAX_MEM] = { 0xc000, 0x3000 };
static int n_mem_base = 2, n_mem_size = 2;
module_param_array(mem_base, ulong, &n_mem_base, 0444);
MODULE_PARM_DESC(mem_base, "Physical start of each Codec DMA buffer memory");
module_param_array(mem_size, ulong, &n_mem_size, 0444);
MODULE_PARM_DESC(mem_size, "Size of each Codec DMA buffer memory, bytes");

static unsigned long regs_base = 0x03000000;
static unsigned long regs_size = 0x01000000;
module_param(regs_base, ulong, 0444);
MODULE_PARM_DESC(regs_base, "Physical start of the LPASS register space");
module_param(regs_size, ulong, 0444);
MODULE_PARM_DESC(regs_size, "Size of the LPASS register space, bytes");

// the mapping [pa, pa + len) lies inside [base, base + size)
static bool ar_lpass_inside(unsigned long pa, unsigned long len, unsigned long base,
			    unsigned long size)
{
	return pa >= base && len <= size && pa - base <= size - len;
}

static int ar_lpass_mem_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long pa = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long len = vma->vm_end - vma->vm_start;
	int i;

	for (i = 0; i < n_mem_base; i++) {
		if (ar_lpass_inside(pa, len, mem_base[i], mem_size[i])) {
			vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
			return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, len,
					       vma->vm_page_prot);
		}
	}
	return -EPERM;
}

static int ar_lpass_regs_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long pa = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long len = vma->vm_end - vma->vm_start;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;
	if (!ar_lpass_inside(pa, len, regs_base, regs_size))
		return -EPERM;
	vm_flags_clear(vma, VM_MAYWRITE);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, len, vma->vm_page_prot);
}

static const struct file_operations ar_lpass_mem_fops = {
	.owner = THIS_MODULE,
	.mmap = ar_lpass_mem_mmap,
};

static const struct file_operations ar_lpass_regs_fops = {
	.owner = THIS_MODULE,
	.mmap = ar_lpass_regs_mmap,
};

static struct miscdevice ar_lpass_mem_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ar_lpass_mem",
	.fops = &ar_lpass_mem_fops,
	.mode = 0600,
};

static struct miscdevice ar_lpass_regs_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ar_lpass_regs",
	.fops = &ar_lpass_regs_fops,
	.mode = 0600,
};

static int __init ar_lpass_map_init(void)
{
	int i, ret;

	if (n_mem_base != n_mem_size)
		return -EINVAL;
	for (i = 0; i < n_mem_base; i++) {
		if ((mem_base[i] | mem_size[i]) & ~PAGE_MASK)
			return -EINVAL;
		pr_info("ar_lpass_map: mem 0x%lx + 0x%lx\n", mem_base[i], mem_size[i]);
	}
	if ((regs_base | regs_size) & ~PAGE_MASK)
		return -EINVAL;
	pr_info("ar_lpass_map: regs 0x%lx + 0x%lx (read only)\n", regs_base, regs_size);

	ret = misc_register(&ar_lpass_mem_dev);
	if (ret)
		return ret;
	ret = misc_register(&ar_lpass_regs_dev);
	if (ret)
		misc_deregister(&ar_lpass_mem_dev);
	return ret;
}

static void __exit ar_lpass_map_exit(void)
{
	misc_deregister(&ar_lpass_regs_dev);
	misc_deregister(&ar_lpass_mem_dev);
}

module_init(ar_lpass_map_init);
module_exit(ar_lpass_map_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Victor Zappi");
MODULE_DESCRIPTION("Maps the LPASS Codec DMA buffers and registers for ar-audioengine's CPU-buffer route");
