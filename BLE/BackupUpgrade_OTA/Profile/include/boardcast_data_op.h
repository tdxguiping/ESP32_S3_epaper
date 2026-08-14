#ifndef BOARDCAST_DATA_OP_H
#define BOARDCAST_DATA_OP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"
#include "app_cfg.h"


/**
 * 数据段类型定义
 */
typedef enum {
    TYPE_UNKNOWN = 0,
    TYPE_CONFIG = 1,
    TYPE_DATA = 2,
    TYPE_COMMAND = 3,
    // 添加更多类型...
} DataSegmentType;

/**
 * 数据段结构体
 */
typedef struct {
    int totalLength;        // 整个段的长度（包括类型和数据）
    DataSegmentType type;   // 数据类型
    int dataLength;         // 数据部分的长度
    const unsigned char *data;  // 数据指针
} DataSegment;

#endif

