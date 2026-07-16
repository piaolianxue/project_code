#include "rs485_speed_test.h"
#include "rs485_speed_test_wire.h"

#include <string.h>

#define RS485_SPEED_TEST_PORT               RS422_PORT_U9

typedef struct
{
    uint8_t enabled;
    uint8_t slave_id;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t echo_frames;
    uint32_t timeout_count;
    uint32_t protocol_error_count;
    uint32_t app_error_count;
    uint32_t last_rx_sequence;
    uint32_t last_status;
    uint32_t last_hal_status;
} RS485_SpeedTestSlaveStats;

const uint32_t rs485_speed_test_baud_table[RS485_SPEED_TEST_BAUD_COUNT] =
{
    RS485_SPEED_TEST_DEFAULT_BAUDRATE
};

const uint8_t rs485_speed_test_master_slave_table[RS485_SPEED_TEST_MASTER_SLAVE_COUNT] =
{
    1U,
    2U
};

volatile uint32_t rs485_speed_test_local_node_id = RS485_SPEED_TEST_LOCAL_NODE_ID;
volatile uint32_t rs485_speed_test_current_slave_id = 1U;
volatile uint32_t rs485_speed_test_current_slave_index = 0U;
volatile uint32_t rs485_speed_test_ignored_node_frames = 0U;
volatile uint32_t rs485_speed_test_role = (uint32_t)RS485_SPEED_TEST_DEFAULT_ROLE;
volatile uint32_t rs485_speed_test_state = (uint32_t)RS485_SPEED_TEST_STATE_IDLE;
volatile uint32_t rs485_speed_test_current_index = 0U;
volatile uint32_t rs485_speed_test_current_baud = RS485_SPEED_TEST_DEFAULT_BAUDRATE;
volatile uint32_t rs485_speed_test_current_actual_baud = 0U;
volatile uint32_t rs485_speed_test_next_baud = 0U;
volatile uint32_t rs485_speed_test_best_stable_baud = 0U;
volatile uint32_t rs485_speed_test_best_stable_actual_baud = 0U;
volatile uint32_t rs485_speed_test_failed_baud = 0U;
volatile uint32_t rs485_speed_test_window_elapsed_ms = 0U;
volatile uint32_t rs485_speed_test_tx_frames = 0U;
volatile uint32_t rs485_speed_test_rx_frames = 0U;
volatile uint32_t rs485_speed_test_echo_frames = 0U;
volatile uint32_t rs485_speed_test_timeout_count = 0U;
volatile uint32_t rs485_speed_test_protocol_error_count = 0U;
volatile uint32_t rs485_speed_test_app_error_count = 0U;
volatile uint32_t rs485_speed_test_uart_error_count = 0U;
volatile uint32_t rs485_speed_test_rx_overflow_count = 0U;
volatile uint32_t rs485_speed_test_tx_overflow_count = 0U;
volatile uint32_t rs485_speed_test_rx_bytes_per_second = 0U;
volatile uint32_t rs485_speed_test_rx_bits_per_second = 0U;
volatile uint32_t rs485_speed_test_payload_bits_per_second = 0U;
volatile uint32_t rs485_speed_test_protocol_error_rate_ppm = 0U;
volatile uint32_t rs485_speed_test_sequence = 1U;
volatile uint32_t rs485_speed_test_last_rx_sequence = 0U;
volatile uint32_t rs485_speed_test_last_rx_command = 0U;
volatile uint32_t rs485_speed_test_last_status = (uint32_t)RS422_PROTOCOL_STATUS_NO_FRAME;
volatile uint32_t rs485_speed_test_last_hal_status = (uint32_t)HAL_OK;
volatile uint32_t rs485_speed_test_waiting_echo = 0U;
volatile uint32_t rs485_speed_test_stable = 0U;

