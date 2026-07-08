#ifndef __HOST_COMM_H
#define __HOST_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define HOST_COMM_FRAME_HEADER              0xEEU
#define HOST_COMM_FRAME_TAIL_0              0xFFU
#define HOST_COMM_FRAME_TAIL_1              0xFCU
#define HOST_COMM_FRAME_TAIL_2              0xFFU
#define HOST_COMM_FRAME_TAIL_3              0xFFU
#define HOST_COMM_MAX_PARAM_LENGTH          1024U
#define HOST_COMM_ERROR_RATE_SCALE          1000000U
#define HOST_COMM_DEFAULT_BAUDRATE          115200U
#define HOST_COMM_UPLOAD_INTERVAL_MS        1000U
#define HOST_COMM_TARGET_UART1              1U
#define HOST_COMM_TARGET_UART3              3U
#define HOST_COMM_TARGET_ALL                0xFFU

typedef enum
{
    HOST_COMM_CMD_SET_BAUDRATE = 0x01U,
    HOST_COMM_CMD_SET_ENABLE = 0x02U,
    HOST_COMM_CMD_UPLOAD_STATS = 0x81U
} HostComm_Command;

typedef struct
{
    uint32_t rx_total_bytes;
    uint32_t tx_total_bytes;
    uint32_t error_bytes;
    uint32_t error_rate_ppm;
    uint8_t uart1_enabled;
    uint8_t uart3_enabled;
    uint32_t baudrate;
} HostComm_Stats;

extern volatile uint32_t host_comm_rx_total_bytes;
extern volatile uint32_t host_comm_tx_total_bytes;
extern volatile uint32_t host_comm_error_bytes;
extern volatile uint32_t host_comm_error_rate_ppm;
extern volatile uint32_t host_comm_baudrate;
extern volatile uint8_t host_comm_uart1_enabled;
extern volatile uint8_t host_comm_uart3_enabled;
extern volatile uint8_t host_comm_last_command;
extern volatile HAL_StatusTypeDef host_comm_last_tx_status;
extern volatile HAL_StatusTypeDef host_comm_start_receive_status;

void HostComm_Init(void);
void HostComm_Poll(void);
HAL_StatusTypeDef HostComm_SendStats(void);
void HostComm_GetStats(HostComm_Stats *stats);
void HostComm_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HostComm_UART_ErrorCallback(UART_HandleTypeDef *huart);
void HostComm_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_COMM_H */
