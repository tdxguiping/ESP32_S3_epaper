//
// Created by gj21798 on 2019/10/31.
//
 
#ifndef AESWITHPUREC_AES_UTIL_H
#define AESWITHPUREC_AES_UTIL_H
 
#define MAX_LEN (2*1024*1024)
#define AES_KEY_SIZE 128
 
unsigned char *encrypt(const unsigned char *in, int in_len, unsigned int *out_len,
                       const unsigned char *key, const unsigned char *iv);
 
unsigned char *decrypt(const unsigned char *in, int in_len, unsigned int *out_len,
                       const unsigned char *key, const unsigned char *iv);

unsigned char *encrypt_ecb(const unsigned char *data, int in_len, unsigned int *out_len,
            			const unsigned char *key);

unsigned char *decrypt_ecb(const unsigned char *data, int in_len, unsigned int *out_len,
            			const unsigned char *key);
#endif //AESWITHPUREC_AES_UTIL_H

