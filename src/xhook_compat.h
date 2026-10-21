// xhook_compat.h - force-included into xHook's C sources.
//
// xh_util.c uses PAGE_SIZE/PAGE_MASK without including the header that
// provides them, and a hardcoded 4096 is wrong on 16 KB-page devices.
// Resolve both at runtime instead.

#pragma once

#include <unistd.h>

#undef PAGE_SIZE
#undef PAGE_MASK

#define PAGE_SIZE (getpagesize())
#define PAGE_MASK (~(PAGE_SIZE - 1))
