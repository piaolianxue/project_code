#include "rs485_speed_test.h"
#include "rs485_speed_test_wire.h"

#include <string.h>

typedef struct
{
    RS422_PortId port;
    uint8_t wire_index;
} RS485_SpeedTestConfig;

typedef struct
{
    uint8_t active;
    RS485_SpeedTestState state;
    uint32_t baudrate;
    uint32_t actual_baudrate;
    uint32_t sequence;
    uint32_t wait_sequence;
    uint32_t last_rx_sequence;
    uint32_t wait_deadline_tick;
    uint8_t waiting_echo;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t echo_frames;
    uint32_t timeout_count;
    uint32_t protocol_error_count;
    uint32_t app_error_count;
    uint32_t last_status;
    uint32_t last_hal_status;
    RS422_ProtocolPacket packet;
    RS422_ProtocolStats protocol_stats;
} RS485_SpeedTestInstance;

static const RS485_SpeedTestConfig rs485_speed_test_config[RS485_SPEED_TEST_INSTANCE_COUNT] =
{
    { RS422_PORT_U9, 0U },
    { RS422_PORT_U10, 0U }
};

static RS485_SpeedTestInstance rs485_speed_test_instance[RS485_SPEED_TEST_INSTANCE_COUNT];

static uint8_t RS485_SpeedTestIsValidInstance(RS485_SpeedTestInstanceId instance)
{
    return ((uint32_t)instance < (uint32_t)RS485_SPEED_TEST_INSTANCE_COUNT) ? 1U : 0U;
}

static uint32_t RS485_GetU32(const uint8_t *buffer, uint16_t offset)
{
    return ((uint32_t)buffer[offset + 0U]) |
           ((uint32_t)buffer[offset + 1U] << 8U) |
           ((uint32_t)buffer[offset + 2U] << 16U) |
           ((uint32_t)buffer[offset + 3U] << 24U);
}

static void RS485_PutU32(uint8_t *buffer, uint16_t offset, uint32_t value)
{
    buffer[offset + 0U] = (uint8_t)(value & 0xFFU);
    buffer[offset + 1U] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[offset + 2U] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[offset + 3U] = (uint8_t)((value >> 24U) & 0xFFU);
}

static void RS485_SpeedTestBuildPayload(RS485_SpeedTestInstanceId instance,
                                        uint8_t command,
                                        uint32_t sequence,
                                        uint8_t data[RS422_PROTOCOL_DATA_LENGTH])
{
    uint16_t offset;
    RS485_SpeedTestInstance *item = &rs485_speed_test_instance[instance];

    (void)memset(data, 0, RS422_PROTOCOL_DATA_LENGTH);
    data[RS485_SPEED_PAYLOAD_MAGIC0_OFFSET] = RS485_SPEED_PAYLOAD_MAGIC0;
    data[RS485_SPEED_PAYLOAD_MAGIC1_OFFSET] = RS485_SPEED_PAYLOAD_MAGIC1;
    data[RS485_SPEED_PAYLOAD_VERSION_OFFSET] = RS485_SPEED_PAYLOAD_VERSION;
    data[RS485_SPEED_PAYLOAD_COMMAND_OFFSET] = command;
    data[RS485_SPEED_PAYLOAD_INDEX_OFFSET] = rs485_speed_test_config[instance].wire_index;
    data[RS485_SPEED_PAYLOAD_ROLE_OFFSET] = 0U;
    data[RS485_SPEED_PAYLOAD_FLAGS_OFFSET] = 0U;
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_BAUD_OFFSET, item->baudrate);
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_SEQUENCE_OFFSET, sequence);
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_TICK_OFFSET, HAL_GetTick());
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_ACTUAL_BAUD_OFFSET, item->actual_baudrate);

    for (offset = RS485_SPEED_PAYLOAD_PATTERN_OFFSET;
         offset < RS422_PROTOCOL_DATA_LENGTH;
         offset++)
    {
        data[offset] = (uint8_t)(sequence + offset);
    }
}

