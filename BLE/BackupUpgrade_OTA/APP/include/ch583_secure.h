#ifndef CH583_SECURE_H
#define CH583_SECURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"
#include "app_cfg.h"

#ifdef ENABLE_BOARD_ENCRYPT
//void aes_mac_example(char *str, int len);
void get_aes_key(char *key);
uint8_t is_illegal_device(unsigned char *mac, int len);
uint8_t write_key_to_epprom(uint8_t *data, uint8_t isForceWrite);
#endif

#ifdef __cplusplus
}
#endif

#endif


