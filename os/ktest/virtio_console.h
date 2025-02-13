
/**
 * Virtio console used by ktest.
 *
 * `virt` platform in QEMU only supports one UART and it is used by `printf`. So here we use
 * the virtio console to distinguish the output from `printf`. To simplify the logic, we use
 * the emergency write of the virtio console to output char.
 */

#ifndef VIRTIO_CONSOLE_H
#define VIRTIO_CONSOLE_H

#include "../memlayout.h"

#define DEVICE_ID_ADDR KERNEL_VIRTIO_MMIO7_BASE + 0x8
// 0x100: Configuration Space offset; 0x8: Offset of emerg_wr
#define EMERG_WR_ADDR KERNEL_VIRTIO_MMIO7_BASE + 0x100 + 0x8

void virtio_putchar(int);
void virtio_console_init();

#endif