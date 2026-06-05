#pragma once
#include <stddef.h>
#include <stdint.h>
class ePaperPort;
void EpdType800480_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size);
