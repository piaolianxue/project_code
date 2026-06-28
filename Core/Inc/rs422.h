#ifndef __RS422_H
#define __RS422_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define RS422_RX_BUFFER_SIZE        256U
#define RS422_TX_BUFFER_SIZE        256U
typedef enum
{
    RS422_PORT_U9 = 0,
    RS422_PORT_U10,
    RS422_PORT_COUNT
} RS422_PortId;

void RS422_Init(void);

HAL_StatusTypeDef RS422_StartReceive(RS422_PortId port);
HAL_StatusTypeDef RS422_StartReceiveAll(void);
HAL_StatusTypeDef RS422_Transmit_IT(RS422_PortId port, const uint8_t *data, uint16_t size);
HAL_StatusTypeDef RS422_Transmit(RS422_PortId port, const uint8_t *data, uint16_t size, uint32_t timeout);

uint16_t RS422_Available(RS422_PortId port);
uint16_t RS422_Read(RS422_PortId port, uint8_t *data, uint16_t max_size);
void RS422_ClearRx(RS422_PortId port);
uint8_t RS422_IsTxBusy(RS422_PortId port);

void RS422_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void RS422_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void RS422_UART_ErrorCallback(UART_HandleTypeDef *huart);
void RS422_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __RS422_H */
