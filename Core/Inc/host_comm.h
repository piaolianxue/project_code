#ifndef __HOST_COMM_H
#define __HOST_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define HOST_COMM_FRAME_HEADER                 0xEEU//帧头
#define HOST_COMM_FRAME_TAIL_0                 0xFFU//帧尾0
#define HOST_COMM_FRAME_TAIL_1                 0xFCU//帧尾1
#define HOST_COMM_FRAME_TAIL_2                 0xFFU//帧尾2
#define HOST_COMM_FRAME_TAIL_3                 0xFFU//帧尾3
#define HOST_COMM_MAX_FRAME_LENGTH             1024U//最大帧长度
#define HOST_COMM_MAX_BODY_LENGTH              (HOST_COMM_MAX_FRAME_LENGTH - 1U - 4U)//最大帧体长度
#define HOST_COMM_ERROR_RATE_SCALE             1000000U//错误率缩放
#define HOST_COMM_DEFAULT_BAUDRATE             115200U//默认波特率
#define HOST_COMM_TEST_MODE_RS422              0U//测试模式 RS422
#define HOST_COMM_TEST_MODE_RS485              1U//测试模式 RS485
#define HOST_COMM_TEST_MODE_DEFAULT            HOST_COMM_TEST_MODE_RS422//默认测试模式
#define HOST_COMM_UPLOAD_INTERVAL_MS           1000U//上传间隔时间
#define HOST_COMM_UPLOAD_VALUE_DISPLAY_LENGTH  6U//上传值显示长度
#define HOST_COMM_UPLOAD_FRAME_LENGTH          (1U + 2U + 4U + HOST_COMM_UPLOAD_VALUE_DISPLAY_LENGTH + 4U)//上传帧长度
#define HOST_COMM_UPLOAD_HEX_LENGTH            (HOST_COMM_UPLOAD_FRAME_LENGTH * 3U)//上传帧十六进制长度
#define HOST_COMM_UPLOAD_DEBUG_HISTORY_SIZE    12U//上传调试历史记录大小

#define HOST_COMM_DOWNLOAD_INSTRUCTION         0xB111U//下载指令
#define HOST_COMM_UPLOAD_INSTRUCTION           0xB110U//上传指令
#define HOST_COMM_CONTROL_TYPE_TEXT            0x11U//文本控件
#define HOST_COMM_CONTROL_TYPE_BUTTON          0x10U//按钮控件

typedef struct
{
    uint32_t screen_rx_total_bytes;
    uint32_t screen_tx_total_bytes;
    uint32_t screen_error_bytes;
    uint32_t screen_error_rate_ppm;
    uint8_t uart_enabled[4U];
    uint32_t uart_baudrate[4U];
    uint8_t test_running;
} HostComm_Stats;//主机通信统计信息结构体

extern volatile uint32_t host_comm_rx_total_bytes;//接收总字节数
extern volatile uint32_t host_comm_tx_total_bytes;//发送总字节数
extern volatile uint32_t host_comm_error_bytes;//错误字节数
extern volatile uint32_t host_comm_error_rate_ppm;//错误率（百万分之一）
extern volatile uint32_t host_comm_baudrate;//波特率
extern volatile uint8_t host_comm_uart1_enabled;//UART1使能标志
extern volatile uint8_t host_comm_uart2_enabled;//UART2使能标志
extern volatile uint8_t host_comm_uart3_enabled;//UART3使能标志
extern volatile uint8_t host_comm_uart4_enabled;//UART4使能标志
extern volatile uint32_t host_comm_uart1_baudrate;//UART1波特率
extern volatile uint32_t host_comm_uart2_baudrate;//UART2波特率
extern volatile uint32_t host_comm_uart3_baudrate;//UART3波特率
extern volatile uint32_t host_comm_uart4_baudrate;//UART4波特率
extern volatile uint8_t host_comm_test_running;//测试运行标志
extern volatile uint16_t host_comm_last_instruction;//最后接收的指令
extern volatile uint32_t host_comm_last_control_id;//最后接收的控件ID
extern volatile uint32_t host_comm_send_stats_count;//发送统计信息计数
extern volatile uint32_t host_comm_upload_frame_count;//上传帧计数
extern volatile uint32_t host_comm_last_upload_control_id;//最后上传的控件ID
extern volatile uint32_t host_comm_last_upload_value;//最后上传的控件值
extern volatile uint16_t host_comm_last_upload_length;//最后上传的帧长度
extern volatile uint8_t host_comm_last_upload_frame[HOST_COMM_UPLOAD_FRAME_LENGTH];//最后上传的帧数据
extern volatile char host_comm_last_upload_hex[HOST_COMM_UPLOAD_HEX_LENGTH];//最后上传的帧十六进制字符串
extern volatile uint8_t host_comm_upload_debug_write_index;//上传调试写入索引
extern volatile uint32_t host_comm_upload_debug_control_id[HOST_COMM_UPLOAD_DEBUG_HISTORY_SIZE];//上传调试控件ID
extern volatile uint32_t host_comm_upload_debug_value[HOST_COMM_UPLOAD_DEBUG_HISTORY_SIZE];//上传调试控件值
extern volatile uint16_t host_comm_upload_debug_length[HOST_COMM_UPLOAD_DEBUG_HISTORY_SIZE];//上传调试帧长度
extern volatile uint8_t host_comm_upload_debug_frame[HOST_COMM_UPLOAD_DEBUG_HISTORY_SIZE][HOST_COMM_UPLOAD_FRAME_LENGTH];//上传调试帧数据
extern volatile char host_comm_upload_debug_hex[HOST_COMM_UPLOAD_DEBUG_HISTORY_SIZE][HOST_COMM_UPLOAD_HEX_LENGTH];//上传调试帧十六进制字符串
extern volatile HAL_StatusTypeDef host_comm_last_tx_status;//最后发送状态
extern volatile HAL_StatusTypeDef host_comm_start_receive_status;//开始接收状态

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
