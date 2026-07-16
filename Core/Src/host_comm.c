#include "host_comm.h"
#include "rs485_speed_test.h"
#include "rs422.h"
#include "rs422_protocol.h"
#include "usart.h"

#include <string.h>

#define HOST_COMM_BODY_MIN_LENGTH             7U//指令(2)+控制号(4)+控制类型(1)
#define HOST_COMM_TEXT_MIN_PARAM_LENGTH       1U//至少一个字符
#define HOST_COMM_BUTTON_PARAM_LENGTH         2U//控制类型(1)+动作(1)
#define HOST_COMM_BUTTON_KIND_SWITCH          0x01U//开关类型
#define HOST_COMM_BUTTON_ACTION_OFF           0x00U//关闭
#define HOST_COMM_BUTTON_ACTION_ON            0x01U//打开
#define HOST_COMM_UPLOAD_TIMEOUT_MS           100U//上传超时
#define HOST_COMM_UPLOAD_VALUE_DISPLAY_LENGTH 6U//上传值显示长度
#define HOST_COMM_UPLOAD_VALUE_LIMIT          999999U//上传值最大值
#define HOST_COMM_RS422_TEST_INTERVAL_MS      1000U//RS422测试发送间隔
#define HOST_COMM_RS422_TEST_TX_TIMEOUT_MS    200U//RS422测试发送超时
#define HOST_COMM_RS422_TEST_ID               1U//RS422测试发送ID

#define HOST_COMM_CONTROL_BAUD_UART1          0x00010010UL//串口1波特率
#define HOST_COMM_CONTROL_BAUD_UART2          0x00010005UL//串口2波特率
#define HOST_COMM_CONTROL_BAUD_UART3          0x00010015UL//串口3波特率
#define HOST_COMM_CONTROL_BAUD_UART4          0x0001001BUL//串口4波特率
#define HOST_COMM_CONTROL_ENABLE_UART1        0x00010011UL//串口1使能
#define HOST_COMM_CONTROL_ENABLE_UART2        0x00010006UL//串口2使能
#define HOST_COMM_CONTROL_ENABLE_UART3        0x00010016UL//串口3使能
#define HOST_COMM_CONTROL_ENABLE_UART4        0x0001001CUL//串口4使能
#define HOST_COMM_CONTROL_START_TEST          0x0001001FUL//开始测试
#define HOST_COMM_CONTROL_STOP_TEST           0x0001002DUL//停止测试

#define HOST_COMM_UPLOAD_TX_UART1             0x0001000BUL//串口1发送字节
#define HOST_COMM_UPLOAD_TX_UART2             0x00010002UL//串口2发送字节
#define HOST_COMM_UPLOAD_TX_UART3             0x00010013UL//串口3发送字节
#define HOST_COMM_UPLOAD_TX_UART4             0x00010019UL//串口4发送字节
#define HOST_COMM_UPLOAD_RX_UART1             0x0001000CUL//串口1接收字节
#define HOST_COMM_UPLOAD_RX_UART2             0x0001000EUL//串口2接收字节
#define HOST_COMM_UPLOAD_RX_UART3             0x00010017UL//串口3接收字节
#define HOST_COMM_UPLOAD_RX_UART4             0x0001001DUL//串口4接收字节
#define HOST_COMM_UPLOAD_ERR_UART1            0x0001000DUL//串口1错误字节
#define HOST_COMM_UPLOAD_ERR_UART2            0x00010012UL//串口2错误字节
#define HOST_COMM_UPLOAD_ERR_UART3            0x00010018UL//串口3错误字节
#define HOST_COMM_UPLOAD_ERR_UART4            0x0001001EUL//串口4错误字节

typedef enum
{
    HOST_COMM_PARSE_HEADER = 0U,
    HOST_COMM_PARSE_BODY
} HostComm_ParseState;

typedef struct
{
    HostComm_ParseState state;
    uint8_t body[HOST_COMM_MAX_BODY_LENGTH + 4U];
    uint16_t body_length;
    uint8_t rx_byte;
} HostComm_Context;

typedef struct
{
    uint8_t body[HOST_COMM_MAX_BODY_LENGTH];
    uint16_t body_length;
} HostComm_PendingFrame;

