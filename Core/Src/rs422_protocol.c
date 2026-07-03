#include "rs422_protocol.h"

#include <string.h>

#define RS422_PROTOCOL_BODY_LENGTH         (RS422_PROTOCOL_FRAME_LENGTH - 2U)//帧体长度（除去帧头 2 字节）
#define RS422_PROTOCOL_ID_OFFSET           2U//板子对外接口号字段偏移
#define RS422_PROTOCOL_TYPE_OFFSET         3U//数据类型字段偏移
#define RS422_PROTOCOL_DIRECTION_OFFSET    4U//方向字段偏移
#define RS422_PROTOCOL_LENGTH_OFFSET       5U//数据长度字段偏移
#define RS422_PROTOCOL_DATA_OFFSET         6U//数据字段偏移
#define RS422_PROTOCOL_CHECKSUM_OFFSET     (RS422_PROTOCOL_FRAME_LENGTH - 1U)//校验和字段偏移

typedef enum
{
    RS422_PROTOCOL_PARSE_HEADER_HIGH = 0U,//等待帧头高字节
    RS422_PROTOCOL_PARSE_HEADER_LOW,//等待帧头低字节
    RS422_PROTOCOL_PARSE_BODY//等待帧体
} RS422_ProtocolParseState;//协议解析状态机状态

typedef struct
{
    RS422_ProtocolParseState state;//协议解析状态机状态
    uint8_t frame[RS422_PROTOCOL_FRAME_LENGTH];//接收缓冲区
    uint16_t index;//接收缓冲区索引
} RS422_ProtocolParser;//协议解析器

static RS422_ProtocolParser rs422_protocol_parser[RS422_PORT_COUNT];

/* 以下统计变量用于接收侧诊断，误码率单位为 ppm。 */
volatile uint32_t rs422_protocol_rx_total_bytes[RS422_PORT_COUNT] = {0U};//接收总字节数
volatile uint32_t rs422_protocol_rx_good_frames[RS422_PORT_COUNT] = {0U};//接收的有效帧数
volatile uint32_t rs422_protocol_rx_error_frames[RS422_PORT_COUNT] = {0U};//接收的错误帧数
volatile uint32_t rs422_protocol_rx_error_bytes[RS422_PORT_COUNT] = {0U};//接收的错误字节数
volatile uint32_t rs422_protocol_rx_sync_drop_bytes[RS422_PORT_COUNT] = {0U};//接收的同步丢失字节数
volatile uint32_t rs422_protocol_rx_error_rate_ppm[RS422_PORT_COUNT] = {0U};//接收的错误率（ppm）

/**
  * @brief  判断 RS422 协议端口编号是否有效。
  * @param  port: RS422 端口枚举值。
  * @retval 1 表示有效，0 表示无效。
  */
static uint8_t RS422_ProtocolIsValidPort(RS422_PortId port)
{
    return ((uint32_t)port < (uint32_t)RS422_PORT_COUNT) ? 1U : 0U;
}

/**
  * @brief  判断协议方向字段是否在允许范围内。
  * @param  direction: 待检查的方向字段，0 表示发送，1 表示接收。
  * @retval 1 表示有效，0 表示无效。
  */
static uint8_t RS422_ProtocolIsValidDirection(uint8_t direction)
{
    return ((direction == (uint8_t)RS422_PROTOCOL_DIR_TX) ||
            (direction == (uint8_t)RS422_PROTOCOL_DIR_RX)) ? 1U : 0U;
}

