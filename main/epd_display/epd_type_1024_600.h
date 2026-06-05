#pragma once
#include <stddef.h>
#include <stdint.h>
class ePaperPort;
void EpdType1024600_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size);