typedef struct
{
    uint32_t tx_total;
    uint32_t rx_total;
    uint32_t error_rate_ppm;
} HostComm_PortStats;

static HostComm_Context host_comm_ctx;
static HostComm_PendingFrame host_comm_pending_frame;
static RS422_ProtocolPacket host_comm_rs422_packet[RS422_PORT_COUNT];
static RS422_ProtocolStatus host_comm_rs422_last_status[RS422_PORT_COUNT];
static uint8_t host_comm_rs422_test_data[RS422_PROTOCOL_DATA_LENGTH];
static uint32_t host_comm_last_rs422_test_tick = 0U;
static volatile uint8_t host_comm_frame_pending = 0U;
static volatile uint8_t host_comm_stats_upload_pending = 0U;

volatile uint32_t host_comm_rx_total_bytes = 0U;
volatile uint32_t host_comm_tx_total_bytes = 0U;
volatile uint32_t host_comm_error_bytes = 0U;
volatile uint32_t host_comm_error_rate_ppm = 0U;
volatile uint32_t host_comm_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
volatile uint8_t host_comm_uart1_enabled = 0U;
volatile uint8_t host_comm_uart2_enabled = 0U;
volatile uint8_t host_comm_uart3_enabled = 0U;
volatile uint8_t host_comm_uart4_enabled = 0U;
volatile uint32_t host_comm_uart1_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
volatile uint32_t host_comm_uart2_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
volatile uint32_t host_comm_uart3_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
volatile uint32_t host_comm_uart4_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
volatile uint8_t host_comm_test_running = 0U;
volatile uint16_t host_comm_last_instruction = 0U;
volatile uint32_t host_comm_last_control_id = 0U;
volatile uint32_t host_comm_send_stats_count = 0U;
volatile uint32_t host_comm_upload_frame_count = 0U;
volatile uint32_t host_comm_last_upload_control_id = 0U;
volatile uint32_t host_comm_last_upload_value = 0U;
volatile uint16_t host_comm_last_upload_length = 0U;
volatile HAL_StatusTypeDef host_comm_last_tx_status = HAL_OK;
volatile HAL_StatusTypeDef host_comm_start_receive_status = HAL_OK;

static const uint8_t host_comm_tail[4U] =
{
    HOST_COMM_FRAME_TAIL_0,
    HOST_COMM_FRAME_TAIL_1,
    HOST_COMM_FRAME_TAIL_2,
    HOST_COMM_FRAME_TAIL_3
};

static uint16_t HostComm_ReadU16BE(const uint8_t *buffer)
{
    return (uint16_t)(((uint16_t)buffer[0U] << 8U) | buffer[1U]);
}

static uint32_t HostComm_ReadU32BE(const uint8_t *buffer)
{
    return ((uint32_t)buffer[0U] << 24U) |
           ((uint32_t)buffer[1U] << 16U) |
           ((uint32_t)buffer[2U] << 8U) |
           (uint32_t)buffer[3U];
}

static void HostComm_WriteU16BE(uint8_t *buffer, uint16_t value)
{
    buffer[0U] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[1U] = (uint8_t)(value & 0xFFU);
}

static void HostComm_WriteU32BE(uint8_t *buffer, uint32_t value)
{
    buffer[0U] = (uint8_t)((value >> 24U) & 0xFFU);
    buffer[1U] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[2U] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[3U] = (uint8_t)(value & 0xFFU);
}

static void HostComm_UpdateErrorRate(void)
{
    uint64_t scaled_error;

    if (host_comm_rx_total_bytes == 0U)
    {
        host_comm_error_rate_ppm = 0U;
        return;
    }

    scaled_error = (uint64_t)host_comm_error_bytes *
                   (uint64_t)HOST_COMM_ERROR_RATE_SCALE;
    host_comm_error_rate_ppm = (uint32_t)(scaled_error / host_comm_rx_total_bytes);
}

static void HostComm_RecordError(uint32_t count)
{
    host_comm_error_bytes += count;
    HostComm_UpdateErrorRate();
}

