#ifndef __SLAVE_COMM_H
#define __SLAVE_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SLAVE_COMM_I2C_ADDRESS_7BIT        0x43U
#define SLAVE_COMM_I2C_ADDRESS             (SLAVE_COMM_I2C_ADDRESS_7BIT << 1U)
#define SLAVE_COMM_CAN_COMMAND_ID          0x18EF4301UL
#define SLAVE_COMM_CAN_RESPONSE_ID         0x18EF0143UL
#define SLAVE_COMM_CAN_MAX_DATA_LENGTH     64U
#define SLAVE_COMM_CAN_MAX_PARAM_LENGTH    (SLAVE_COMM_CAN_MAX_DATA_LENGTH - 2U)
#define SLAVE_COMM_I2C_MAX_TRANSFER_LENGTH 64U
#define SLAVE_COMM_I2C_TIMEOUT_MS          100U

typedef enum
{
    SLAVE_COMM_CAN_COMMAND_PING = 0x01U,
    SLAVE_COMM_CAN_COMMAND_READ_REG = 0x02U,
    SLAVE_COMM_CAN_COMMAND_WRITE_REG = 0x03U,
    SLAVE_COMM_CAN_COMMAND_SET_CAN_MODE = 0x04U,
    SLAVE_COMM_CAN_COMMAND_CLEAR_FLAGS = 0x05U
} SlaveComm_CanCommand;

typedef struct
{
    uint32_t i2c_tx_count;
    uint32_t i2c_rx_count;
    uint32_t i2c_error_count;
    uint32_t can_tx_count;
    uint32_t can_rx_count;
    uint32_t can_error_count;
    uint32_t can_ignored_count;
    uint32_t last_can_id;
    uint32_t last_can_sequence;
    uint32_t last_can_command;
    uint32_t last_can_length;
    HAL_StatusTypeDef last_i2c_status;
    HAL_StatusTypeDef last_can_status;
} SlaveComm_Stats;

extern volatile uint32_t slave_comm_i2c_tx_count;
extern volatile uint32_t slave_comm_i2c_rx_count;
extern volatile uint32_t slave_comm_i2c_error_count;
extern volatile uint32_t slave_comm_can_tx_count;
extern volatile uint32_t slave_comm_can_rx_count;
extern volatile uint32_t slave_comm_can_error_count;
extern volatile uint32_t slave_comm_can_ignored_count;
extern volatile uint32_t slave_comm_can_sequence;
extern volatile uint32_t slave_comm_last_can_id;
extern volatile uint32_t slave_comm_last_can_sequence;
extern volatile uint32_t slave_comm_last_can_command;
extern volatile uint32_t slave_comm_last_can_length;
extern volatile uint8_t slave_comm_last_can_data[SLAVE_COMM_CAN_MAX_DATA_LENGTH];
extern volatile HAL_StatusTypeDef slave_comm_last_i2c_status;
extern volatile HAL_StatusTypeDef slave_comm_last_can_status;

void SlaveComm_Init(void);
void SlaveComm_Poll(void);
void SlaveComm_GetStats(SlaveComm_Stats *stats);
HAL_StatusTypeDef SlaveComm_I2C_WriteRegs(uint8_t reg,
                                          const uint8_t *data,
                                          uint16_t length);
HAL_StatusTypeDef SlaveComm_I2C_ReadRegs(uint8_t reg,
                                         uint8_t *data,
                                         uint16_t length);
HAL_StatusTypeDef SlaveComm_CanPing(void);
HAL_StatusTypeDef SlaveComm_CanReadReg(uint8_t reg, uint8_t length);
HAL_StatusTypeDef SlaveComm_CanWriteReg(uint8_t reg,
                                        const uint8_t *data,
                                        uint8_t length);
HAL_StatusTypeDef SlaveComm_CanSetCanMode(uint8_t mode);
HAL_StatusTypeDef SlaveComm_CanClearFlags(uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif /* __SLAVE_COMM_H */