static RS422_ProtocolPacket rs485_speed_test_packet;
static RS422_ProtocolStats rs485_speed_test_protocol_stats;
static RS485_SpeedTestSlaveStats rs485_speed_test_slave_stats[RS485_SPEED_TEST_MASTER_SLAVE_COUNT];
static uint32_t rs485_speed_test_wait_sequence;
static uint32_t rs485_speed_test_wait_deadline_tick;
static uint32_t rs485_speed_test_window_start_tick;
static uint32_t rs485_speed_test_last_sample_tick;
static uint32_t rs485_speed_test_last_sample_bytes;
static uint8_t rs485_speed_test_running;

static uint32_t RS485_GetU32(const uint8_t *data, uint16_t offset)
{
    return ((uint32_t)data[offset]) |
           ((uint32_t)data[(uint16_t)(offset + 1U)] << 8U) |
           ((uint32_t)data[(uint16_t)(offset + 2U)] << 16U) |
           ((uint32_t)data[(uint16_t)(offset + 3U)] << 24U);
}

static void RS485_PutU32(uint8_t *data, uint16_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xFFU);
    data[(uint16_t)(offset + 1U)] = (uint8_t)((value >> 8U) & 0xFFU);
    data[(uint16_t)(offset + 2U)] = (uint8_t)((value >> 16U) & 0xFFU);
    data[(uint16_t)(offset + 3U)] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint8_t RS485_SpeedTestInstanceIsValid(RS485_SpeedTestInstanceId instance)
{
    return ((uint32_t)instance < (uint32_t)RS485_SPEED_TEST_INSTANCE_COUNT) ? 1U : 0U;
}

static uint8_t RS485_SpeedTestInstanceToSlaveIndex(RS485_SpeedTestInstanceId instance)
{
    if (instance == RS485_SPEED_TEST_INSTANCE_UART3)
    {
        return 1U;
    }

    return 0U;
}

static uint8_t RS485_SpeedTestSlaveIndexFromId(uint8_t slave_id)
{
    uint8_t index;

    for (index = 0U; index < RS485_SPEED_TEST_MASTER_SLAVE_COUNT; index++)
    {
        if (rs485_speed_test_master_slave_table[index] == slave_id)
        {
            return index;
        }
    }

    return RS485_SPEED_TEST_MASTER_SLAVE_COUNT;
}