static void HostComm_ResetParser(void)
{
    host_comm_ctx.state = HOST_COMM_PARSE_HEADER;
    host_comm_ctx.body_length = 0U;
}

static void HostComm_RequestStatsUpload(void)
{
    host_comm_stats_upload_pending = 1U;
}

static void HostComm_InitRs422TestData(void)
{
    uint16_t index;

    for (index = 0U; index < RS422_PROTOCOL_DATA_LENGTH; index++)
    {
        host_comm_rs422_test_data[index] = (uint8_t)index;
    }
}

#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS422)
static void HostComm_ResetRs422TestStats(void)
{
    RS422_ProtocolClearStats(RS422_PORT_U9);
    RS422_ProtocolClearStats(RS422_PORT_U10);
    RS422_ClearRx(RS422_PORT_U9);
    RS422_ClearRx(RS422_PORT_U10);
    rs422_diag_tx_complete_count[RS422_PORT_U9] = 0U;
    rs422_diag_tx_complete_count[RS422_PORT_U10] = 0U;
}
#endif

static uint8_t HostComm_HasTail(void)
{
    uint16_t tail_start;

    if (host_comm_ctx.body_length < 4U)
    {
        return 0U;
    }

    tail_start = (uint16_t)(host_comm_ctx.body_length - 4U);
    return (memcmp(&host_comm_ctx.body[tail_start], host_comm_tail, 4U) == 0) ? 1U : 0U;
}

static void HostComm_QueueFrame(const uint8_t *body, uint16_t body_length)
{
    if (host_comm_frame_pending != 0U)
    {
        HostComm_RecordError((uint32_t)body_length + 5U);
        return;
    }

    if (body_length > HOST_COMM_MAX_BODY_LENGTH)
    {
        HostComm_RecordError((uint32_t)body_length + 5U);
        return;
    }

    if (body_length > 0U)
    {
        (void)memcpy(host_comm_pending_frame.body, body, body_length);
    }
    host_comm_pending_frame.body_length = body_length;
    host_comm_frame_pending = 1U;
}

static uint8_t HostComm_IsValidBaudrate(uint32_t baudrate)
{
    switch (baudrate)
    {
        case 1200U:
        case 2400U:
        case 4800U:
        case 9600U:
        case 19200U:
        case 38400U:
        case 57600U:
        case 115200U:
        case 230400U:
        case 460800U:
        case 921600U:
        case 1000000U:
        case 2000000U:
            return 1U;

        default:
            return 0U;
    }
}

static void HostComm_StoreBaudrate(uint8_t port, uint32_t baudrate)
{
    switch (port)
    {
        case 1U:
            host_comm_uart1_baudrate = baudrate;
            break;

        case 2U:
            host_comm_uart2_baudrate = baudrate;
            break;

        case 3U:
            host_comm_uart3_baudrate = baudrate;
            break;

        case 4U:
            host_comm_uart4_baudrate = baudrate;
            break;

        default:
            break;
    }
}

static HAL_StatusTypeDef HostComm_ApplyRs422Baudrate(RS422_PortId port, uint32_t baudrate)
{
    if (HostComm_IsValidBaudrate(baudrate) == 0U)
    {
        return HAL_ERROR;
    }

    return RS422_SetBaudRate(port, baudrate);
}

static uint8_t HostComm_ControlIdToPort(uint32_t control_id, uint8_t for_baudrate)
{
    if (for_baudrate != 0U)
    {
        switch (control_id)
        {
            case HOST_COMM_CONTROL_BAUD_UART1:
                return 1U;
            case HOST_COMM_CONTROL_BAUD_UART2:
                return 2U;
            case HOST_COMM_CONTROL_BAUD_UART3:
                return 3U;
            case HOST_COMM_CONTROL_BAUD_UART4:
                return 4U;
            default:
                return 0U;
        }
    }

    switch (control_id)
    {
        case HOST_COMM_CONTROL_ENABLE_UART1:
            return 1U;
        case HOST_COMM_CONTROL_ENABLE_UART2:
            return 2U;
        case HOST_COMM_CONTROL_ENABLE_UART3:
            return 3U;
        case HOST_COMM_CONTROL_ENABLE_UART4:
            return 4U;
        default:
            return 0U;
    }
}