/**
  * @brief  刷新指定端口的协议层误码率统计。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
static void RS422_ProtocolUpdateErrorRate(RS422_PortId port)
{
    uint64_t scaled_error;

    if (rs422_protocol_rx_total_bytes[port] == 0U)
    {
        rs422_protocol_rx_error_rate_ppm[port] = 0U;
        return;
    }

    scaled_error = (uint64_t)rs422_protocol_rx_error_bytes[port] *
                   (uint64_t)RS422_PROTOCOL_ERROR_RATE_SCALE;
    rs422_protocol_rx_error_rate_ppm[port] =
        (uint32_t)(scaled_error / rs422_protocol_rx_total_bytes[port]);
}

/**
  * @brief  记录一个被丢弃的非包头同步字节。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
static void RS422_ProtocolRecordSyncDrop(RS422_PortId port)
{
    rs422_protocol_rx_sync_drop_bytes[port]++;
    rs422_protocol_rx_error_bytes[port]++;
    RS422_ProtocolUpdateErrorRate(port);
}

/**
  * @brief  记录一个校验或字段错误的完整协议帧。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
static void RS422_ProtocolRecordErrorFrame(RS422_PortId port)
{
    rs422_protocol_rx_error_frames[port]++;
    rs422_protocol_rx_error_bytes[port] += RS422_PROTOCOL_FRAME_LENGTH;
    RS422_ProtocolUpdateErrorRate(port);
}

/**
  * @brief  重置指定端口的协议接收状态机。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
static void RS422_ProtocolResetParser(RS422_PortId port)
{
    rs422_protocol_parser[port].state = RS422_PROTOCOL_PARSE_HEADER_HIGH;
    rs422_protocol_parser[port].index = 0U;
}

/**
  * @brief  判断数据类型字段是否属于协议允许的 00/FF/55/AA。
  * @param  data_type: 待检查的数据类型字段。
  * @retval 1 表示有效，0 表示无效。
  */
uint8_t RS422_ProtocolIsValidDataType(uint8_t data_type)
{
    return ((data_type == (uint8_t)RS422_PROTOCOL_TYPE_00) ||
            (data_type == (uint8_t)RS422_PROTOCOL_TYPE_FF) ||
            (data_type == (uint8_t)RS422_PROTOCOL_TYPE_55) ||
            (data_type == (uint8_t)RS422_PROTOCOL_TYPE_AA)) ? 1U : 0U;
}

/**
  * @brief  计算 RS422 协议的 8 位累加校验。
  * @param  data: 参与校验的数据起始地址。
  * @param  size: 参与校验的字节数。
  * @retval 累加后取低 8 位的校验值。
  */
uint8_t RS422_ProtocolChecksum(const uint8_t *data, uint16_t size)
{
    uint16_t index;
    uint8_t checksum = 0U;

    if (data == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < size; index++)
    {
        checksum = (uint8_t)(checksum + data[index]);
    }

    return checksum;
}

/**
  * @brief  按固定帧格式组装一帧 RS422 协议数据。
  * @param  id: 板子对外接口号。
  * @param  data_type: 数据类型，只允许 00/FF/55/AA。
  * @param  direction: 收发指示，0 表示发送，1 表示接收。
  * @param  data: 128 字节固定数据区。
  * @param  frame: 输出的 135 字节完整协议帧。
  * @retval HAL 状态，HAL_OK 表示组帧成功。
  */
HAL_StatusTypeDef RS422_ProtocolBuildFrame(uint8_t id,
                                           uint8_t data_type,
                                           uint8_t direction,
                                           const uint8_t data[RS422_PROTOCOL_DATA_LENGTH],
                                           uint8_t frame[RS422_PROTOCOL_FRAME_LENGTH])
{
    if ((data == NULL) || (frame == NULL) ||
        (RS422_ProtocolIsValidDataType(data_type) == 0U) ||
        (RS422_ProtocolIsValidDirection(direction) == 0U))
    {
        return HAL_ERROR;
    }

    frame[0U] = RS422_PROTOCOL_HEADER_HIGH;
    frame[1U] = RS422_PROTOCOL_HEADER_LOW;
    frame[RS422_PROTOCOL_ID_OFFSET] = id;
    frame[RS422_PROTOCOL_TYPE_OFFSET] = data_type;
    frame[RS422_PROTOCOL_DIRECTION_OFFSET] = direction;
    frame[RS422_PROTOCOL_LENGTH_OFFSET] = RS422_PROTOCOL_DATA_LENGTH;
    (void)memcpy(&frame[RS422_PROTOCOL_DATA_OFFSET], data, RS422_PROTOCOL_DATA_LENGTH);
    frame[RS422_PROTOCOL_CHECKSUM_OFFSET] =
        RS422_ProtocolChecksum(&frame[RS422_PROTOCOL_ID_OFFSET],
                               (uint16_t)(RS422_PROTOCOL_FRAME_LENGTH - 3U));

    return HAL_OK;
}

