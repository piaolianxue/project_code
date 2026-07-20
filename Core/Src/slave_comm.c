#include "slave_comm.h"
#include "i2c.h"
#include "fdcan.h"

#include <string.h>

static uint8_t slave_comm_can_tx_data[SLAVE_COMM_CAN_MAX_DATA_LENGTH];
static uint8_t slave_comm_can_rx_data[SLAVE_COMM_CAN_MAX_DATA_LENGTH];

volatile uint32_t slave_comm_i2c_tx_count = 0U;
volatile uint32_t slave_comm_i2c_rx_count = 0U;
volatile uint32_t slave_comm_i2c_error_count = 0U;
volatile uint32_t slave_comm_can_tx_count = 0U;
volatile uint32_t slave_comm_can_rx_count = 0U;
volatile uint32_t slave_comm_can_error_count = 0U;
volatile uint32_t slave_comm_can_ignored_count = 0U;
volatile uint32_t slave_comm_can_sequence = 0U;
volatile uint32_t slave_comm_last_can_id = 0U;
volatile uint32_t slave_comm_last_can_sequence = 0U;
volatile uint32_t slave_comm_last_can_command = 0U;
volatile uint32_t slave_comm_last_can_length = 0U;
volatile uint8_t slave_comm_last_can_data[SLAVE_COMM_CAN_MAX_DATA_LENGTH] = {0U};
volatile HAL_StatusTypeDef slave_comm_last_i2c_status = HAL_OK;
volatile HAL_StatusTypeDef slave_comm_last_can_status = HAL_OK;

static void SlaveComm_RecordI2CStatus(HAL_StatusTypeDef status,
                                      uint8_t is_receive)
{
    slave_comm_last_i2c_status = status;
    if (status == HAL_OK)
    {
        if (is_receive != 0U)
        {
            slave_comm_i2c_rx_count++;
        }
        else
        {
            slave_comm_i2c_tx_count++;
        }
    }
    else
    {
        slave_comm_i2c_error_count++;
    }
}

static void SlaveComm_RecordCanStatus(HAL_StatusTypeDef status)
{
    slave_comm_last_can_status = status;
    if (status == HAL_OK)
    {
        slave_comm_can_tx_count++;
    }
    else
    {
        slave_comm_can_error_count++;
    }
}

static uint32_t SlaveComm_CanLengthToDlc(uint8_t length)
{
    if (length <= 1U) { return (length == 0U) ? FDCAN_DLC_BYTES_0 : FDCAN_DLC_BYTES_1; }
    if (length == 2U) { return FDCAN_DLC_BYTES_2; }
    if (length == 3U) { return FDCAN_DLC_BYTES_3; }
    if (length == 4U) { return FDCAN_DLC_BYTES_4; }
    if (length == 5U) { return FDCAN_DLC_BYTES_5; }
    if (length == 6U) { return FDCAN_DLC_BYTES_6; }
    if (length == 7U) { return FDCAN_DLC_BYTES_7; }
    if (length <= 8U) { return FDCAN_DLC_BYTES_8; }
    if (length <= 12U) { return FDCAN_DLC_BYTES_12; }
    if (length <= 16U) { return FDCAN_DLC_BYTES_16; }
    if (length <= 20U) { return FDCAN_DLC_BYTES_20; }
    if (length <= 24U) { return FDCAN_DLC_BYTES_24; }
    if (length <= 32U) { return FDCAN_DLC_BYTES_32; }
    if (length <= 48U) { return FDCAN_DLC_BYTES_48; }
    return FDCAN_DLC_BYTES_64;
}

static uint8_t SlaveComm_CanDlcToLength(uint32_t dlc)
{
    switch (dlc)
    {
        case FDCAN_DLC_BYTES_0: return 0U;
        case FDCAN_DLC_BYTES_1: return 1U;
        case FDCAN_DLC_BYTES_2: return 2U;
        case FDCAN_DLC_BYTES_3: return 3U;
        case FDCAN_DLC_BYTES_4: return 4U;
        case FDCAN_DLC_BYTES_5: return 5U;
        case FDCAN_DLC_BYTES_6: return 6U;
        case FDCAN_DLC_BYTES_7: return 7U;
        case FDCAN_DLC_BYTES_8: return 8U;
        case FDCAN_DLC_BYTES_12: return 12U;
        case FDCAN_DLC_BYTES_16: return 16U;
        case FDCAN_DLC_BYTES_20: return 20U;
        case FDCAN_DLC_BYTES_24: return 24U;
        case FDCAN_DLC_BYTES_32: return 32U;
        case FDCAN_DLC_BYTES_48: return 48U;
        case FDCAN_DLC_BYTES_64: return 64U;
        default: return 0U;
    }
}

static uint8_t SlaveComm_NextCanSequence(void)
{
    slave_comm_can_sequence = (slave_comm_can_sequence + 1U) & 0xFFU;
    return (uint8_t)slave_comm_can_sequence;
}

static HAL_StatusTypeDef SlaveComm_CanSendCommand(uint8_t command,
                                                  const uint8_t *params,
                                                  uint8_t param_length)
{
    FDCAN_TxHeaderTypeDef tx_header;
    HAL_StatusTypeDef status;
    uint8_t frame_length;

    if ((param_length > SLAVE_COMM_CAN_MAX_PARAM_LENGTH) ||
        ((param_length != 0U) && (params == NULL)))
    {
        SlaveComm_RecordCanStatus(HAL_ERROR);
        return HAL_ERROR;
    }

    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U)
    {
        SlaveComm_RecordCanStatus(HAL_BUSY);
        return HAL_BUSY;
    }

    frame_length = (uint8_t)(param_length + 2U);
    (void)memset(slave_comm_can_tx_data, 0, sizeof(slave_comm_can_tx_data));
    slave_comm_can_tx_data[0U] = SlaveComm_NextCanSequence();
    slave_comm_can_tx_data[1U] = command;
    if (param_length != 0U)
    {
        (void)memcpy(&slave_comm_can_tx_data[2U], params, param_length);
    }

    tx_header.Identifier = SLAVE_COMM_CAN_COMMAND_ID;
    tx_header.IdType = FDCAN_EXTENDED_ID;
    tx_header.TxFrameType = FDCAN_DATA_FRAME;
    tx_header.DataLength = SlaveComm_CanLengthToDlc(frame_length);
    tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_header.BitRateSwitch = FDCAN_BRS_ON;
    tx_header.FDFormat = FDCAN_FD_CAN;
    tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_header.MessageMarker = 0U;

    status = HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1,
                                           &tx_header,
                                           slave_comm_can_tx_data);
    SlaveComm_RecordCanStatus(status);
    return status;
}

void SlaveComm_Init(void)
{
    slave_comm_i2c_tx_count = 0U;
    slave_comm_i2c_rx_count = 0U;
    slave_comm_i2c_error_count = 0U;
    slave_comm_can_tx_count = 0U;
    slave_comm_can_rx_count = 0U;
    slave_comm_can_error_count = 0U;
    slave_comm_can_ignored_count = 0U;
    slave_comm_can_sequence = 0U;
    slave_comm_last_can_id = 0U;
    slave_comm_last_can_sequence = 0U;
    slave_comm_last_can_command = 0U;
    slave_comm_last_can_length = 0U;
    slave_comm_last_i2c_status = HAL_OK;
    slave_comm_last_can_status = HAL_OK;
    (void)memset(slave_comm_can_rx_data, 0, sizeof(slave_comm_can_rx_data));
}

void SlaveComm_Poll(void)
{
    FDCAN_RxHeaderTypeDef rx_header;
    uint8_t length;
    uint8_t index;

    while (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) > 0U)
    {
        if (HAL_FDCAN_GetRxMessage(&hfdcan1,
                                   FDCAN_RX_FIFO0,
                                   &rx_header,
                                   slave_comm_can_rx_data) != HAL_OK)
        {
            slave_comm_can_error_count++;
            slave_comm_last_can_status = HAL_ERROR;
            return;
        }

        if ((rx_header.IdType != FDCAN_EXTENDED_ID) ||
            (rx_header.RxFrameType != FDCAN_DATA_FRAME) ||
            (rx_header.Identifier != SLAVE_COMM_CAN_RESPONSE_ID))
        {
            slave_comm_can_ignored_count++;
            continue;
        }

        length = SlaveComm_CanDlcToLength(rx_header.DataLength);
        if (length > SLAVE_COMM_CAN_MAX_DATA_LENGTH)
        {
            length = SLAVE_COMM_CAN_MAX_DATA_LENGTH;
        }

        slave_comm_last_can_id = rx_header.Identifier;
        slave_comm_last_can_length = length;
        slave_comm_last_can_sequence = (length > 0U) ? slave_comm_can_rx_data[0U] : 0U;
        slave_comm_last_can_command = (length > 1U) ? slave_comm_can_rx_data[1U] : 0U;
        for (index = 0U; index < SLAVE_COMM_CAN_MAX_DATA_LENGTH; index++)
        {
            slave_comm_last_can_data[index] =
                (index < length) ? slave_comm_can_rx_data[index] : 0U;
        }
        slave_comm_can_rx_count++;
        slave_comm_last_can_status = HAL_OK;
    }
}

void SlaveComm_GetStats(SlaveComm_Stats *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->i2c_tx_count = slave_comm_i2c_tx_count;
    stats->i2c_rx_count = slave_comm_i2c_rx_count;
    stats->i2c_error_count = slave_comm_i2c_error_count;
    stats->can_tx_count = slave_comm_can_tx_count;
    stats->can_rx_count = slave_comm_can_rx_count;
    stats->can_error_count = slave_comm_can_error_count;
    stats->can_ignored_count = slave_comm_can_ignored_count;
    stats->last_can_id = slave_comm_last_can_id;
    stats->last_can_sequence = slave_comm_last_can_sequence;
    stats->last_can_command = slave_comm_last_can_command;
    stats->last_can_length = slave_comm_last_can_length;
    stats->last_i2c_status = slave_comm_last_i2c_status;
    stats->last_can_status = slave_comm_last_can_status;
}

HAL_StatusTypeDef SlaveComm_I2C_WriteRegs(uint8_t reg,
                                          const uint8_t *data,
                                          uint16_t length)
{
    uint8_t tx_buffer[SLAVE_COMM_I2C_MAX_TRANSFER_LENGTH + 1U];
    HAL_StatusTypeDef status;

    if ((length > SLAVE_COMM_I2C_MAX_TRANSFER_LENGTH) ||
        ((length != 0U) && (data == NULL)))
    {
        SlaveComm_RecordI2CStatus(HAL_ERROR, 0U);
        return HAL_ERROR;
    }

    tx_buffer[0U] = reg;
    if (length != 0U)
    {
        (void)memcpy(&tx_buffer[1U], data, length);
    }

    status = HAL_I2C_Master_Transmit(&hi2c1,
                                     SLAVE_COMM_I2C_ADDRESS,
                                     tx_buffer,
                                     (uint16_t)(length + 1U),
                                     SLAVE_COMM_I2C_TIMEOUT_MS);
    SlaveComm_RecordI2CStatus(status, 0U);
    return status;
}

HAL_StatusTypeDef SlaveComm_I2C_ReadRegs(uint8_t reg,
                                         uint8_t *data,
                                         uint16_t length)
{
    HAL_StatusTypeDef status;

    if ((length == 0U) ||
        (length > SLAVE_COMM_I2C_MAX_TRANSFER_LENGTH) ||
        (data == NULL))
    {
        SlaveComm_RecordI2CStatus(HAL_ERROR, 1U);
        return HAL_ERROR;
    }

    status = HAL_I2C_Master_Transmit(&hi2c1,
                                     SLAVE_COMM_I2C_ADDRESS,
                                     &reg,
                                     1U,
                                     SLAVE_COMM_I2C_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        status = HAL_I2C_Master_Receive(&hi2c1,
                                        SLAVE_COMM_I2C_ADDRESS,
                                        data,
                                        length,
                                        SLAVE_COMM_I2C_TIMEOUT_MS);
    }

    SlaveComm_RecordI2CStatus(status, 1U);
    return status;
}

HAL_StatusTypeDef SlaveComm_CanPing(void)
{
    return SlaveComm_CanSendCommand((uint8_t)SLAVE_COMM_CAN_COMMAND_PING,
                                    NULL,
                                    0U);
}

HAL_StatusTypeDef SlaveComm_CanReadReg(uint8_t reg, uint8_t length)
{
    uint8_t params[2U];

    params[0U] = reg;
    params[1U] = length;
    return SlaveComm_CanSendCommand((uint8_t)SLAVE_COMM_CAN_COMMAND_READ_REG,
                                    params,
                                    (uint8_t)sizeof(params));
}

HAL_StatusTypeDef SlaveComm_CanWriteReg(uint8_t reg,
                                        const uint8_t *data,
                                        uint8_t length)
{
    uint8_t params[SLAVE_COMM_CAN_MAX_PARAM_LENGTH];

    if ((length > (SLAVE_COMM_CAN_MAX_PARAM_LENGTH - 1U)) ||
        ((length != 0U) && (data == NULL)))
    {
        SlaveComm_RecordCanStatus(HAL_ERROR);
        return HAL_ERROR;
    }

    params[0U] = reg;
    if (length != 0U)
    {
        (void)memcpy(&params[1U], data, length);
    }

    return SlaveComm_CanSendCommand((uint8_t)SLAVE_COMM_CAN_COMMAND_WRITE_REG,
                                    params,
                                    (uint8_t)(length + 1U));
}

HAL_StatusTypeDef SlaveComm_CanSetCanMode(uint8_t mode)
{
    return SlaveComm_CanSendCommand((uint8_t)SLAVE_COMM_CAN_COMMAND_SET_CAN_MODE,
                                    &mode,
                                    1U);
}

HAL_StatusTypeDef SlaveComm_CanClearFlags(uint16_t flags)
{
    uint8_t params[2U];

    params[0U] = (uint8_t)(flags & 0xFFU);
    params[1U] = (uint8_t)((flags >> 8U) & 0xFFU);
    return SlaveComm_CanSendCommand((uint8_t)SLAVE_COMM_CAN_COMMAND_CLEAR_FLAGS,
                                    params,
                                    (uint8_t)sizeof(params));
}