static uint8_t HostComm_ParseAsciiU32(const uint8_t *data,
                                      uint16_t length,
                                      uint32_t *value)
{
    uint16_t index;
    uint32_t parsed = 0U;
    uint8_t saw_digit = 0U;

    if ((data == NULL) || (value == NULL))
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        if (data[index] == 0x00U)
        {
            break;
        }
        if ((data[index] < (uint8_t)'0') || (data[index] > (uint8_t)'9'))
        {
            break;
        }
        parsed = (parsed * 10U) + (uint32_t)(data[index] - (uint8_t)'0');
        saw_digit = 1U;
    }

    if (saw_digit == 0U)
    {
        return 0U;
    }

    *value = parsed;
    return 1U;
}

static uint16_t HostComm_FormatU32(uint32_t value, uint8_t *buffer)
{
    uint16_t index;
    uint32_t divisor = 100000U;

    if (buffer == NULL)
    {
        return 0U;
    }

    if (value > HOST_COMM_UPLOAD_VALUE_LIMIT)
    {
        value = HOST_COMM_UPLOAD_VALUE_LIMIT;
    }

    for (index = 0U; index < HOST_COMM_UPLOAD_VALUE_DISPLAY_LENGTH; index++)
    {
        buffer[index] = (uint8_t)((value / divisor) + (uint32_t)'0');
        value %= divisor;
        divisor /= 10U;
    }

    return HOST_COMM_UPLOAD_VALUE_DISPLAY_LENGTH;
}

static void HostComm_SetPortEnabled(uint8_t port, uint8_t enabled)
{
    uint8_t state = (enabled != 0U) ? 1U : 0U;

    switch (port)
    {
        case 1U:
            host_comm_uart1_enabled = state;
            break;

        case 2U:
            host_comm_uart2_enabled = state;
            break;

        case 3U:
            host_comm_uart3_enabled = state;
            break;

        case 4U:
            host_comm_uart4_enabled = state;
            break;

        default:
            HostComm_RecordError(1U);
            break;
    }

}

#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
static uint32_t HostComm_GetRs485Baudrate(uint8_t port)
{
    if (port == 1U)
    {
        return (host_comm_uart1_baudrate != 0U) ?
               host_comm_uart1_baudrate : RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    }

    if (port == 3U)
    {
        return (host_comm_uart3_baudrate != 0U) ?
               host_comm_uart3_baudrate : RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    }

    return RS485_SPEED_TEST_DEFAULT_BAUDRATE;
}
#endif

static void HostComm_StartTest(void)
{
#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
    RS485_SpeedTest_StopAll();
    RS485_SpeedTest_SetRole(RS485_SPEED_TEST_ROLE_MASTER);
    host_comm_test_running = 1U;

    if (host_comm_uart1_enabled != 0U)
    {
        RS485_SpeedTest_Start(RS485_SPEED_TEST_INSTANCE_UART1,
                              HostComm_GetRs485Baudrate(1U));
    }

    if (host_comm_uart3_enabled != 0U)
    {
        RS485_SpeedTest_Start(RS485_SPEED_TEST_INSTANCE_UART3,
                              HostComm_GetRs485Baudrate(3U));
    }
#else
    HostComm_ResetRs422TestStats();
    host_comm_test_running = 1U;
    host_comm_last_rs422_test_tick =
        HAL_GetTick() - HOST_COMM_RS422_TEST_INTERVAL_MS;
#endif
}

static void HostComm_StopTest(void)
{
#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
    RS485_SpeedTest_StopAll();
#endif
    host_comm_test_running = 0U;
}

static void HostComm_HandleTextControl(uint32_t control_id,
                                       const uint8_t *param,
                                       uint16_t param_length)
{
    uint8_t port;
    uint32_t baudrate;

    port = HostComm_ControlIdToPort(control_id, 1U);
    if ((port == 0U) || (param_length < HOST_COMM_TEXT_MIN_PARAM_LENGTH))
    {
        HostComm_RecordError((uint32_t)param_length + 7U);
        return;
    }

    if (HostComm_ParseAsciiU32(param, param_length, &baudrate) == 0U)
    {
        HostComm_RecordError((uint32_t)param_length + 7U);
        return;
    }

    if (HostComm_IsValidBaudrate(baudrate) == 0U)
    {
        HostComm_RecordError((uint32_t)param_length + 7U);
        return;
    }

    HostComm_StoreBaudrate(port, baudrate);
    if (port == 1U)
    {
        (void)HostComm_ApplyRs422Baudrate(RS422_PORT_U9, baudrate);
    }
    else if (port == 3U)
    {
        (void)HostComm_ApplyRs422Baudrate(RS422_PORT_U10, baudrate);
    }
}

static void HostComm_HandleButtonControl(uint32_t control_id,
                                         const uint8_t *param,
                                         uint16_t param_length)
{
    uint8_t port;

    if ((param == NULL) ||
        (param_length != HOST_COMM_BUTTON_PARAM_LENGTH) ||
        (param[0U] != HOST_COMM_BUTTON_KIND_SWITCH))
    {
        HostComm_RecordError((uint32_t)param_length + 7U);
        return;
    }

    if (control_id == HOST_COMM_CONTROL_START_TEST)
    {
        if (param[1U] == HOST_COMM_BUTTON_ACTION_OFF)
        {
            HostComm_StartTest();
        }
        else
        {
            HostComm_RecordError((uint32_t)param_length + 7U);
        }
        return;
    }

    if (control_id == HOST_COMM_CONTROL_STOP_TEST)
    {
        if (param[1U] == HOST_COMM_BUTTON_ACTION_OFF)
        {
            HostComm_StopTest();
        }
        else
        {
            HostComm_RecordError((uint32_t)param_length + 7U);
        }
        return;
    }

    port = HostComm_ControlIdToPort(control_id, 0U);
    if ((port == 0U) ||
        ((param[1U] != HOST_COMM_BUTTON_ACTION_OFF) &&
         (param[1U] != HOST_COMM_BUTTON_ACTION_ON)))
    {
        HostComm_RecordError((uint32_t)param_length + 7U);
        return;
    }

    HostComm_SetPortEnabled(port, param[1U]);
}

static void HostComm_HandleFrame(const uint8_t *body, uint16_t body_length)
{
    uint16_t instruction;
    uint32_t control_id;
    uint8_t control_type;
    const uint8_t *param;
    uint16_t param_length;

    if ((body == NULL) || (body_length < HOST_COMM_BODY_MIN_LENGTH))
    {
        HostComm_RecordError((uint32_t)body_length + 5U);
        HostComm_RequestStatsUpload();
        return;
    }

    instruction = HostComm_ReadU16BE(&body[0U]);
    control_id = HostComm_ReadU32BE(&body[2U]);
    control_type = body[6U];
    param = &body[7U];
    param_length = (uint16_t)(body_length - HOST_COMM_BODY_MIN_LENGTH);
    host_comm_last_instruction = instruction;
    host_comm_last_control_id = control_id;

    if (instruction != HOST_COMM_DOWNLOAD_INSTRUCTION)
    {
        HostComm_RecordError((uint32_t)body_length + 5U);
        HostComm_RequestStatsUpload();
        return;
    }

    if (control_type == HOST_COMM_CONTROL_TYPE_TEXT)
    {
        HostComm_HandleTextControl(control_id, param, param_length);
    }
    else if (control_type == HOST_COMM_CONTROL_TYPE_BUTTON)
    {
        HostComm_HandleButtonControl(control_id, param, param_length);
    }
    else
    {
        HostComm_RecordError((uint32_t)body_length + 5U);
    }

    HostComm_RequestStatsUpload();
}

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
                host_comm_ctx.body_length = 0U;
                host_comm_ctx.state = HOST_COMM_PARSE_BODY;
            }
            else
            {
                HostComm_RecordError(1U);
            }
            break;

        case HOST_COMM_PARSE_BODY:
            if (host_comm_ctx.body_length >= (HOST_COMM_MAX_BODY_LENGTH + 4U))
            {
                HostComm_RecordError((uint32_t)host_comm_ctx.body_length + 1U);
                HostComm_ResetParser();
                if (byte == HOST_COMM_FRAME_HEADER)
                {
                    host_comm_ctx.state = HOST_COMM_PARSE_BODY;
                }
                break;
            }

            host_comm_ctx.body[host_comm_ctx.body_length] = byte;
            host_comm_ctx.body_length++;

            if (HostComm_HasTail() != 0U)
            {
                payload_length = (uint16_t)(host_comm_ctx.body_length - 4U);
                HostComm_QueueFrame(host_comm_ctx.body, payload_length);
                HostComm_ResetParser();
            }
            break;

        default:
            HostComm_ResetParser();
            break;
    }
}

#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS422)
static void HostComm_PollRs422Port(RS422_PortId rs422_port)
{
    RS422_ProtocolStatus status;

    if (rs422_port == RS422_PORT_COUNT)
    {
        return;
    }

    do
    {
        status = RS422_ProtocolPoll(rs422_port, &host_comm_rs422_packet[rs422_port]);
        if (status != RS422_PROTOCOL_STATUS_NO_FRAME)
        {
            host_comm_rs422_last_status[rs422_port] = status;
        }
    } while ((status != RS422_PROTOCOL_STATUS_NO_FRAME) &&
             (RS422_Available(rs422_port) > 0U));
}
#endif

#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS422)
static void HostComm_PollRs422Ports(void)
{
    HostComm_PollRs422Port(RS422_PORT_U9);
    HostComm_PollRs422Port(RS422_PORT_U10);
}
#endif

#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS422)
static void HostComm_RunRs422Test(uint32_t now)
{
    if (host_comm_test_running == 0U)
    {
        return;
    }

    if ((now - host_comm_last_rs422_test_tick) < HOST_COMM_RS422_TEST_INTERVAL_MS)
    {
        return;
    }
    host_comm_last_rs422_test_tick = now;

    if (host_comm_uart1_enabled != 0U)
    {
        (void)RS422_ProtocolSend(RS422_PORT_U9,
                                 HOST_COMM_RS422_TEST_ID,
                                 (uint8_t)RS422_PROTOCOL_TYPE_55,
                                 host_comm_rs422_test_data,
                                 HOST_COMM_RS422_TEST_TX_TIMEOUT_MS);
    }

    if (host_comm_uart3_enabled != 0U)
    {
        (void)RS422_ProtocolSend(RS422_PORT_U10,
                                 HOST_COMM_RS422_TEST_ID,
                                 (uint8_t)RS422_PROTOCOL_TYPE_55,
                                 host_comm_rs422_test_data,
                                 HOST_COMM_RS422_TEST_TX_TIMEOUT_MS);
    }
}
#endif

static void HostComm_RunSelectedTest(uint32_t now)
{
#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
    (void)now;
    RS485_SpeedTest_Run();
#else
    HostComm_PollRs422Ports();
    HostComm_RunRs422Test(now);
#endif
}

static void HostComm_CollectPortStats(uint8_t port, HostComm_PortStats *stats)
{
#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
    RS485_SpeedTestStats rs485_stats;
#endif

    if (stats == NULL)
    {
        return;
    }

    stats->tx_total = 0U;
    stats->rx_total = 0U;
    stats->error_rate_ppm = 0U;

#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
    if (port == 1U)
    {
        RS485_SpeedTest_GetStats(RS485_SPEED_TEST_INSTANCE_UART1, &rs485_stats);
        stats->tx_total = rs485_stats.tx_bytes;
        stats->rx_total = rs485_stats.rx_bytes;
        stats->error_rate_ppm = rs485_stats.error_rate_ppm;
    }
    else if (port == 3U)
    {
        RS485_SpeedTest_GetStats(RS485_SPEED_TEST_INSTANCE_UART3, &rs485_stats);
        stats->tx_total = rs485_stats.tx_bytes;
        stats->rx_total = rs485_stats.rx_bytes;
        stats->error_rate_ppm = rs485_stats.error_rate_ppm;
    }
#else
    if (port == 1U)
    {
        HostComm_PollRs422Port(RS422_PORT_U9);
    }
    else if (port == 3U)
    {
        HostComm_PollRs422Port(RS422_PORT_U10);
    }

    if (port == 1U)
    {
        stats->tx_total = rs422_diag_tx_complete_count[RS422_PORT_U9];
        stats->rx_total = rs422_protocol_rx_total_bytes[RS422_PORT_U9];
        stats->error_rate_ppm = rs422_protocol_rx_error_rate_ppm[RS422_PORT_U9];
    }
    else if (port == 3U)
    {
        stats->tx_total = rs422_diag_tx_complete_count[RS422_PORT_U10];
        stats->rx_total = rs422_protocol_rx_total_bytes[RS422_PORT_U10];
        stats->error_rate_ppm = rs422_protocol_rx_error_rate_ppm[RS422_PORT_U10];
    }
#endif
}

