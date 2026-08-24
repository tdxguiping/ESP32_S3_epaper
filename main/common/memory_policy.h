#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *MemoryPolicy_AllocBuffer(size_t size);
void *MemoryPolicy_ReallocBuffer(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif
