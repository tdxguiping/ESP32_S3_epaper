#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "CONFIG.h"
#include "app_cfg.h"
#include "commoninfo.h"
#include "aes.h"
#include "aes_util.h"  // 假设包含 encrypt_ecb 和 decrypt_ecb 的声明
#include "util.h"

#ifdef ENABLE_BOARD_ENCRYPT
// 示例密钥（128位，16字节）
unsigned char aes_key[KEY_Len] = {
    0xe8, 0x94, 0xfc, 0xfe, 0xe7, 0x68, 0x3f, 0x73, 
	0x70, 0x15, 0xf4, 0x90, 0x0e, 0x51, 0xd2, 0xe0
};

unsigned char aes_key_bak[KEY_Len] = {
	0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

// 加密：先与key的倒置数组异或，再加0x14
void key_encrypt(unsigned char plaintext[KEY_Len], const unsigned char key[16], unsigned char ciphertext[16]) {
    unsigned char reversed_key[KEY_Len];
    // 倒置key数组（例如key[0]和key[15]交换，key[1]和key[14]交换...）
    for (int i = 0; i < KEY_Len; i++) {
        reversed_key[i] = key[15 - i];
    }
    // 异或 + 加0x14
    for (int i = 0; i < KEY_Len; i++) {
        ciphertext[i] = (plaintext[i] ^ reversed_key[i]) + 0x14;
    }
}

// 解密：先减0x14，再与key的倒置数组异或（逆向操作）
void key_decrypt(const unsigned char ciphertext[16], const unsigned char key[16], unsigned char plaintext[16]) {
    unsigned char reversed_key[16];
    // 同样需要先倒置key（与加密时的倒置规则一致）
    for (int i = 0; i < KEY_Len; i++) {
        reversed_key[i] = key[15 - i];
    }
    // 减0x14 + 异或
    for (int i = 0; i < KEY_Len; i++) {
        plaintext[i] = (ciphertext[i] - 0x14) ^ reversed_key[i];
    }
}

void get_aes_key(char *key) 
{
    key_decrypt(aes_key, aes_key_bak, key);
}

uint8_t is_need_write_key()
{
	int i = 0;
	unsigned char key_read[KEY_Len];
	Get_Key_EEPROM_Flag(key_read,KEY_Position,KEY_Len);

	for(i=0; i<KEY_Len; i++){
		if(key_read[i] != 0xff){
			return Is_No;
		}
	}

	return Is_Yes;
}

uint8_t write_key_to_epprom(uint8_t *data, uint8_t isForceWrite)
{
	unsigned char key_burn[KEY_BURN_Len]={0x01};

	//Print_I3("write_key_to_epprom data :\n");
	//hex_dump(data, KEY_Len);
	if(isForceWrite == Is_Yes){
		Print_I3("key force\n");
		Save_Key_EEPROM_Flag(data,KEY_Position,KEY_Len);
		Save_Key_EEPROM_Flag(key_burn,KEY_BURN_Position,KEY_BURN_Len);

		return Is_Yes;
	}else{
		if(global_DEVICE_STATUS.fisBurnId != Is_Yes){
			Print_I3("key wr\n");
			Save_Key_EEPROM_Flag(data,KEY_Position,KEY_Len);
			Save_Key_EEPROM_Flag(key_burn,KEY_BURN_Position,KEY_BURN_Len);
			return Is_Yes;
		}else{
			Print_I3("key no\n");
			return Is_No;
		}
	}
}

uint8_t is_illegal_device(unsigned char *mac, int len)
{
#if 1
    unsigned int decrypted_len;
	unsigned char key_bak[KEY_Len];
	uint8_t device_mac[6];
	get_aes_key(key_bak);
	//Print_I3("get_aes_key: ");
	//hex_dump(key_bak, KEY_Len);

#if 0	
    unsigned int ciphertext_len;
    unsigned char *ciphertext = encrypt_ecb(mac, len, &ciphertext_len, key_bak);
    if (!ciphertext) {
        Print_I3("enc er\n");
        return -1;
    }
	Print_I3("enc len:%d",ciphertext_len);
    hex_dump(ciphertext, ciphertext_len);

	if(is_need_write_key() == Is_Yes){
		Print_I3("need write key");
		write_key_to_epprom(ciphertext);
	}else{
		Print_I3("no need write key");
	}
#endif
	unsigned char key_read[KEY_Len];
	unsigned char key_Burn[KEY_BURN_Len];

	Get_Key_EEPROM_Flag(key_read,KEY_Position,KEY_Len);
	//Print_I3("key_read: KEY_Len:%d",KEY_Len);
	//hex_dump(key_read, KEY_Len);
	Get_Key_EEPROM_Flag(key_Burn,KEY_BURN_Position,KEY_BURN_Len);
	//Print_I3("key_Burn: key_Burn:%d",KEY_BURN_Len);
	//hex_dump(key_Burn, KEY_BURN_Len);
	if(key_Burn[0] == 0x01){
		global_DEVICE_STATUS.fisBurnId =Is_Yes;
	}
	
    unsigned char *decrypted = decrypt_ecb(key_read, KEY_Len, &decrypted_len, key_bak);
    if (!decrypted) {
        Print_I3("dec er\n");
       	free(decrypted);
       	return Is_No;
    }
	//Print_I3("dec len:%d",decrypted_len);
    //hex_dump(decrypted, decrypted_len);

	unsigned char mac_temp[MAC_Len];
	memcpy(mac_temp, mac, MAC_Len); 
	reverseMac(mac_temp);        
	//Print_I3("printf: mac:");
	//hex_dump(mac, 6);
 	// 检查解密结果是否与明文一致
    if (memcmp(mac_temp, decrypted, MAC_Len) == 0) {
        Print_I3("key ok\n");
		global_DEVICE_STATUS.fisVaildDevice =Is_Yes;
		free(decrypted);
		return Is_Yes;
    } else {
        Print_I3("key er\n");
#ifdef EPD_DISPLAY_TEST_ENABLE
		global_DEVICE_STATUS.fisVaildDevice =Is_Yes;
		free(decrypted);
		return Is_Yes;
#else
		global_DEVICE_STATUS.fisVaildDevice =Is_No;
		free(decrypted);
		return Is_No;
#endif
    }
#endif
#if 0
	unsigned char key_bak[KEY_Len];
	unsigned char key_burn[KEY_BURN_Len];

	Get_Key_EEPROM_Flag(key_bak,KEY_Position,KEY_Len);
	Get_Key_EEPROM_Flag(key_burn,KEY_BURN_Position,KEY_BURN_Len);
	Print_I3("key:\n");
	hex_dump(key_bak, KEY_Len);
	Print_I3("burn:\n");
	hex_dump(key_burn, KEY_BURN_Len);
	/*unsigned char key_bak1[KEY_Len]={0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06};
	unsigned char key_burn1[KEY_BURN_Len]={0x06};
	Save_Key_EEPROM_Flag(key_bak1,KEY_Position,KEY_Len);
	Save_Key_EEPROM_Flag(key_burn1,KEY_BURN_Position,KEY_BURN_Len);
	Get_Key_EEPROM_Flag(key_bak,KEY_Position,KEY_Len);
	Get_Key_EEPROM_Flag(key_burn,KEY_BURN_Position,KEY_BURN_Len);
	Print_I3("key1:\n");
	hex_dump(key_bak, KEY_Len);
	Print_I3("burn1:\n");
	hex_dump(key_burn, KEY_BURN_Len);*/
	
	unsigned char mac_bak[MAC_Len];
	Get_EEPROM_Flag(mac_bak,MAC_Position,MAC_Len);
	Print_I3("mac1:\n");
	hex_dump(mac_bak, MAC_Len);
	/*unsigned char mac_bak1[MAC_Len]={0xaa,0xbb,0xcc,0x11,0x22,0x33};
	Save_EEPROM_Flag(mac_bak1,MAC_Position,MAC_Len);
	Get_EEPROM_Flag(mac_bak,MAC_Position,MAC_Len);
	Print_I3("mac2:\n");
	hex_dump(mac_bak, MAC_Len);*/
#endif
}

#if 0
void testAesKey()
{
	unsigned char aes_key[KEY_Len] = {
	    //0x0f, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
	    //0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa7, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
	    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xd2, 0xa7, 0xab, 0xf7, 0x15, 0x88, 0x09, 0x03, 0xcf, 0x4f, 0x3c
	};

	unsigned char data[14] = {
		0x00, 0x02, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00
	};

	/*unsigned char encry_data[16] = {
		//0xa6, 0x59, 0xe6, 0x42, 0x7c, 0x9a, 0x5c, 0xbc, 0x8d, 0x49, 0x2c, 0x84, 0x44, 0x77, 0xfc, 0x9e
		0xfe, 0x0c, 0xf0, 0x3d, 0x0a, 0x1e, 0x19, 0x06, 0x76, 0x36, 0xc0, 0xda, 0x9d, 0xec, 0xc3, 0xf8
	};*/

	unsigned int decrypted_len;
	unsigned int ciphertext_len;
    unsigned char *ciphertext = encrypt_ecb(data, 14, &ciphertext_len, aes_key);
    if (!ciphertext) {
        Print_I3("enc er\n");
		free(ciphertext);
        return;
    }
	Print_I3("enc len:%d",ciphertext_len);
    hex_dump(ciphertext, ciphertext_len);

	//unsigned char *decrypted = decrypt_ecb(encry_data, 16, &decrypted_len, aes_key);
	unsigned char *decrypted = decrypt_ecb(ciphertext, ciphertext_len, &decrypted_len, aes_key);
    if (!decrypted) {
        Print_I3("dec er\n");
		free(decrypted);
       	return;
    }
	Print_I3("dec len:%d",decrypted_len);
    hex_dump(decrypted, decrypted_len);	
	free(ciphertext);
	free(decrypted);
}
#endif
#endif