static HAL_StatusTypeDef HostComm_SendValue(uint32_t control_id, uint32_t value)
{
    uint8_t frame[1U + 2U + 4U + HOST_COMM_UPLOAD_VALUE_DISPLAY_LENGTH + 4U];
    uint16_t index = 0U;
    uint16_t value_length;

    frame[index++] = HOST_COMM_FRAME_HEADER;
    HostComm_WriteU16BE(&frame[index], HOST_COMM_UPLOAD_INSTRUCTION);
    index = (uint16_t)(index + 2U);
    HostComm_WriteU32BE(&frame[index], control_id);
    index = (uint16_t)(index + 4U);
    value_length = HostComm_FormatU32(value, &frame[index]);
    index = (uint16_t)(index + value_length);
    (void)memcpy(&frame[index], host_comm_tail, 4U);
    index = (uint16_t)(index + 4U);

    host_comm_upload_frame_count++;
    host_comm_last_upload_control_id = control_id;
    host_comm_last_upload_value = value;
    host_comm_last_upload_length = index;
    host_comm_last_tx_status = HAL_UART_Transmit(&huart7,
                                                 frame,
                                                 index,
                                                 HOST_COMM_UPLOAD_TIMEOUT_MS);
    if (host_comm_last_tx_status == HAL_OK)
    {
        host_comm_tx_total_bytes += index;
    }

    return host_comm_last_tx_status;
}

static HAL_StatusTypeDef HostComm_SendPortStats(uint8_t port,
                                                uint32_t tx_control_id,
                                                uint32_t rx_control_id,
                                                uint32_t error_control_id)
{
    HostComm_PortStats stats;
    HAL_StatusTypeDef status;

    HostComm_CollectPortStats(port, &stats);

    status = HostComm_SendValue(tx_control_id, stats.tx_total);
    if (status != HAL_OK)
    {
        return status;
    }
    status = HostComm_SendValue(rx_control_id, stats.rx_total);
    if (status != HAL_OK)
    {
        return status;
    }

    return HostComm_SendValue(error_control_id, stats.error_rate_ppm);
}

void HostComm_Init(void)
{
    host_comm_rx_total_bytes = 0U;
    host_comm_tx_total_bytes = 0U;
    host_comm_error_bytes = 0U;
    host_comm_error_rate_ppm = 0U;
    host_comm_baudrate = huart7.Init.BaudRate;
    host_comm_uart1_enabled = 0U;
    host_comm_uart2_enabled = 0U;
    host_comm_uart3_enabled = 0U;
    host_comm_uart4_enabled = 0U;
#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
    host_comm_uart1_baudrate = RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    host_comm_uart2_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
    host_comm_uart3_baudrate = RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    host_comm_uart4_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
#else
    host_comm_uart1_baudrate = huart1.Init.BaudRate;
    host_comm_uart2_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
    host_comm_uart3_baudrate = huart3.Init.BaudRate;
    host_comm_uart4_baudrate = HOST_COMM_DEFAULT_BAUDRATE;
#endif
    host_comm_test_running = 0U;
    host_comm_last_instruction = 0U;
    host_comm_last_control_id = 0U;
    host_comm_send_stats_count = 0U;
    host_comm_upload_frame_count = 0U;
    host_comm_last_upload_control_id = 0U;
    host_comm_last_upload_value = 0U;
    host_comm_last_upload_length = 0U;
    host_comm_last_tx_status = HAL_OK;
    host_comm_frame_pending = 0U;
    host_comm_pending_frame.body_length = 0U;
    (void)memset(host_comm_rs422_packet, 0, sizeof(host_comm_rs422_packet));
    (void)memset(host_comm_rs422_last_status, 0, sizeof(host_comm_rs422_last_status));
    HostComm_InitRs422TestData();
#if (HOST_COMM_TEST_MODE_DEFAULT == HOST_COMM_TEST_MODE_RS485)
    RS485_SpeedTest_Init();
#endif
    host_comm_last_rs422_test_tick = 0U;
    host_comm_stats_upload_pending = 1U;
    HostComm_ResetParser();
    host_comm_start_receive_status =
        HAL_UART_Receive_IT(&huart7, &host_comm_ctx.rx_byte, 1U);
}

