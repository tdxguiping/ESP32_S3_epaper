//
// Created by gj21798 on 2019/10/31.
//
 
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "aes.h"
#include "aes_util.h"

// ECB模式AES加密
unsigned char *
encrypt_ecb(const unsigned char *data, int in_len, unsigned int *out_len,
            const unsigned char *key) {
    if (in_len <= 0 || in_len >= MAX_LEN) {
        return NULL;
    }
    if (!data || !key) {
        return NULL;
    }

    // 计算填充长度（PKCS#7填充）
    unsigned int rest_len = in_len % AES_BLOCK_SIZE;
    unsigned int padding_len = (rest_len == 0) ? AES_BLOCK_SIZE : (AES_BLOCK_SIZE - rest_len);
    unsigned int src_len = in_len + padding_len;

    // 分配输入缓冲区并填充
    unsigned char *input = (unsigned char *)calloc(1, src_len);
    if (!input) {
        return NULL;
    }
    memcpy(input, data, in_len);
    // 填充PKCS#7格式数据
    for (unsigned int i = 0; i < padding_len; i++) {
        input[in_len + i] = (unsigned char)padding_len;
    }

    // 分配输出缓冲区
    unsigned char *buff = (unsigned char *)calloc(1, src_len);
    if (!buff) {
        free(input);
        return NULL;
    }

    // 密钥扩展
    unsigned int key_schedule[AES_BLOCK_SIZE * 4] = {0};  // 足够存储各密钥长度的扩展密钥
    aes_key_setup(key, key_schedule, AES_KEY_SIZE);  // 假设AES_KEY_SIZE是定义的密钥长度（128/192/256）

    // ECB模式加密（逐个块处理）
    for (unsigned int i = 0; i < src_len; i += AES_BLOCK_SIZE) {
        aes_encrypt(&input[i], &buff[i], key_schedule, AES_KEY_SIZE);
    }

    *out_len = src_len;
    free(input);
    return buff;
}

// ECB模式AES解密
unsigned char *
decrypt_ecb(const unsigned char *data, int in_len, unsigned int *out_len,
            const unsigned char *key) {
    if (in_len <= 0 || in_len >= MAX_LEN || (in_len % AES_BLOCK_SIZE) != 0) {
        return NULL;  // ECB模式要求输入长度为块大小的整数倍
    }
    if (!data || !key) {
        return NULL;
    }

    unsigned int src_len = in_len;
    unsigned char *buff = (unsigned char *)calloc(1, src_len);
    if (!buff) {
        return NULL;
    }

    // 密钥扩展
    unsigned int key_schedule[AES_BLOCK_SIZE * 4] = {0};
    aes_key_setup(key, key_schedule, AES_KEY_SIZE);

    // ECB模式解密（逐个块处理）
    for (unsigned int i = 0; i < src_len; i += AES_BLOCK_SIZE) {
        aes_decrypt(&data[i], &buff[i], key_schedule, AES_KEY_SIZE);
    }

    // 去除PKCS#7填充
    unsigned char padding_len = buff[src_len - 1];
    if (padding_len > 0 && padding_len <= AES_BLOCK_SIZE) {
        // 验证填充合法性（可选，增强安全性）
        int valid = 1;
        for (unsigned int i = 0; i < padding_len; i++) {
            if (buff[src_len - 1 - i] != padding_len) {
                valid = 0;
                break;
            }
        }
        if (valid) {
            src_len -= padding_len;
        }
    }

    *out_len = src_len;
    return buff;
}

unsigned char *
encrypt(const unsigned char *data, int in_len, unsigned int *out_len,
        const unsigned char *key, const unsigned char *iv) {
    if (in_len <= 0 || in_len >= MAX_LEN) {
        return NULL;
    }
 
    if (!data) {
        return NULL;
    }
 
    unsigned int rest_len = in_len % AES_BLOCK_SIZE;
    unsigned int padding_len = AES_BLOCK_SIZE - rest_len;
    unsigned int src_len = in_len + padding_len;
 
    unsigned char *input = (unsigned char *) calloc(1, src_len);
    memcpy(input, data, in_len);
    if (padding_len > 0) {
//        memset(input + in_len, (unsigned char) padding_len, padding_len);
        for (unsigned int i = 0; i < padding_len; i++) {
            *(input + in_len + i) = (unsigned char) padding_len;
        }
    }
 
    unsigned char *buff = (unsigned char *) calloc(1, src_len);
    if (!buff) {
        free(input);
        return NULL;
    }
 
    unsigned int key_schedule[AES_BLOCK_SIZE * 4] = {0};
 
    aes_key_setup(key, key_schedule, AES_KEY_SIZE);
    aes_encrypt_cbc(input, src_len, buff, key_schedule, AES_KEY_SIZE, iv);
    *out_len = src_len;
 
    //内存释放
    free(input);
 
    return buff;
}
 
unsigned char
*decrypt(const unsigned char *data, int in_len, unsigned int *out_len,
         const unsigned char *key, const unsigned char *iv) {
    if (in_len <= 0 || in_len >= MAX_LEN) {
        return NULL;
    }
    if (!data) {
        return NULL;
    }
 
    unsigned int padding_len = 0;
    unsigned int src_len = in_len + padding_len;
 
    unsigned char *input = (unsigned char *) calloc(1, src_len);
    memcpy(input, data, in_len);
    if (padding_len > 0) {
//        memset(input + in_len, (unsigned char) padding_len, padding_len);
        for (unsigned int i = 0; i < padding_len; i++) {
            *(input + in_len + i) = (unsigned char) padding_len;
        }
    }
 
    unsigned char *buff = (unsigned char *) calloc(1, src_len);
    if (!buff) {
        free(input);
        return NULL;
    }
 
    unsigned int key_schedule[AES_BLOCK_SIZE * 4] = {0};
 
    aes_key_setup(key, key_schedule, AES_KEY_SIZE);
    aes_decrypt_cbc(input, src_len, buff, key_schedule, AES_KEY_SIZE, iv);
 
    unsigned char *ptr = buff;
    ptr += (src_len - 1);
    padding_len = (unsigned int) *ptr;
    if (padding_len > 0 && padding_len <= AES_BLOCK_SIZE) {
        src_len -= padding_len;
    }
 
    *out_len = src_len;
 
    //内存释放
    free(input);
 
    return buff;
}

