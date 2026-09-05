#pragma once

#include <stddef.h>
#include <stdint.h>

class ePaperPort;

bool EpdType12081600_1243_BOE_FillTestPattern(uint8_t *display_buf,
                                              size_t display_size);
void EpdType12081600_1243_BOE_Display(ePaperPort &epd,
                                      const uint8_t *display_buf,
                                      size_t display_size);
