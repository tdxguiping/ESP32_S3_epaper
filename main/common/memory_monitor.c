#include "memory_monitor.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "MEM";

void MemoryMonitor_Dump(const char *tag)
{
    ESP_LOGI(TAG,
             "%s internal_free=%u internal_largest=%u internal_min=%u psram_free=%u psram_largest=%u",
             tag != NULL ? tag : "(null)",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
