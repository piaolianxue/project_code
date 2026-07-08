#include "host_comm.h"
#include "usart.h"

#include <string.h>

typedef enum
{
    HOST_COMM_PARSE_HEADER = 0U,
    HOST_COMM_PARSE_COMMAND,
    HOST_COMM_PARSE_PAYLOAD
} HostComm_ParseState;

typedef struct
{
    HostComm_ParseState state;
    uint8_t command;
    uint8_t payload[HOST_COMM_MAX_PARAM_LENGTH + 4U];
    uint16_t payload_length;
    uint8_t rx_byte;
} HostComm_Context;

static HostComm_Context host_comm_ctx;
static uint8_t host_comm_pending_payload[HOST_COMM_MAX_PARAM_LENGTH];
static volatile uint8_t host_comm_command_pending = 0U;
static volatile uint8_t host_comm_pending_command = 0U;
static volatile uint16_t host_comm_pending_payload_length = 0U;

volatile uint32_t host_comm_rx_total_bytes = 0U;
volatile uint32_t host_comm_tx_total_bytes = 0U;
volatile uint32_t host_comm_error_bytes = 0U;
volatile uint32_t host_comm_error_rate_ppm = 0U;
volatile uint32_t host_comm_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
volatile uint8_t host_comm_uart1_enabled = 1U;
volatile uint8_t host_comm_uart3_enabled = 1U;
volatile uint8_t host_comm_last_command = 0U;
volatile HAL_StatusTypeDef host_comm_last_tx_status = HAL_OK;
volatile HAL_StatusTypeDef host_comm_start_receive_status = HAL_OK;

static volatile uint8_t host_comm_stats_upload_pending = 0U;

static const uint8_t host_comm_tail[4U] =
{
    HOST_COMM_FRAME_TAIL_0,
    HOST_COMM_FRAME_TAIL_1,
    HOST_COMM_FRAME_TAIL_2,
    HOST_COMM_FRAME_TAIL_3
};

/**
  * @brief  以小端格式写入 32 位整数。
  * @param  buffer: 输出缓冲区，长度至少 4 字节。
  * @param  value: 待写入数值。
  * @retval None
  */
static void HostComm_WriteU32LE(uint8_t *buffer, uint32_t value)
{
    buffer[0U] = (uint8_t)(value & 0xFFU);
    buffer[1U] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[2U] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[3U] = (uint8_t)((value >> 24U) & 0xFFU);
}

/**
  * @brief  以小端格式读取 32 位整数。
  * @param  buffer: 输入缓冲区，长度至少 4 字节。
  * @retval 解析后的 32 位数值。
  */
static uint32_t HostComm_ReadU32LE(const uint8_t *buffer)
{
    return ((uint32_t)buffer[0U]) |
           ((uint32_t)buffer[1U] << 8U) |
           ((uint32_t)buffer[2U] << 16U) |
           ((uint32_t)buffer[3U] << 24U);
}

/**
  * @brief  根据错误字节数和接收总字节数刷新误码率，单位 ppm。
  * @retval None
  */
static void HostComm_UpdateErrorRate(void)
{
    uint64_t scaled_error;

    if (host_comm_rx_total_bytes == 0U)
    {
        host_comm_error_rate_ppm = 0U;
        return;
    }

    scaled_error = (uint64_t)host_comm_error_bytes * (uint64_t)HOST_COMM_ERROR_RATE_SCALE;
    host_comm_error_rate_ppm = (uint32_t)(scaled_error / host_comm_rx_total_bytes);
}

/**
  * @brief  记录上位机协议解析错误字节。
  * @param  count: 本次错误字节数。
  * @retval None
  */
static void HostComm_RecordError(uint32_t count)
{
    host_comm_error_bytes += count;
    HostComm_UpdateErrorRate();
}

/**
  * @brief  重置协议解析状态机。
  * @retval None
  */
static void HostComm_ResetParser(void)
{
    host_comm_ctx.state = HOST_COMM_PARSE_HEADER;
    host_comm_ctx.command = 0U;
    host_comm_ctx.payload_length = 0U;
}

/**
  * @brief  请求主循环上传一帧通讯统计。
  * @retval None
  */
static void HostComm_RequestStatsUpload(void)
{
    host_comm_stats_upload_pending = 1U;
}

/**
  * @brief  将完整命令保存给主循环处理，避免在 UART 中断里重配外设或阻塞发送。
  * @param  command: 指令字节。
  * @param  payload: 参数区，不包含 4 字节帧尾。
  * @param  payload_length: 参数区长度。
  * @retval None
  */
static void HostComm_QueueFrame(uint8_t command, const uint8_t *payload, uint16_t payload_length)
{
    if (host_comm_command_pending != 0U)
    {
        HostComm_RecordError(payload_length + 2U);
        return;
    }

    if (payload_length > HOST_COMM_MAX_PARAM_LENGTH)
    {
        HostComm_RecordError(payload_length + 2U);
        return;
    }

    if (payload_length > 0U)
    {
        (void)memcpy(host_comm_pending_payload, payload, payload_length);
    }

    host_comm_pending_command = command;
    host_comm_pending_payload_length = payload_length;
    host_comm_command_pending = 1U;
}

/**
  * @brief  判断当前 payload 末尾是否已经匹配完整帧尾。
  * @retval 1 表示匹配，0 表示未匹配。
  */
static uint8_t HostComm_HasTail(void)
{
    uint16_t tail_start;

    if (host_comm_ctx.payload_length < 4U)
    {
        return 0U;
    }

    tail_start = (uint16_t)(host_comm_ctx.payload_length - 4U);
    return (memcmp(&host_comm_ctx.payload[tail_start], host_comm_tail, 4U) == 0) ? 1U : 0U;
}

/**
  * @brief  判断 UART7 波特率是否在本工程允许配置的范围内。
  * @param  baudrate: 待检查波特率。
  * @retval 1 表示有效，0 表示无效。
  */
static uint8_t HostComm_IsValidBaudrate(uint32_t baudrate)
{
    switch (baudrate)
    {
        case 9600U:
        case 19200U:
        case 38400U:
        case 57600U:
        case 115200U:
        case 230400U:
        case 460800U:
        case 921600U:
            return 1U;

        default:
            return 0U;
    }
}

/**
  * @brief  重新配置 UART7 波特率，并恢复接收状态。
  * @param  baudrate: 目标波特率。
  * @retval HAL 状态。
  */
static HAL_StatusTypeDef HostComm_ApplyBaudrate(uint32_t baudrate)
{
    if (HostComm_IsValidBaudrate(baudrate) == 0U)
    {
        HostComm_RecordError(1U);
        return HAL_ERROR;
    }

    (void)HAL_UART_AbortReceive(&huart7);
    if (HAL_UART_DeInit(&huart7) != HAL_OK)
    {
        return HAL_ERROR;
    }

    huart7.Init.BaudRate = baudrate;
    if (HAL_UART_Init(&huart7) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_UARTEx_SetTxFifoThreshold(&huart7, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&huart7, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_UARTEx_DisableFifoMode(&huart7) != HAL_OK)
    {
        return HAL_ERROR;
    }

    host_comm_baudrate = baudrate;
    host_comm_start_receive_status =
        HAL_UART_Receive_IT(&huart7, &host_comm_ctx.rx_byte, 1U);

    return HAL_OK;
}

/**
  * @brief  设置 RS422 串口1/串口3示例运行使能。
  * @param  target: 1 表示串口1，3 表示串口3，0xFF 表示两路一起。
  * @param  enabled: 0 表示关闭，非 0 表示打开。
  * @retval None
  */
static void HostComm_SetRs422Enabled(uint8_t target, uint8_t enabled)
{
    uint8_t state = (enabled != 0U) ? 1U : 0U;

    if ((target == HOST_COMM_TARGET_UART1) || (target == HOST_COMM_TARGET_ALL))
    {
        host_comm_uart1_enabled = state;
    }

    if ((target == HOST_COMM_TARGET_UART3) || (target == HOST_COMM_TARGET_ALL))
    {
        host_comm_uart3_enabled = state;
    }

    if ((target != HOST_COMM_TARGET_UART1) &&
        (target != HOST_COMM_TARGET_UART3) &&
        (target != HOST_COMM_TARGET_ALL))
    {
        HostComm_RecordError(1U);
    }
}

/**
  * @brief  处理一帧完整的上位机命令。
  * @param  command: 指令字节。
  * @param  payload: 参数区，不包含 4 字节帧尾。
  * @param  payload_length: 参数区长度。
  * @retval None
  */
static void HostComm_HandleFrame(uint8_t command, const uint8_t *payload, uint16_t payload_length)
{
    host_comm_last_command = command;

    switch (command)
    {
        case HOST_COMM_CMD_SET_BAUDRATE:
            if (payload_length == 4U)
            {
                (void)HostComm_ApplyBaudrate(HostComm_ReadU32LE(payload));
            }
            else
            {
                HostComm_RecordError(payload_length + 2U);
            }
            HostComm_RequestStatsUpload();
            break;

        case HOST_COMM_CMD_SET_ENABLE:
            if (payload_length == 2U)
            {
                HostComm_SetRs422Enabled(payload[0U], payload[1U]);
            }
            else
            {
                HostComm_RecordError(payload_length + 2U);
            }
            HostComm_RequestStatsUpload();
            break;

        default:
            HostComm_RecordError(payload_length + 2U);
            HostComm_RequestStatsUpload();
            break;
    }
}

/**
  * @brief  将一个新接收字节喂给上位机协议解析状态机。
  * @param  byte: 新接收字节。
  * @retval None
  */
static void HostComm_ParseByte(uint8_t byte)
{
    uint16_t payload_length;

    host_comm_rx_total_bytes++;
    HostComm_UpdateErrorRate();

    switch (host_comm_ctx.state)
    {
        case HOST_COMM_PARSE_HEADER:
            if (byte == HOST_COMM_FRAME_HEADER)
            {
                host_comm_ctx.state = HOST_COMM_PARSE_COMMAND;
            }
            else
            {
                HostComm_RecordError(1U);
            }
            break;

        case HOST_COMM_PARSE_COMMAND:
            host_comm_ctx.command = byte;
            host_comm_ctx.payload_length = 0U;
            host_comm_ctx.state = HOST_COMM_PARSE_PAYLOAD;
            break;

        case HOST_COMM_PARSE_PAYLOAD:
            if (host_comm_ctx.payload_length >= (HOST_COMM_MAX_PARAM_LENGTH + 4U))
            {
                HostComm_RecordError(host_comm_ctx.payload_length);
                HostComm_ResetParser();
                if (byte == HOST_COMM_FRAME_HEADER)
                {
                    host_comm_ctx.state = HOST_COMM_PARSE_COMMAND;
                }
                break;
            }

            host_comm_ctx.payload[host_comm_ctx.payload_length] = byte;
            host_comm_ctx.payload_length++;

            if (HostComm_HasTail() != 0U)
            {
                payload_length = (uint16_t)(host_comm_ctx.payload_length - 4U);
                HostComm_QueueFrame(host_comm_ctx.command, host_comm_ctx.payload, payload_length);
                HostComm_ResetParser();
            }
            break;

        default:
            HostComm_ResetParser();
            break;
    }
}

/**
  * @brief  初始化 UART7 上位机通讯模块。
  * @retval None
  */
void HostComm_Init(void)
{
    host_comm_rx_total_bytes = 0U;
    host_comm_tx_total_bytes = 0U;
    host_comm_error_bytes = 0U;
    host_comm_error_rate_ppm = 0U;
    host_comm_baudrate = huart7.Init.BaudRate;
    host_comm_uart1_enabled = 1U;
    host_comm_uart3_enabled = 1U;
    host_comm_last_command = 0U;
    host_comm_last_tx_status = HAL_OK;
    host_comm_command_pending = 0U;
    host_comm_pending_command = 0U;
    host_comm_pending_payload_length = 0U;
    host_comm_stats_upload_pending = 1U;
    HostComm_ResetParser();
    host_comm_start_receive_status =
        HAL_UART_Receive_IT(&huart7, &host_comm_ctx.rx_byte, 1U);
}

/**
  * @brief  周期上传上位机通讯统计。
  * @retval None
  */
void HostComm_Poll(void)
{
    static uint32_t last_upload_tick = 0U;
    uint32_t now = HAL_GetTick();
    uint8_t pending_command;
    uint16_t pending_payload_length;

    if (host_comm_command_pending != 0U)
    {
        __disable_irq();
        pending_command = host_comm_pending_command;
        pending_payload_length = host_comm_pending_payload_length;
        host_comm_command_pending = 2U;
        __enable_irq();

        HostComm_HandleFrame(pending_command, host_comm_pending_payload, pending_payload_length);

        __disable_irq();
        host_comm_command_pending = 0U;
        __enable_irq();
    }

    if (host_comm_stats_upload_pending != 0U)
    {
        host_comm_stats_upload_pending = 0U;
        (void)HostComm_SendStats();
        last_upload_tick = now;
        return;
    }

    if ((now - last_upload_tick) < HOST_COMM_UPLOAD_INTERVAL_MS)
    {
        return;
    }
    last_upload_tick = now;

    (void)HostComm_SendStats();
}

/**
  * @brief  按上位机协议上传 UART7 通讯统计。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef HostComm_SendStats(void)
{
    uint8_t frame[1U + 1U + 18U + 4U];
    uint16_t index = 0U;

    frame[index++] = HOST_COMM_FRAME_HEADER;
    frame[index++] = (uint8_t)HOST_COMM_CMD_UPLOAD_STATS;
    HostComm_WriteU32LE(&frame[index], host_comm_rx_total_bytes);
    index = (uint16_t)(index + 4U);
    HostComm_WriteU32LE(&frame[index], host_comm_tx_total_bytes);
    index = (uint16_t)(index + 4U);
    HostComm_WriteU32LE(&frame[index], host_comm_error_bytes);
    index = (uint16_t)(index + 4U);
    HostComm_WriteU32LE(&frame[index], host_comm_error_rate_ppm);
    index = (uint16_t)(index + 4U);
    frame[index++] = host_comm_uart1_enabled;
    frame[index++] = host_comm_uart3_enabled;
    HostComm_WriteU32LE(&frame[index], host_comm_baudrate);
    index = (uint16_t)(index + 4U);
    (void)memcpy(&frame[index], host_comm_tail, 4U);
    index = (uint16_t)(index + 4U);

    host_comm_last_tx_status = HAL_UART_Transmit(&huart7, frame, index, 100U);
    if (host_comm_last_tx_status == HAL_OK)
    {
        host_comm_tx_total_bytes += index;
    }

    return host_comm_last_tx_status;
}

/**
  * @brief  获取上位机通讯统计快照。
  * @param  stats: 输出统计结构。
  * @retval None
  */
void HostComm_GetStats(HostComm_Stats *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->rx_total_bytes = host_comm_rx_total_bytes;
    stats->tx_total_bytes = host_comm_tx_total_bytes;
    stats->error_bytes = host_comm_error_bytes;
    stats->error_rate_ppm = host_comm_error_rate_ppm;
    stats->uart1_enabled = host_comm_uart1_enabled;
    stats->uart3_enabled = host_comm_uart3_enabled;
    stats->baudrate = host_comm_baudrate;
}

/**
  * @brief  UART7 接收完成回调入口。
  * @param  huart: HAL UART 句柄。
  * @retval None
  */
void HostComm_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart7)
    {
        return;
    }

    HostComm_ParseByte(host_comm_ctx.rx_byte);
    host_comm_start_receive_status =
        HAL_UART_Receive_IT(&huart7, &host_comm_ctx.rx_byte, 1U);
}

/**
  * @brief  UART7 错误回调入口。
  * @param  huart: HAL UART 句柄。
  * @retval None
  */
void HostComm_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart7)
    {
        return;
    }

    HostComm_RecordError(1U);
    (void)HAL_UART_AbortReceive_IT(&huart7);
}

/**
  * @brief  UART7 接收中止完成回调入口。
  * @param  huart: HAL UART 句柄。
  * @retval None
  */
void HostComm_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart7)
    {
        return;
    }

    host_comm_start_receive_status =
        HAL_UART_Receive_IT(&huart7, &host_comm_ctx.rx_byte, 1U);
}
