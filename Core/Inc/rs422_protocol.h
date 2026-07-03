#ifndef __RS422_PROTOCOL_H
#define __RS422_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rs422.h"

#define RS422_PROTOCOL_HEADER              0x0FF0U//帧头
#define RS422_PROTOCOL_HEADER_HIGH         0x0FU//帧头高字节
#define RS422_PROTOCOL_HEADER_LOW          0xF0U//帧头低字节
#define RS422_PROTOCOL_DATA_LENGTH         128U//数据区长度
#define RS422_PROTOCOL_FRAME_LENGTH        135U//帧长度
#define RS422_PROTOCOL_ERROR_RATE_SCALE    1000000U//错误率缩放

typedef enum
{
    RS422_PROTOCOL_TYPE_00 = 0x00U,//数据类型 00
    RS422_PROTOCOL_TYPE_FF = 0xFFU,//数据类型 FF
    RS422_PROTOCOL_TYPE_55 = 0x55U,//数据类型 55
    RS422_PROTOCOL_TYPE_AA = 0xAAU//数据类型 AA
} RS422_ProtocolDataType;

typedef enum
{
    RS422_PROTOCOL_DIR_TX = 0U,//数据发送方向
    RS422_PROTOCOL_DIR_RX = 1U//数据接收方向
} RS422_ProtocolDirection;

typedef enum
{
    RS422_PROTOCOL_STATUS_OK = 0U,
    RS422_PROTOCOL_STATUS_NO_FRAME,//缓冲区内目前没有完整的数据帧
    RS422_PROTOCOL_STATUS_INVALID_PORT,//传入了不存在的串口号
    RS422_PROTOCOL_STATUS_INVALID_ARGUMENT,//函数参数传入了空指针（NULL）或越界数值
    RS422_PROTOCOL_STATUS_INVALID_HEADER,//接收到的数据未能匹配到 0x0FF0 帧头
    RS422_PROTOCOL_STATUS_INVALID_TYPE,//接收到的数据类型不合法
    RS422_PROTOCOL_STATUS_INVALID_DIRECTION,//接收到的数据方向不合法
    RS422_PROTOCOL_STATUS_INVALID_LENGTH,//接收到的数据长度不合法
    RS422_PROTOCOL_STATUS_CHECKSUM_ERROR//接收到的数据校验和错误
} RS422_ProtocolStatus;

typedef struct
{
    uint8_t id;//板子对外接口号
    uint8_t data_type;//数据类型
    uint8_t direction;//数据方向
    uint8_t length;//数据长度
    uint8_t data[RS422_PROTOCOL_DATA_LENGTH];//数据内容
    uint8_t checksum;//数据校验和
} RS422_ProtocolPacket;

typedef struct
{
    uint32_t total_bytes;//接收总字节数
    uint32_t good_frames;//接收的有效帧数
    uint32_t error_frames;//接收的错误帧数
    uint32_t error_bytes;//接收的错误字节数
    uint32_t sync_drop_bytes;//接收的同步丢失字节数
    uint32_t error_rate_ppm;//接收的错误率（百万分之一）
} RS422_ProtocolStats;// 协议接收统计结构体

/* 以下统计变量用于 DAPLink 直接观察协议层接收总字节数和误码率。 */
extern volatile uint32_t rs422_protocol_rx_total_bytes[RS422_PORT_COUNT];//接收总字节数
extern volatile uint32_t rs422_protocol_rx_good_frames[RS422_PORT_COUNT];//接收的有效帧数
extern volatile uint32_t rs422_protocol_rx_error_frames[RS422_PORT_COUNT];//接收的错误帧数
extern volatile uint32_t rs422_protocol_rx_error_bytes[RS422_PORT_COUNT];//接收的错误字节数
extern volatile uint32_t rs422_protocol_rx_sync_drop_bytes[RS422_PORT_COUNT];//接收的同步丢失字节数
extern volatile uint32_t rs422_protocol_rx_error_rate_ppm[RS422_PORT_COUNT];//接收的错误率（百万分之一）

uint8_t RS422_ProtocolIsValidDataType(uint8_t data_type);//判断数据类型是否合法
uint8_t RS422_ProtocolChecksum(const uint8_t *data, uint16_t size);//计算累加校验
HAL_StatusTypeDef RS422_ProtocolBuildFrame(uint8_t id,
                                           uint8_t data_type,
                                           uint8_t direction,
                                           const uint8_t data[RS422_PROTOCOL_DATA_LENGTH],
                                           uint8_t frame[RS422_PROTOCOL_FRAME_LENGTH]);//组装一帧完整协议数据
HAL_StatusTypeDef RS422_ProtocolSend(RS422_PortId port,
                                      uint8_t id,
                                      uint8_t data_type,
                                      const uint8_t data[RS422_PROTOCOL_DATA_LENGTH],
                                      uint32_t timeout);//发送数据
RS422_ProtocolStatus RS422_ProtocolDecodeFrame(const uint8_t frame[RS422_PROTOCOL_FRAME_LENGTH],
                                               RS422_ProtocolPacket *packet);//解码一帧完整协议数据
RS422_ProtocolStatus RS422_ProtocolPoll(RS422_PortId port, RS422_ProtocolPacket *packet);//轮询接收数据
void RS422_ProtocolClearStats(RS422_PortId port);//清空指定端口的协议层接收统计和解析状态
void RS422_ProtocolGetStats(RS422_PortId port, RS422_ProtocolStats *stats);//获取指定端口的协议层接收统计

#ifdef __cplusplus
}
#endif

#endif /* __RS422_PROTOCOL_H */
