#pragma once
#include <stddef.h>
#include <stdint.h>
class ePaperPort;
void EpdType16001200_133_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size);