static uint8_t RS485_SpeedTestAnySlaveEnabled(void)
{
    uint8_t index;

    for (index = 0U; index < RS485_SPEED_TEST_MASTER_SLAVE_COUNT; index++)
    {
        if (rs485_speed_test_slave_stats[index].enabled != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t RS485_SpeedTestCurrentMasterSlaveId(void)
{
    uint8_t guard = 0U;

    while (guard < RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
    {
        if (rs485_speed_test_current_slave_index >= RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
        {
            rs485_speed_test_current_slave_index = 0U;
        }

        if (rs485_speed_test_slave_stats[rs485_speed_test_current_slave_index].enabled != 0U)
        {
            return rs485_speed_test_master_slave_table[rs485_speed_test_current_slave_index];
        }

        rs485_speed_test_current_slave_index++;
        guard++;
    }

    rs485_speed_test_current_slave_index = 0U;
    return rs485_speed_test_master_slave_table[0U];
}

static void RS485_SpeedTestSetFirstMasterSlave(void)
{
    rs485_speed_test_current_slave_index = 0U;
    rs485_speed_test_current_slave_id = RS485_SpeedTestCurrentMasterSlaveId();
}

static void RS485_SpeedTestAdvanceMasterSlave(void)
{
    rs485_speed_test_current_slave_index++;
    if (rs485_speed_test_current_slave_index >= RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
    {
        rs485_speed_test_current_slave_index = 0U;
    }

    rs485_speed_test_current_slave_id = RS485_SpeedTestCurrentMasterSlaveId();
    rs485_speed_test_waiting_echo = 0U;
}

static void RS485_SpeedTestBuildPayload(uint8_t command,
                                        uint8_t index,
                                        uint32_t baud,
                                        uint32_t sequence,
                                        uint32_t actual_baud,
                                        uint8_t data[RS422_PROTOCOL_DATA_LENGTH])
{
    uint16_t offset;

    (void)memset(data, 0, RS422_PROTOCOL_DATA_LENGTH);
    data[RS485_SPEED_PAYLOAD_MAGIC0_OFFSET] = RS485_SPEED_PAYLOAD_MAGIC0;
    data[RS485_SPEED_PAYLOAD_MAGIC1_OFFSET] = RS485_SPEED_PAYLOAD_MAGIC1;
    data[RS485_SPEED_PAYLOAD_VERSION_OFFSET] = RS485_SPEED_PAYLOAD_VERSION;
    data[RS485_SPEED_PAYLOAD_COMMAND_OFFSET] = command;
    data[RS485_SPEED_PAYLOAD_INDEX_OFFSET] = index;
    data[RS485_SPEED_PAYLOAD_ROLE_OFFSET] = (uint8_t)rs485_speed_test_role;
    data[RS485_SPEED_PAYLOAD_FLAGS_OFFSET] = 0U;
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_BAUD_OFFSET, baud);
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_SEQUENCE_OFFSET, sequence);
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_TICK_OFFSET, HAL_GetTick());
    RS485_PutU32(data, RS485_SPEED_PAYLOAD_ACTUAL_BAUD_OFFSET, actual_baud);

    for (offset = RS485_SPEED_PAYLOAD_PATTERN_OFFSET;
         offset < RS422_PROTOCOL_DATA_LENGTH;
         offset++)
    {
        data[offset] = (uint8_t)(sequence + offset);
    }
}

static uint8_t RS485_SpeedTestPayloadIsValid(const uint8_t data[RS422_PROTOCOL_DATA_LENGTH])
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

static HAL_StatusTypeDef RS485_SpeedTestSendCommand(uint8_t target_node_id,
                                                    uint8_t command,
                                                    uint8_t index,
                                                    uint32_t baud,
                                                    uint32_t sequence,
                                                    uint32_t actual_baud)
{
    uint8_t payload[RS422_PROTOCOL_DATA_LENGTH];
    HAL_StatusTypeDef status;

    RS485_SpeedTestBuildPayload(command, index, baud, sequence, actual_baud, payload);
    status = RS422_ProtocolSend_IT(RS485_SPEED_TEST_PORT,
                                   target_node_id,
                                   RS485_SPEED_TEST_DATA_TYPE,
                                   payload);
    rs485_speed_test_last_hal_status = (uint32_t)status;
    return status;
}

static void RS485_SpeedTestRefreshProtocolStats(void)
{
    RS422_ProtocolGetStats(RS485_SPEED_TEST_PORT, &rs485_speed_test_protocol_stats);
    rs485_speed_test_current_actual_baud = RS422_GetActualBaudRate(RS485_SPEED_TEST_PORT);
    rs485_speed_test_uart_error_count = rs422_diag_error_count[RS485_SPEED_TEST_PORT];
    rs485_speed_test_rx_overflow_count = rs422_diag_rx_overflow_count[RS485_SPEED_TEST_PORT];
    rs485_speed_test_tx_overflow_count = rs422_diag_tx_overflow_count[RS485_SPEED_TEST_PORT];
    rs485_speed_test_protocol_error_rate_ppm = rs485_speed_test_protocol_stats.error_rate_ppm;
}

static void RS485_SpeedTestUpdateRateMeter(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed;
    uint32_t delta_bytes;

    if (rs485_speed_test_last_sample_tick == 0U)
    {
        rs485_speed_test_last_sample_tick = now;
        rs485_speed_test_last_sample_bytes = rs485_speed_test_protocol_stats.total_bytes;
        return;
    }

    elapsed = now - rs485_speed_test_last_sample_tick;
    if (elapsed < 1000U)
    {
        return;
    }

    delta_bytes = rs485_speed_test_protocol_stats.total_bytes -
                  rs485_speed_test_last_sample_bytes;
    rs485_speed_test_rx_bytes_per_second = (delta_bytes * 1000U) / elapsed;
    rs485_speed_test_rx_bits_per_second = rs485_speed_test_rx_bytes_per_second * 10U;
    rs485_speed_test_payload_bits_per_second =
        (rs485_speed_test_rx_bytes_per_second * 8U * RS422_PROTOCOL_DATA_LENGTH) /
        RS422_PROTOCOL_FRAME_LENGTH;
    rs485_speed_test_last_sample_tick = now;
    rs485_speed_test_last_sample_bytes = rs485_speed_test_protocol_stats.total_bytes;
}

static void RS485_SpeedTestResetCounters(void)
{
    uint8_t index;

    rs485_speed_test_current_slave_id = 1U;
    rs485_speed_test_current_slave_index = 0U;
    rs485_speed_test_ignored_node_frames = 0U;
    rs485_speed_test_current_index = 0U;
    rs485_speed_test_current_baud = RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    rs485_speed_test_current_actual_baud = RS422_GetActualBaudRate(RS485_SPEED_TEST_PORT);
    rs485_speed_test_next_baud = 0U;
    rs485_speed_test_best_stable_baud = 0U;
    rs485_speed_test_best_stable_actual_baud = 0U;
    rs485_speed_test_failed_baud = 0U;
    rs485_speed_test_window_elapsed_ms = 0U;
    rs485_speed_test_tx_frames = 0U;
    rs485_speed_test_rx_frames = 0U;
    rs485_speed_test_echo_frames = 0U;
    rs485_speed_test_timeout_count = 0U;
    rs485_speed_test_protocol_error_count = 0U;
    rs485_speed_test_app_error_count = 0U;
    rs485_speed_test_uart_error_count = 0U;
    rs485_speed_test_rx_overflow_count = 0U;
    rs485_speed_test_tx_overflow_count = 0U;
    rs485_speed_test_rx_bytes_per_second = 0U;
    rs485_speed_test_rx_bits_per_second = 0U;
    rs485_speed_test_payload_bits_per_second = 0U;
    rs485_speed_test_protocol_error_rate_ppm = 0U;
    rs485_speed_test_sequence = 1U;
    rs485_speed_test_last_rx_sequence = 0U;
    rs485_speed_test_last_rx_command = 0U;
    rs485_speed_test_last_status = (uint32_t)RS422_PROTOCOL_STATUS_NO_FRAME;
    rs485_speed_test_last_hal_status = (uint32_t)HAL_OK;
    rs485_speed_test_waiting_echo = 0U;
    rs485_speed_test_stable = 0U;
    rs485_speed_test_wait_sequence = 0U;
    rs485_speed_test_wait_deadline_tick = 0U;
    rs485_speed_test_window_start_tick = HAL_GetTick();
    rs485_speed_test_last_sample_tick = 0U;
    rs485_speed_test_last_sample_bytes = 0U;
    (void)memset(&rs485_speed_test_packet, 0, sizeof(rs485_speed_test_packet));
    (void)memset(&rs485_speed_test_protocol_stats, 0, sizeof(rs485_speed_test_protocol_stats));

    for (index = 0U; index < RS485_SPEED_TEST_MASTER_SLAVE_COUNT; index++)
    {
        rs485_speed_test_slave_stats[index].slave_id =
            rs485_speed_test_master_slave_table[index];
        rs485_speed_test_slave_stats[index].tx_frames = 0U;
        rs485_speed_test_slave_stats[index].rx_frames = 0U;
        rs485_speed_test_slave_stats[index].echo_frames = 0U;
        rs485_speed_test_slave_stats[index].timeout_count = 0U;
        rs485_speed_test_slave_stats[index].protocol_error_count = 0U;
        rs485_speed_test_slave_stats[index].app_error_count = 0U;
        rs485_speed_test_slave_stats[index].last_rx_sequence = 0U;
        rs485_speed_test_slave_stats[index].last_status =
            (uint32_t)RS422_PROTOCOL_STATUS_NO_FRAME;
        rs485_speed_test_slave_stats[index].last_hal_status = (uint32_t)HAL_OK;
    }
}

static void RS485_SpeedTestHandleDataPacket(const RS422_ProtocolPacket *packet)
{
    uint8_t command;
    uint8_t index;
    uint8_t slave_index;
    uint32_t baud;
    uint32_t sequence;

    if ((packet == NULL) || (RS485_SpeedTestPayloadIsValid(packet->data) == 0U))
    {
        rs485_speed_test_app_error_count++;
        return;
    }

    command = packet->data[RS485_SPEED_PAYLOAD_COMMAND_OFFSET];
    index = packet->data[RS485_SPEED_PAYLOAD_INDEX_OFFSET];
    baud = RS485_GetU32(packet->data, RS485_SPEED_PAYLOAD_BAUD_OFFSET);
    sequence = RS485_GetU32(packet->data, RS485_SPEED_PAYLOAD_SEQUENCE_OFFSET);
    if (RS485_SpeedTestPatternIsValid(packet->data, sequence) == 0U)
    {
        rs485_speed_test_app_error_count++;
        return;
    }

    rs485_speed_test_last_rx_command = command;
    rs485_speed_test_last_rx_sequence = sequence;
    rs485_speed_test_rx_frames++;

    if (rs485_speed_test_role == (uint32_t)RS485_SPEED_TEST_ROLE_MASTER)
    {
        if (packet->id != (uint8_t)rs485_speed_test_current_slave_id)
        {
            rs485_speed_test_ignored_node_frames++;
            return;
        }

        slave_index = RS485_SpeedTestSlaveIndexFromId(packet->id);
        if (slave_index >= RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
        {
            rs485_speed_test_ignored_node_frames++;
            return;
        }

        rs485_speed_test_slave_stats[slave_index].rx_frames++;
        rs485_speed_test_slave_stats[slave_index].last_rx_sequence = sequence;

        if ((command == (uint8_t)RS485_SPEED_COMMAND_ECHO) &&
            (rs485_speed_test_waiting_echo != 0U) &&
            (sequence == rs485_speed_test_wait_sequence) &&
            (index == (uint8_t)rs485_speed_test_current_index) &&
            (baud == rs485_speed_test_current_baud))
        {
            rs485_speed_test_echo_frames++;
            rs485_speed_test_slave_stats[slave_index].echo_frames++;
            rs485_speed_test_waiting_echo = 0U;
            RS485_SpeedTestAdvanceMasterSlave();
        }
        else
        {
            rs485_speed_test_app_error_count++;
            rs485_speed_test_slave_stats[slave_index].app_error_count++;
        }
    }
}

static void RS485_SpeedTestPollProtocol(void)
{
    RS422_ProtocolStatus status;
    uint8_t guard = 0U;

    do
    {
        status = RS422_ProtocolPoll(RS485_SPEED_TEST_PORT, &rs485_speed_test_packet);
        rs485_speed_test_last_status = (uint32_t)status;
        if (status == RS422_PROTOCOL_STATUS_OK)
        {
            RS485_SpeedTestHandleDataPacket(&rs485_speed_test_packet);
        }
        else if (status != RS422_PROTOCOL_STATUS_NO_FRAME)
        {
            rs485_speed_test_protocol_error_count++;
        }
        guard++;
    } while ((status != RS422_PROTOCOL_STATUS_NO_FRAME) && (guard < 8U));
}

static void RS485_SpeedTestMasterSendDataIfReady(void)
{
    HAL_StatusTypeDef status;
    uint32_t sequence;
    uint8_t slave_index;

    if ((rs485_speed_test_running == 0U) ||
        (rs485_speed_test_waiting_echo != 0U) ||
        (RS422_IsTxBusy(RS485_SPEED_TEST_PORT) != 0U) ||
        (RS485_SpeedTestAnySlaveEnabled() == 0U))
    {
        return;
    }

    rs485_speed_test_current_slave_id = RS485_SpeedTestCurrentMasterSlaveId();
    slave_index = RS485_SpeedTestSlaveIndexFromId((uint8_t)rs485_speed_test_current_slave_id);
    if (slave_index >= RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
    {
        RS485_SpeedTestAdvanceMasterSlave();
        return;
    }

    sequence = rs485_speed_test_sequence++;
    status = RS485_SpeedTestSendCommand((uint8_t)rs485_speed_test_current_slave_id,
                                        (uint8_t)RS485_SPEED_COMMAND_DATA,
                                        (uint8_t)rs485_speed_test_current_index,
                                        rs485_speed_test_current_baud,
                                        sequence,
                                        RS422_GetActualBaudRate(RS485_SPEED_TEST_PORT));
    rs485_speed_test_slave_stats[slave_index].last_hal_status = (uint32_t)status;

    if (status == HAL_OK)
    {
        rs485_speed_test_wait_sequence = sequence;
        rs485_speed_test_wait_deadline_tick =
            HAL_GetTick() + RS485_SPEED_TEST_FRAME_TIMEOUT_MS;
        rs485_speed_test_waiting_echo = 1U;
        rs485_speed_test_tx_frames++;
        rs485_speed_test_slave_stats[slave_index].tx_frames++;
    }
    else
    {
        rs485_speed_test_app_error_count++;
        rs485_speed_test_slave_stats[slave_index].app_error_count++;
        RS485_SpeedTestAdvanceMasterSlave();
    }
}

static void RS485_SpeedTestMasterRun(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t slave_index;

    if (rs485_speed_test_state == (uint32_t)RS485_SPEED_TEST_STATE_MASTER_START_RATE)
    {
        rs485_speed_test_current_index = 0U;
        rs485_speed_test_current_baud = rs485_speed_test_baud_table[0U];
        rs485_speed_test_current_actual_baud =
            RS422_GetActualBaudRate(RS485_SPEED_TEST_PORT);
        rs485_speed_test_window_start_tick = now;
        RS485_SpeedTestSetFirstMasterSlave();
        rs485_speed_test_state = (uint32_t)RS485_SPEED_TEST_STATE_MASTER_RUNNING;
    }

    if (rs485_speed_test_state != (uint32_t)RS485_SPEED_TEST_STATE_MASTER_RUNNING)
    {
        return;
    }

    rs485_speed_test_window_elapsed_ms = now - rs485_speed_test_window_start_tick;

    if ((rs485_speed_test_waiting_echo != 0U) &&
        ((int32_t)(now - rs485_speed_test_wait_deadline_tick) >= 0))
    {
        rs485_speed_test_timeout_count++;
        slave_index = RS485_SpeedTestSlaveIndexFromId((uint8_t)rs485_speed_test_current_slave_id);
        if (slave_index < RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
        {
            rs485_speed_test_slave_stats[slave_index].timeout_count++;
        }
        rs485_speed_test_waiting_echo = 0U;
        RS485_SpeedTestAdvanceMasterSlave();
    }

    RS485_SpeedTestMasterSendDataIfReady();
}

void RS485_SpeedTest_SetRole(RS485_SpeedTestRole role)
{
    if ((role == RS485_SPEED_TEST_ROLE_MASTER) || (role == RS485_SPEED_TEST_ROLE_SLAVE))
    {
        rs485_speed_test_role = (uint32_t)role;
    }
}

void RS485_SpeedTest_Init(void)
{
    uint8_t index;
    HAL_StatusTypeDef status;

    for (index = 0U; index < RS485_SPEED_TEST_MASTER_SLAVE_COUNT; index++)
    {
        rs485_speed_test_slave_stats[index].enabled = 0U;
        rs485_speed_test_slave_stats[index].slave_id =
            rs485_speed_test_master_slave_table[index];
    }

    rs485_speed_test_local_node_id = RS485_SPEED_TEST_LOCAL_NODE_ID;
    RS485_SpeedTestResetCounters();
    RS485_SpeedTestSetFirstMasterSlave();
    status = RS422_SetBaudRate(RS485_SPEED_TEST_PORT, RS485_SPEED_TEST_DEFAULT_BAUDRATE);
    rs485_speed_test_last_hal_status = (uint32_t)status;
    rs485_speed_test_current_actual_baud = RS422_GetActualBaudRate(RS485_SPEED_TEST_PORT);
    rs485_speed_test_running = 0U;
    rs485_speed_test_state = (status == HAL_OK) ?
        (uint32_t)RS485_SPEED_TEST_STATE_IDLE :
        (uint32_t)RS485_SPEED_TEST_STATE_ERROR;
}

void RS485_SpeedTest_Start(RS485_SpeedTestInstanceId instance, uint32_t baudrate)
{
    uint8_t slave_index;
    HAL_StatusTypeDef status;

    if (RS485_SpeedTestInstanceIsValid(instance) == 0U)
    {
        return;
    }

    slave_index = RS485_SpeedTestInstanceToSlaveIndex(instance);
    if (slave_index >= RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
    {
        return;
    }

    if (rs485_speed_test_running == 0U)
    {
        RS485_SpeedTestResetCounters();
    }

    if (baudrate == 0U)
    {
        baudrate = RS485_SPEED_TEST_DEFAULT_BAUDRATE;
    }

    rs485_speed_test_current_baud = baudrate;
    status = RS422_SetBaudRate(RS485_SPEED_TEST_PORT, baudrate);
    rs485_speed_test_last_hal_status = (uint32_t)status;
    rs485_speed_test_slave_stats[slave_index].last_hal_status = (uint32_t)status;
    if (status != HAL_OK)
    {
        rs485_speed_test_state = (uint32_t)RS485_SPEED_TEST_STATE_ERROR;
        rs485_speed_test_app_error_count++;
        rs485_speed_test_slave_stats[slave_index].app_error_count++;
        return;
    }

    rs485_speed_test_role = (uint32_t)RS485_SPEED_TEST_ROLE_MASTER;
    rs485_speed_test_slave_stats[slave_index].enabled = 1U;
    rs485_speed_test_running = 1U;
    rs485_speed_test_current_actual_baud = RS422_GetActualBaudRate(RS485_SPEED_TEST_PORT);
    rs485_speed_test_state = (uint32_t)RS485_SPEED_TEST_STATE_MASTER_START_RATE;
    RS485_SpeedTestSetFirstMasterSlave();
}

void RS485_SpeedTest_Stop(RS485_SpeedTestInstanceId instance)
{
    uint8_t slave_index;

    if (RS485_SpeedTestInstanceIsValid(instance) == 0U)
    {
        return;
    }

    slave_index = RS485_SpeedTestInstanceToSlaveIndex(instance);
    if (slave_index < RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
    {
        rs485_speed_test_slave_stats[slave_index].enabled = 0U;
    }

    if (RS485_SpeedTestAnySlaveEnabled() == 0U)
    {
        rs485_speed_test_running = 0U;
        rs485_speed_test_waiting_echo = 0U;
        rs485_speed_test_state = (uint32_t)RS485_SPEED_TEST_STATE_STOPPED;
    }
}

void RS485_SpeedTest_StopAll(void)
{
    uint8_t index;

    for (index = 0U; index < RS485_SPEED_TEST_MASTER_SLAVE_COUNT; index++)
    {
        rs485_speed_test_slave_stats[index].enabled = 0U;
    }

    rs485_speed_test_running = 0U;
    rs485_speed_test_waiting_echo = 0U;
    rs485_speed_test_state = (uint32_t)RS485_SPEED_TEST_STATE_STOPPED;
}

void RS485_SpeedTest_Run(void)
{
    RS485_SpeedTestPollProtocol();
    RS485_SpeedTestRefreshProtocolStats();
    RS485_SpeedTestUpdateRateMeter();

    if (rs485_speed_test_role == (uint32_t)RS485_SPEED_TEST_ROLE_MASTER)
    {
        RS485_SpeedTestMasterRun();
    }
}

void RS485_SpeedTest_GetStats(RS485_SpeedTestInstanceId instance,
                              RS485_SpeedTestStats *stats)
{
    uint8_t slave_index;
    uint32_t error_count;

    if ((RS485_SpeedTestInstanceIsValid(instance) == 0U) || (stats == NULL))
    {
        return;
    }

    slave_index = RS485_SpeedTestInstanceToSlaveIndex(instance);
    if (slave_index >= RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
    {
        return;
    }

    RS485_SpeedTestRefreshProtocolStats();
    if (rs485_speed_test_slave_stats[slave_index].enabled == 0U)
    {
        (void)memset(stats, 0, sizeof(*stats));
        stats->state = rs485_speed_test_state;
        stats->baudrate = rs485_speed_test_current_baud;
        stats->actual_baudrate = rs485_speed_test_current_actual_baud;
        stats->last_status = rs485_speed_test_last_status;
        stats->last_hal_status = rs485_speed_test_slave_stats[slave_index].last_hal_status;
        return;
    }

    error_count = rs485_speed_test_slave_stats[slave_index].timeout_count +
                  rs485_speed_test_slave_stats[slave_index].protocol_error_count +
                  rs485_speed_test_slave_stats[slave_index].app_error_count;

    stats->active = rs485_speed_test_slave_stats[slave_index].enabled;
    stats->state = rs485_speed_test_state;
    stats->baudrate = rs485_speed_test_current_baud;
    stats->actual_baudrate = rs485_speed_test_current_actual_baud;
    stats->tx_bytes = rs485_speed_test_slave_stats[slave_index].tx_frames *
                      RS422_PROTOCOL_FRAME_LENGTH;
    stats->rx_bytes = rs485_speed_test_slave_stats[slave_index].rx_frames *
                      RS422_PROTOCOL_FRAME_LENGTH;
    stats->error_bytes = error_count * RS422_PROTOCOL_FRAME_LENGTH;
    stats->error_rate_ppm = (stats->rx_bytes == 0U) ? 0U :
        (uint32_t)(((uint64_t)stats->error_bytes * RS422_PROTOCOL_ERROR_RATE_SCALE) /
                   stats->rx_bytes);
    stats->tx_frames = rs485_speed_test_slave_stats[slave_index].tx_frames;
    stats->rx_frames = rs485_speed_test_slave_stats[slave_index].rx_frames;
    stats->echo_frames = rs485_speed_test_slave_stats[slave_index].echo_frames;
    stats->timeout_count = rs485_speed_test_slave_stats[slave_index].timeout_count;
    stats->protocol_error_count = rs485_speed_test_protocol_error_count;
    stats->app_error_count = rs485_speed_test_slave_stats[slave_index].app_error_count;
    stats->uart_error_count = rs422_diag_error_count[RS485_SPEED_TEST_PORT];
    stats->rx_overflow_count = rs422_diag_rx_overflow_count[RS485_SPEED_TEST_PORT];
    stats->tx_overflow_count = rs422_diag_tx_overflow_count[RS485_SPEED_TEST_PORT];
    stats->last_sequence = rs485_speed_test_sequence;
    stats->last_rx_sequence = rs485_speed_test_slave_stats[slave_index].last_rx_sequence;
    stats->last_status = rs485_speed_test_last_status;
    stats->last_hal_status = rs485_speed_test_slave_stats[slave_index].last_hal_status;
}

uint8_t RS485_SpeedTest_IsRunning(RS485_SpeedTestInstanceId instance)
{
    uint8_t slave_index;

    if (RS485_SpeedTestInstanceIsValid(instance) == 0U)
    {
        return 0U;
    }

    slave_index = RS485_SpeedTestInstanceToSlaveIndex(instance);
    if (slave_index >= RS485_SPEED_TEST_MASTER_SLAVE_COUNT)
    {
        return 0U;
    }

    return rs485_speed_test_slave_stats[slave_index].enabled;
}
