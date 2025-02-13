#include "virtio_console.h"

#include "../log.h"
#include "../printf.h"

static int virtio_inited = false;

void virtio_putchar(int ch) {
    if (!virtio_inited) {
        panic("VirtIO not inited");
    }
    *(volatile unsigned int *)(EMERG_WR_ADDR) = ch;
}

void virtio_console_init() {
    if (*(volatile unsigned int *)(DEVICE_ID_ADDR) != 3) {
        panic("Not found VirtIO console");
    }
    virtio_inited = true;
}
