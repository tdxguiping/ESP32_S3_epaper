#include <string.h>

#include "boardcast_data_op.h"

/**
 * 解析回调函数类型
 */
typedef void (*DataSegmentCallback)(int segmentIndex, const DataSegment *segment, void *userData);

/**
 * 解析分段的字节数组（格式：长度(含类型+数据)+类型+数据）
 * @param buffer 输入的字节数组
 * @param bufferLen 数组长度
 * @param callback 回调函数，处理每个解析出的数据段
 * @param userData 用户数据，传递给回调函数
 * @return 成功解析的段数
 */
int parseSegmentedData(const unsigned char *buffer, int bufferLen, 
                      DataSegmentCallback callback, void *userData) {
    int segmentCount = 0;
    int offset = 0;
    
    // 循环解析每个数据段
    while (offset < bufferLen) {  // 确保至少还有一个字节（长度字节）
        // 读取当前段的总长度（第一个字节）
        int totalLength = buffer[offset];
        
        // 确保总长度有效（至少包含类型字节）
        if (totalLength < 1) {
            break;  // 无效长度，退出解析
        }
        
        // 检查整个段是否完整（当前位置 + 总长度 <= 缓冲区长度）
        if (offset + totalLength > bufferLen) {
            // 数据不完整，退出解析
            break;
        }
        
        // 读取类型（第二个字节）
        DataSegmentType type = (DataSegmentType)buffer[offset + 1];
        
        // 计算数据部分长度（总长度 - 类型字节长度）
        int dataLength = totalLength - 1;  // 总长度包含类型字节，所以数据长度=总长度-1
        
        // 获取数据指针（跳过长度和类型字节）
        const unsigned char *data = &buffer[offset + 2];
        
        // 构建数据段结构体
        DataSegment segment = {
            .totalLength = totalLength,
            .type = type,
            .dataLength = dataLength,
            .data = data
        };
        
        // 调用回调函数处理当前段
        if (callback) {
            callback(segmentCount, &segment, userData);
        }
        
        // 移动到下一个段的起始位置
        offset += totalLength;
        segmentCount++;
    }
    
    return segmentCount;
}

