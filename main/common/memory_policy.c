#include "memory_policy.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "tdx_cfg.h"

void *MemoryPolicy_AllocBuffer(size_t size)
{
    if (size > USER_INTERNAL_RAM_FALLBACK_MAX_SIZE) {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return malloc(size);
}

void *MemoryPolicy_ReallocBuffer(void *ptr, size_t size)
{
    if (size > USER_INTERNAL_RAM_FALLBACK_MAX_SIZE) {
        return heap_caps_realloc(ptr,
                                 size,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return realloc(ptr, size);
}
