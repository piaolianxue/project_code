#ifndef __RS485_SPEED_TEST_H
#define __RS485_SPEED_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rs422_protocol.h"

#define RS485_SPEED_TEST_DEFAULT_BAUDRATE      1000000U
#define RS485_SPEED_TEST_FRAME_TIMEOUT_MS      120U
#define RS485_SPEED_TEST_NODE_ID               1U
#define RS485_SPEED_TEST_DATA_TYPE             ((uint8_t)RS422_PROTOCOL_TYPE_55)

typedef enum
{
    RS485_SPEED_TEST_INSTANCE_UART1 = 0U,
    RS485_SPEED_TEST_INSTANCE_UART3,
    RS485_SPEED_TEST_INSTANCE_COUNT
} RS485_SpeedTestInstanceId;

typedef enum
{
    RS485_SPEED_TEST_STATE_IDLE = 0U,
    RS485_SPEED_TEST_STATE_RUNNING,
    RS485_SPEED_TEST_STATE_STOPPED,
    RS485_SPEED_TEST_STATE_ERROR
} RS485_SpeedTestState;

typedef struct
{
    uint8_t active;
    uint32_t state;
    uint32_t baudrate;
    uint32_t actual_baudrate;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t error_bytes;
    uint32_t error_rate_ppm;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t echo_frames;
    uint32_t timeout_count;
    uint32_t protocol_error_count;
    uint32_t app_error_count;
    uint32_t uart_error_count;
    uint32_t rx_overflow_count;
    uint32_t tx_overflow_count;
    uint32_t last_sequence;
    uint32_t last_rx_sequence;
    uint32_t last_status;
    uint32_t last_hal_status;
} RS485_SpeedTestStats;

void RS485_SpeedTest_Init(void);
void RS485_SpeedTest_Start(RS485_SpeedTestInstanceId instance, uint32_t baudrate);
void RS485_SpeedTest_Stop(RS485_SpeedTestInstanceId instance);
void RS485_SpeedTest_StopAll(void);
void RS485_SpeedTest_Run(void);
void RS485_SpeedTest_GetStats(RS485_SpeedTestInstanceId instance,
                              RS485_SpeedTestStats *stats);
uint8_t RS485_SpeedTest_IsRunning(RS485_SpeedTestInstanceId instance);

#ifdef __cplusplus
}
#endif

#endif
