#ifndef __RS485_SPEED_TEST_H
#define __RS485_SPEED_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rs422_protocol.h"

typedef enum
{
    RS485_SPEED_TEST_ROLE_MASTER = 0U,
    RS485_SPEED_TEST_ROLE_SLAVE = 1U
} RS485_SpeedTestRole;

typedef enum
{
    RS485_SPEED_TEST_STATE_IDLE = 0U,
    RS485_SPEED_TEST_STATE_MASTER_START_RATE,
    RS485_SPEED_TEST_STATE_MASTER_RUNNING,
    RS485_SPEED_TEST_STATE_MASTER_WAIT_BAUD_ACK,
    RS485_SPEED_TEST_STATE_MASTER_SWITCH_BAUD,
    RS485_SPEED_TEST_STATE_MASTER_SETTLE_BAUD,
    RS485_SPEED_TEST_STATE_MASTER_RATE_FAIL,
    RS485_SPEED_TEST_STATE_MASTER_DONE,
    RS485_SPEED_TEST_STATE_SLAVE_BASELINE_READY,
    RS485_SPEED_TEST_STATE_SLAVE_PENDING_BAUD,
    RS485_SPEED_TEST_STATE_SLAVE_TARGET_ACTIVE,
    RS485_SPEED_TEST_STATE_SLAVE_RECOVER_BASELINE,
    RS485_SPEED_TEST_STATE_STOPPED,
    RS485_SPEED_TEST_STATE_ERROR
} RS485_SpeedTestState;

#ifndef RS485_SPEED_TEST_DEFAULT_ROLE
#define RS485_SPEED_TEST_DEFAULT_ROLE          RS485_SPEED_TEST_ROLE_MASTER
#endif

#ifndef RS485_SPEED_TEST_LOCAL_NODE_ID
#define RS485_SPEED_TEST_LOCAL_NODE_ID         0U
#endif

#define RS485_SPEED_TEST_DEFAULT_BAUDRATE      2000000U
#define RS485_SPEED_TEST_BAUD_COUNT            1U
#define RS485_SPEED_TEST_MASTER_SLAVE_COUNT    2U
#define RS485_SPEED_TEST_FRAME_TIMEOUT_MS      120U
#define RS485_SPEED_TEST_BAUD_ACK_TIMEOUT_MS   800U
#define RS485_SPEED_TEST_BAUD_GUARD_MS         100U
#define RS485_SPEED_TEST_WINDOW_MS             60000U
#define RS485_SPEED_TEST_MIN_ECHO_FRAMES       1000U
#define RS485_SPEED_TEST_ERROR_RATE_LIMIT      0U
#define RS485_SPEED_TEST_DATA_TYPE             ((uint8_t)RS422_PROTOCOL_TYPE_55)

typedef enum
{
    RS485_SPEED_TEST_INSTANCE_UART1 = 0U,
    RS485_SPEED_TEST_INSTANCE_UART3,
    RS485_SPEED_TEST_INSTANCE_COUNT
} RS485_SpeedTestInstanceId;

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

extern const uint32_t rs485_speed_test_baud_table[RS485_SPEED_TEST_BAUD_COUNT];
extern const uint8_t rs485_speed_test_master_slave_table[RS485_SPEED_TEST_MASTER_SLAVE_COUNT];

extern volatile uint32_t rs485_speed_test_local_node_id;
extern volatile uint32_t rs485_speed_test_current_slave_id;
extern volatile uint32_t rs485_speed_test_current_slave_index;
extern volatile uint32_t rs485_speed_test_ignored_node_frames;
extern volatile uint32_t rs485_speed_test_role;
extern volatile uint32_t rs485_speed_test_state;
extern volatile uint32_t rs485_speed_test_current_index;
extern volatile uint32_t rs485_speed_test_current_baud;
extern volatile uint32_t rs485_speed_test_current_actual_baud;
extern volatile uint32_t rs485_speed_test_next_baud;
extern volatile uint32_t rs485_speed_test_best_stable_baud;
extern volatile uint32_t rs485_speed_test_best_stable_actual_baud;
extern volatile uint32_t rs485_speed_test_failed_baud;
extern volatile uint32_t rs485_speed_test_window_elapsed_ms;
extern volatile uint32_t rs485_speed_test_tx_frames;
extern volatile uint32_t rs485_speed_test_rx_frames;
extern volatile uint32_t rs485_speed_test_echo_frames;
extern volatile uint32_t rs485_speed_test_timeout_count;
extern volatile uint32_t rs485_speed_test_protocol_error_count;
extern volatile uint32_t rs485_speed_test_app_error_count;
extern volatile uint32_t rs485_speed_test_uart_error_count;
extern volatile uint32_t rs485_speed_test_rx_overflow_count;
extern volatile uint32_t rs485_speed_test_tx_overflow_count;
extern volatile uint32_t rs485_speed_test_rx_bytes_per_second;
extern volatile uint32_t rs485_speed_test_rx_bits_per_second;
extern volatile uint32_t rs485_speed_test_payload_bits_per_second;
extern volatile uint32_t rs485_speed_test_protocol_error_rate_ppm;
extern volatile uint32_t rs485_speed_test_sequence;
extern volatile uint32_t rs485_speed_test_last_rx_sequence;
extern volatile uint32_t rs485_speed_test_last_rx_command;
extern volatile uint32_t rs485_speed_test_last_status;
extern volatile uint32_t rs485_speed_test_last_hal_status;
extern volatile uint32_t rs485_speed_test_waiting_echo;
extern volatile uint32_t rs485_speed_test_stable;

void RS485_SpeedTest_Init(void);
void RS485_SpeedTest_Run(void);
void RS485_SpeedTest_SetRole(RS485_SpeedTestRole role);

void RS485_SpeedTest_Start(RS485_SpeedTestInstanceId instance, uint32_t baudrate);
void RS485_SpeedTest_Stop(RS485_SpeedTestInstanceId instance);
void RS485_SpeedTest_StopAll(void);
void RS485_SpeedTest_GetStats(RS485_SpeedTestInstanceId instance,
                              RS485_SpeedTestStats *stats);
uint8_t RS485_SpeedTest_IsRunning(RS485_SpeedTestInstanceId instance);

#ifdef __cplusplus
}
#endif

#endif