/**
  * @brief  组装并发送一帧方向为发送的 RS422 协议数据。
  * @param  port: RS422 端口枚举值。
  * @param  id: 板子对外接口号。
  * @param  data_type: 数据类型，只允许 00/FF/55/AA。
  * @param  data: 128 字节固定数据区。
  * @param  timeout: 阻塞等待发送完成的超时时间，单位 ms。
  * @retval HAL 状态，HAL_OK 表示发送完成。
  */
HAL_StatusTypeDef RS422_ProtocolSend(RS422_PortId port,
                                      uint8_t id,
                                      uint8_t data_type,
                                      const uint8_t data[RS422_PROTOCOL_DATA_LENGTH],
                                      uint32_t timeout)
{
    uint8_t frame[RS422_PROTOCOL_FRAME_LENGTH];
    HAL_StatusTypeDef status;

    if (RS422_ProtocolIsValidPort(port) == 0U)
    {
        return HAL_ERROR;
    }

    status = RS422_ProtocolBuildFrame(id,
                                      data_type,
                                      (uint8_t)RS422_PROTOCOL_DIR_TX,
                                      data,
                                      frame);
    if (status != HAL_OK)
    {
        return status;
    }

    return RS422_Transmit(port, frame, RS422_PROTOCOL_FRAME_LENGTH, timeout);
}

/**
  * @brief  解码一帧完整 RS422 协议数据并检查字段和校验位。
  * @param  frame: 输入的 135 字节完整协议帧。
  * @param  packet: 解码后的协议字段和数据。
  * @retval 协议解析状态。
  */