static uint8_t RS485_SpeedTestPayloadHeaderIsValid(const uint8_t data[RS422_PROTOCOL_DATA_LENGTH])
{
    if ((data[RS485_SPEED_PAYLOAD_MAGIC0_OFFSET] != RS485_SPEED_PAYLOAD_MAGIC0) ||
        (data[RS485_SPEED_PAYLOAD_MAGIC1_OFFSET] != RS485_SPEED_PAYLOAD_MAGIC1) ||
        (data[RS485_SPEED_PAYLOAD_VERSION_OFFSET] != RS485_SPEED_PAYLOAD_VERSION))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t RS485_SpeedTestPatternIsValid(const uint8_t data[RS422_PROTOCOL_DATA_LENGTH],
                                             uint32_t sequence)
{
    uint16_t offset;

    for (offset = RS485_SPEED_PAYLOAD_PATTERN_OFFSET;
         offset < RS422_PROTOCOL_DATA_LENGTH;
         offset++)
    {
        if (data[offset] != (uint8_t)(sequence + offset))
        {
            return 0U;
        }
    }

    return 1U;
}

static void RS485_SpeedTestClearInstance(RS485_SpeedTestInstanceId instance)
{
    RS485_SpeedTestInstance *item;
    RS422_PortId port;

    if (RS485_SpeedTestIsValidInstance(instance) == 0U)
    {
        return;
    }

    item = &rs485_speed_test_instance[instance];
    port = rs485_speed_test_config[instance].port;

    RS422_ClearRx(port);
    RS422_ClearTx(port);
    RS422_ProtocolClearStats(port);
    rs422_diag_tx_complete_count[port] = 0U;
    rs422_diag_rx_complete_count[port] = 0U;
    rs422_diag_error_count[port] = 0U;
    rs422_diag_abort_count[port] = 0U;
    rs422_diag_last_error_code[port] = 0U;
    rs422_diag_last_rx_byte[port] = 0U;
    rs422_diag_start_receive_status[port] = 0U;
    rs422_diag_rx_overflow_count[port] = 0U;
    rs422_diag_tx_overflow_count[port] = 0U;
    rs422_diag_tx_total_bytes[port] = 0U;
    rs422_diag_requested_baud[port] = 0U;
    rs422_diag_actual_baud[port] = 0U;

    (void)memset(item, 0, sizeof(*item));
    item->state = RS485_SPEED_TEST_STATE_IDLE;
    item->baudrate = RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    item->actual_baudrate = RS422_GetActualBaudRate(port);
    item->last_status = (uint32_t)RS422_PROTOCOL_STATUS_NO_FRAME;
    item->last_hal_status = (uint32_t)HAL_OK;
}

static void RS485_SpeedTestHandlePacket(RS485_SpeedTestInstanceId instance,
                                        const RS422_ProtocolPacket *packet)
{
    RS485_SpeedTestInstance *item;
    const uint8_t *data;
    uint8_t command;
    uint8_t index;
    uint32_t sequence;

    if ((RS485_SpeedTestIsValidInstance(instance) == 0U) || (packet == NULL))
    {
        return;
    }

    item = &rs485_speed_test_instance[instance];
    data = packet->data;

    if ((packet->id != RS485_SPEED_TEST_NODE_ID) ||
        (packet->data_type != RS485_SPEED_TEST_DATA_TYPE))
    {
        item->app_error_count++;
        return;
    }

    if (RS485_SpeedTestPayloadHeaderIsValid(data) == 0U)
    {
        item->app_error_count++;
        return;
    }

    command = data[RS485_SPEED_PAYLOAD_COMMAND_OFFSET];
    index = data[RS485_SPEED_PAYLOAD_INDEX_OFFSET];
    sequence = RS485_GetU32(data, RS485_SPEED_PAYLOAD_SEQUENCE_OFFSET);

    if (RS485_SpeedTestPatternIsValid(data, sequence) == 0U)
    {
        item->app_error_count++;
        return;
    }

    item->rx_frames++;
    item->last_rx_sequence = sequence;

    if (command != (uint8_t)RS485_SPEED_COMMAND_ECHO)
    {
        item->app_error_count++;
        return;
    }

    if ((item->waiting_echo != 0U) &&
        (sequence == item->wait_sequence) &&
        (index == rs485_speed_test_config[instance].wire_index))
    {
        item->echo_frames++;
        item->waiting_echo = 0U;
    }
    else
    {
        item->app_error_count++;
    }
}

static void RS485_SpeedTestPollInstance(RS485_SpeedTestInstanceId instance)
{
    RS485_SpeedTestInstance *item;
    RS422_PortId port;
    RS422_ProtocolStatus status;
    uint8_t guard;

    if (RS485_SpeedTestIsValidInstance(instance) == 0U)
    {
        return;
    }

    item = &rs485_speed_test_instance[instance];
    port = rs485_speed_test_config[instance].port;
    guard = 0U;

    do
    {
        status = RS422_ProtocolPoll(port, &item->packet);
        item->last_status = (uint32_t)status;
        if (status == RS422_PROTOCOL_STATUS_OK)
        {
            RS485_SpeedTestHandlePacket(instance, &item->packet);
        }
        else if (status != RS422_PROTOCOL_STATUS_NO_FRAME)
        {
            item->protocol_error_count++;
        }
        guard++;
    } while ((status != RS422_PROTOCOL_STATUS_NO_FRAME) && (guard < 8U));
}

static void RS485_SpeedTestSendDataIfReady(RS485_SpeedTestInstanceId instance)
{
    RS485_SpeedTestInstance *item;
    RS422_PortId port;
    uint8_t payload[RS422_PROTOCOL_DATA_LENGTH];
    uint32_t sequence;
    HAL_StatusTypeDef status;

    if (RS485_SpeedTestIsValidInstance(instance) == 0U)
    {
        return;
    }

    item = &rs485_speed_test_instance[instance];
    port = rs485_speed_test_config[instance].port;

    if ((item->active == 0U) ||
        (item->state != RS485_SPEED_TEST_STATE_RUNNING) ||
        (item->waiting_echo != 0U) ||
        (RS422_IsTxBusy(port) != 0U))
    {
        return;
    }

    sequence = item->sequence++;
    item->actual_baudrate = RS422_GetActualBaudRate(port);
    RS485_SpeedTestBuildPayload(instance,
                                (uint8_t)RS485_SPEED_COMMAND_DATA,
                                sequence,
                                payload);
    status = RS422_ProtocolSend_IT(port,
                                   RS485_SPEED_TEST_NODE_ID,
                                   RS485_SPEED_TEST_DATA_TYPE,
                                   payload);
    item->last_hal_status = (uint32_t)status;
    if (status == HAL_OK)
    {
        item->tx_frames++;
        item->wait_sequence = sequence;
        item->wait_deadline_tick = HAL_GetTick() + RS485_SPEED_TEST_FRAME_TIMEOUT_MS;
        item->waiting_echo = 1U;
    }
    else
    {
        item->app_error_count++;
    }
}

static void RS485_SpeedTestRefreshStats(RS485_SpeedTestInstanceId instance)
{
    RS485_SpeedTestInstance *item;
    RS422_PortId port;

    if (RS485_SpeedTestIsValidInstance(instance) == 0U)
    {
        return;
    }

    item = &rs485_speed_test_instance[instance];
    port = rs485_speed_test_config[instance].port;

    item->actual_baudrate = RS422_GetActualBaudRate(port);
    RS422_ProtocolGetStats(port, &item->protocol_stats);
}

void RS485_SpeedTest_Init(void)
{
    RS485_SpeedTestInstanceId instance;

    for (instance = (RS485_SpeedTestInstanceId)0;
         instance < RS485_SPEED_TEST_INSTANCE_COUNT;
         instance++)
    {
        RS485_SpeedTestClearInstance(instance);
    }
}

void RS485_SpeedTest_Start(RS485_SpeedTestInstanceId instance, uint32_t baudrate)
{
    RS485_SpeedTestInstance *item;
    RS422_PortId port;
    HAL_StatusTypeDef status;

    if (RS485_SpeedTestIsValidInstance(instance) == 0U)
    {
        return;
    }

    if (baudrate == 0U)
    {
        baudrate = RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    }

    RS485_SpeedTestClearInstance(instance);

    item = &rs485_speed_test_instance[instance];
    port = rs485_speed_test_config[instance].port;
    item->baudrate = baudrate;
    item->actual_baudrate = baudrate;

    status = RS422_SetBaudRate(port, baudrate);
    item->last_hal_status = (uint32_t)status;
    item->actual_baudrate = RS422_GetActualBaudRate(port);
    if (status != HAL_OK)
    {
        item->state = RS485_SPEED_TEST_STATE_ERROR;
        item->app_error_count++;
        return;
    }

    item->active = 1U;
    item->state = RS485_SPEED_TEST_STATE_RUNNING;
    item->sequence = 1U;
    item->last_status = (uint32_t)RS422_PROTOCOL_STATUS_NO_FRAME;
}

void RS485_SpeedTest_Stop(RS485_SpeedTestInstanceId instance)
{
    if (RS485_SpeedTestIsValidInstance(instance) == 0U)
    {
        return;
    }

    rs485_speed_test_instance[instance].active = 0U;
    rs485_speed_test_instance[instance].waiting_echo = 0U;
    if (rs485_speed_test_instance[instance].state == RS485_SPEED_TEST_STATE_RUNNING)
    {
        rs485_speed_test_instance[instance].state = RS485_SPEED_TEST_STATE_STOPPED;
    }
}

void RS485_SpeedTest_StopAll(void)
{
    RS485_SpeedTestInstanceId instance;

    for (instance = (RS485_SpeedTestInstanceId)0;
         instance < RS485_SPEED_TEST_INSTANCE_COUNT;
         instance++)
    {
        RS485_SpeedTest_Stop(instance);
    }
}

void RS485_SpeedTest_Run(void)
{
    RS485_SpeedTestInstanceId instance;
    RS485_SpeedTestInstance *item;
    uint32_t now;

    now = HAL_GetTick();
    for (instance = (RS485_SpeedTestInstanceId)0;
         instance < RS485_SPEED_TEST_INSTANCE_COUNT;
         instance++)
    {
        item = &rs485_speed_test_instance[instance];
        if (item->active != 0U)
        {
            RS485_SpeedTestPollInstance(instance);

            if ((item->waiting_echo != 0U) &&
                ((int32_t)(now - item->wait_deadline_tick) >= 0))
            {
                item->timeout_count++;
                item->app_error_count++;
                item->waiting_echo = 0U;
            }

            RS485_SpeedTestSendDataIfReady(instance);
        }

        RS485_SpeedTestRefreshStats(instance);
    }
}

void RS485_SpeedTest_GetStats(RS485_SpeedTestInstanceId instance,
                              RS485_SpeedTestStats *stats)
{
    RS485_SpeedTestInstance *item;
    RS422_PortId port;

    if ((RS485_SpeedTestIsValidInstance(instance) == 0U) || (stats == NULL))
    {
        return;
    }

    item = &rs485_speed_test_instance[instance];
    port = rs485_speed_test_config[instance].port;
    RS485_SpeedTestRefreshStats(instance);

    stats->active = item->active;
    stats->state = (uint32_t)item->state;
    stats->baudrate = item->baudrate;
    stats->actual_baudrate = item->actual_baudrate;
    stats->tx_bytes = rs422_diag_tx_total_bytes[port];
    stats->rx_bytes = item->protocol_stats.total_bytes;
    stats->error_bytes = item->protocol_stats.error_bytes;
    stats->error_rate_ppm = item->protocol_stats.error_rate_ppm;
    stats->tx_frames = item->tx_frames;
    stats->rx_frames = item->rx_frames;
    stats->echo_frames = item->echo_frames;
    stats->timeout_count = item->timeout_count;
    stats->protocol_error_count = item->protocol_error_count;
    stats->app_error_count = item->app_error_count;
    stats->uart_error_count = rs422_diag_error_count[port];
    stats->rx_overflow_count = rs422_diag_rx_overflow_count[port];
    stats->tx_overflow_count = rs422_diag_tx_overflow_count[port];
    stats->last_sequence = item->sequence;
    stats->last_rx_sequence = item->last_rx_sequence;
    stats->last_status = item->last_status;
    stats->last_hal_status = item->last_hal_status;
}

uint8_t RS485_SpeedTest_IsRunning(RS485_SpeedTestInstanceId instance)
{
    if (RS485_SpeedTestIsValidInstance(instance) == 0U)
    {
        return 0U;
    }

    return rs485_speed_test_instance[instance].active;
}
