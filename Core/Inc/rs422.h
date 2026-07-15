#ifndef __RS422_H
#define __RS422_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define RS422_RX_BUFFER_SIZE        512U
#define RS422_TX_BUFFER_SIZE        512U
typedef enum
{
    RS422_PORT_U9 = 0,
    RS422_PORT_U10,
    RS422_PORT_COUNT
} RS422_PortId;

void RS422_Init(void);

/* 以下诊断变量用于 DAPLink 观察 UART 中断和错误路径是否被触发。 */
extern volatile uint32_t rs422_diag_tx_complete_count[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_rx_complete_count[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_error_count[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_abort_count[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_last_error_code[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_last_rx_byte[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_start_receive_status[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_rx_overflow_count[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_tx_overflow_count[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_tx_total_bytes[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_requested_baud[RS422_PORT_COUNT];
extern volatile uint32_t rs422_diag_actual_baud[RS422_PORT_COUNT];

HAL_StatusTypeDef RS422_StartReceive(RS422_PortId port);
HAL_StatusTypeDef RS422_StartReceiveAll(void);
HAL_StatusTypeDef RS422_Transmit_IT(RS422_PortId port, const uint8_t *data, uint16_t size);
HAL_StatusTypeDef RS422_Transmit(RS422_PortId port, const uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef RS422_SetBaudRate(RS422_PortId port, uint32_t baud_rate);
uint32_t RS422_GetRequestedBaudRate(RS422_PortId port);
uint32_t RS422_GetActualBaudRate(RS422_PortId port);

uint16_t RS422_Available(RS422_PortId port);
uint16_t RS422_Read(RS422_PortId port, uint8_t *data, uint16_t max_size);
void RS422_ClearRx(RS422_PortId port);
/* 清空待发送队列，不会强行中止当前已经交给 HAL 的那个字节。 */
void RS422_ClearTx(RS422_PortId port);
uint8_t RS422_IsTxBusy(RS422_PortId port);

void RS422_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void RS422_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void RS422_UART_ErrorCallback(UART_HandleTypeDef *huart);
void RS422_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __RS422_H */