void HostComm_Poll(void)
{
    static uint32_t last_upload_tick = 0U;
    uint32_t now = HAL_GetTick();
    uint16_t pending_body_length;

    if (host_comm_frame_pending != 0U)
    {
        __disable_irq();
        pending_body_length = host_comm_pending_frame.body_length;
        host_comm_frame_pending = 2U;
        __enable_irq();

        HostComm_HandleFrame(host_comm_pending_frame.body, pending_body_length);

        __disable_irq();
        host_comm_frame_pending = 0U;
        __enable_irq();
    }

    HostComm_RunSelectedTest(now);

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

HAL_StatusTypeDef HostComm_SendStats(void)
{
    HAL_StatusTypeDef status;

    host_comm_send_stats_count++;

    status = HostComm_SendPortStats(1U,
                                    HOST_COMM_UPLOAD_TX_UART1,
                                    HOST_COMM_UPLOAD_RX_UART1,
                                    HOST_COMM_UPLOAD_ERR_UART1);
    if (status != HAL_OK)
    {
        return status;
    }
    status = HostComm_SendPortStats(2U,
                                    HOST_COMM_UPLOAD_TX_UART2,
                                    HOST_COMM_UPLOAD_RX_UART2,
                                    HOST_COMM_UPLOAD_ERR_UART2);
    if (status != HAL_OK)
    {
        return status;
    }
    status = HostComm_SendPortStats(3U,
                                    HOST_COMM_UPLOAD_TX_UART3,
                                    HOST_COMM_UPLOAD_RX_UART3,
                                    HOST_COMM_UPLOAD_ERR_UART3);
    if (status != HAL_OK)
    {
        return status;
    }

    return HostComm_SendPortStats(4U,
                                  HOST_COMM_UPLOAD_TX_UART4,
                                  HOST_COMM_UPLOAD_RX_UART4,
                                  HOST_COMM_UPLOAD_ERR_UART4);
}

void HostComm_GetStats(HostComm_Stats *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->screen_rx_total_bytes = host_comm_rx_total_bytes;
    stats->screen_tx_total_bytes = host_comm_tx_total_bytes;
    stats->screen_error_bytes = host_comm_error_bytes;
    stats->screen_error_rate_ppm = host_comm_error_rate_ppm;
    stats->uart_enabled[0U] = host_comm_uart1_enabled;
    stats->uart_enabled[1U] = host_comm_uart2_enabled;
    stats->uart_enabled[2U] = host_comm_uart3_enabled;
    stats->uart_enabled[3U] = host_comm_uart4_enabled;
    stats->uart_baudrate[0U] = host_comm_uart1_baudrate;
    stats->uart_baudrate[1U] = host_comm_uart2_baudrate;
    stats->uart_baudrate[2U] = host_comm_uart3_baudrate;
    stats->uart_baudrate[3U] = host_comm_uart4_baudrate;
    stats->test_running = host_comm_test_running;
}

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

void HostComm_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart7)
    {
        return;
    }

    HostComm_RecordError(1U);
    (void)HAL_UART_AbortReceive_IT(&huart7);
}

void HostComm_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart7)
    {
        return;
    }

    host_comm_start_receive_status =
        HAL_UART_Receive_IT(&huart7, &host_comm_ctx.rx_byte, 1U);
}