RS422_ProtocolStatus RS422_ProtocolDecodeFrame(const uint8_t frame[RS422_PROTOCOL_FRAME_LENGTH],
                                               RS422_ProtocolPacket *packet)
{
    uint8_t checksum;

    if ((frame == NULL) || (packet == NULL))
    {
        return RS422_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if ((frame[0U] != RS422_PROTOCOL_HEADER_HIGH) ||
        (frame[1U] != RS422_PROTOCOL_HEADER_LOW))
    {
        return RS422_PROTOCOL_STATUS_INVALID_HEADER;
    }

    checksum = RS422_ProtocolChecksum(&frame[RS422_PROTOCOL_ID_OFFSET],
                                      (uint16_t)(RS422_PROTOCOL_FRAME_LENGTH - 3U));
    if (checksum != frame[RS422_PROTOCOL_CHECKSUM_OFFSET])
    {
        return RS422_PROTOCOL_STATUS_CHECKSUM_ERROR;
    }

    if (RS422_ProtocolIsValidDataType(frame[RS422_PROTOCOL_TYPE_OFFSET]) == 0U)
    {
        return RS422_PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (RS422_ProtocolIsValidDirection(frame[RS422_PROTOCOL_DIRECTION_OFFSET]) == 0U)
    {
        return RS422_PROTOCOL_STATUS_INVALID_DIRECTION;
    }

    if (frame[RS422_PROTOCOL_LENGTH_OFFSET] != RS422_PROTOCOL_DATA_LENGTH)
    {
        return RS422_PROTOCOL_STATUS_INVALID_LENGTH;
    }

    packet->id = frame[RS422_PROTOCOL_ID_OFFSET];
    packet->data_type = frame[RS422_PROTOCOL_TYPE_OFFSET];
    packet->direction = frame[RS422_PROTOCOL_DIRECTION_OFFSET];
    packet->length = frame[RS422_PROTOCOL_LENGTH_OFFSET];
    (void)memcpy(packet->data,
                 &frame[RS422_PROTOCOL_DATA_OFFSET],
                 RS422_PROTOCOL_DATA_LENGTH);
    packet->checksum = frame[RS422_PROTOCOL_CHECKSUM_OFFSET];

    return RS422_PROTOCOL_STATUS_OK;
}

/**
  * @brief  从指定 RS422 端口的字节流中轮询解析一帧协议数据。
  * @param  port: RS422 端口枚举值。
  * @param  packet: 成功解析后的协议包输出。
  * @retval 协议解析状态，NO_FRAME 表示当前还没有完整帧。
  */
RS422_ProtocolStatus RS422_ProtocolPoll(RS422_PortId port, RS422_ProtocolPacket *packet)
{
    uint8_t byte;
    uint16_t read_size;
    RS422_ProtocolParser *parser;
    RS422_ProtocolStatus status = RS422_PROTOCOL_STATUS_NO_FRAME;

    if (RS422_ProtocolIsValidPort(port) == 0U)
    {
        return RS422_PROTOCOL_STATUS_INVALID_PORT;
    }

    if (packet == NULL)
    {
        return RS422_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    parser = &rs422_protocol_parser[port];

    while (RS422_Available(port) > 0U)
    {
        read_size = RS422_Read(port, &byte, 1U);
        if (read_size == 0U)
        {
            break;
        }

        rs422_protocol_rx_total_bytes[port]++;

        switch (parser->state)
        {
            case RS422_PROTOCOL_PARSE_HEADER_HIGH:
                if (byte == RS422_PROTOCOL_HEADER_HIGH)
                {
                    parser->frame[0U] = byte;
                    parser->state = RS422_PROTOCOL_PARSE_HEADER_LOW;
                }
                else
                {
                    RS422_ProtocolRecordSyncDrop(port);
                }
                break;

            case RS422_PROTOCOL_PARSE_HEADER_LOW:
                if (byte == RS422_PROTOCOL_HEADER_LOW)
                {
                    parser->frame[1U] = byte;
                    parser->index = 2U;
                    parser->state = RS422_PROTOCOL_PARSE_BODY;
                }
                else
                {
                    RS422_ProtocolRecordSyncDrop(port);
                    if (byte == RS422_PROTOCOL_HEADER_HIGH)
                    {
                        parser->frame[0U] = byte;
                        parser->state = RS422_PROTOCOL_PARSE_HEADER_LOW;
                    }
                    else
                    {
                        RS422_ProtocolResetParser(port);
                    }
                }
                break;

            case RS422_PROTOCOL_PARSE_BODY:
                parser->frame[parser->index] = byte;
                parser->index++;
                if (parser->index >= RS422_PROTOCOL_FRAME_LENGTH)
                {
                    status = RS422_ProtocolDecodeFrame(parser->frame, packet);
                    if (status == RS422_PROTOCOL_STATUS_OK)
                    {
                        rs422_protocol_rx_good_frames[port]++;
                        RS422_ProtocolUpdateErrorRate(port);
                    }
                    else
                    {
                        RS422_ProtocolRecordErrorFrame(port);
                    }
                    RS422_ProtocolResetParser(port);
                    return status;
                }
                break;

            default:
                RS422_ProtocolResetParser(port);
                break;
        }
    }

    return status;
}

/**
  * @brief  清空指定端口的协议层接收统计和解析状态。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
void RS422_ProtocolClearStats(RS422_PortId port)
{
    if (RS422_ProtocolIsValidPort(port) == 0U)
    {
        return;
    }

    rs422_protocol_rx_total_bytes[port] = 0U;
    rs422_protocol_rx_good_frames[port] = 0U;
    rs422_protocol_rx_error_frames[port] = 0U;
    rs422_protocol_rx_error_bytes[port] = 0U;
    rs422_protocol_rx_sync_drop_bytes[port] = 0U;
    rs422_protocol_rx_error_rate_ppm[port] = 0U;
    RS422_ProtocolResetParser(port);
}

/**
  * @brief  读取指定端口的协议层接收统计。
  * @param  port: RS422 端口枚举值。
  * @param  stats: 接收统计输出结构。
  * @retval None
  */
void RS422_ProtocolGetStats(RS422_PortId port, RS422_ProtocolStats *stats)
{
    if ((RS422_ProtocolIsValidPort(port) == 0U) || (stats == NULL))
    {
        return;
    }

    stats->total_bytes = rs422_protocol_rx_total_bytes[port];
    stats->good_frames = rs422_protocol_rx_good_frames[port];
    stats->error_frames = rs422_protocol_rx_error_frames[port];
    stats->error_bytes = rs422_protocol_rx_error_bytes[port];
    stats->sync_drop_bytes = rs422_protocol_rx_sync_drop_bytes[port];
    stats->error_rate_ppm = rs422_protocol_rx_error_rate_ppm[port];
}
