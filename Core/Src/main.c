/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ads7951.h"
#include "rs422.h"
#include "rs422_protocol.h"
#include "ssi422.h"

#include <stdarg.h>
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* RS422 协议环回测试开关：0 为正常对外程序，1 为 J12/J13 互连自发自收测试。 */
#define RS422_PROTOCOL_TEST_ENABLE           0U

#if (RS422_PROTOCOL_TEST_ENABLE != 0U)
#define RS422_PROTOCOL_TEST_INTERVAL_MS      1000U
#define RS422_PROTOCOL_TEST_TX_TIMEOUT_MS    200U
#define RS422_PROTOCOL_TEST_ID_U9            1U
#define RS422_PROTOCOL_TEST_ID_U10           2U
#endif
/* RS422 串口1示例开关：0 为不运行示例，1 为通过 U9/USART1 周期发包并轮询接收。 */
#define RS422_UART1_EXAMPLE_ENABLE           0U

#if (RS422_UART1_EXAMPLE_ENABLE != 0U)
#define RS422_UART1_EXAMPLE_INTERVAL_MS      1000U
#define RS422_UART1_EXAMPLE_TX_TIMEOUT_MS    200U
#define RS422_UART1_EXAMPLE_ID               1U
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint16_t adc_u6_ch0 = 0U;
uint16_t adc_u8_ch3 = 0U;
uint16_t adc_u6_values[ADS7951_CHANNEL_COUNT] = {0U};
uint16_t adc_u8_values[ADS7951_CHANNEL_COUNT] = {0U};
HAL_StatusTypeDef adc_u6_status = HAL_OK;
HAL_StatusTypeDef adc_u8_status = HAL_OK;
uint32_t ssi_u16_value = 0U;
uint32_t ssi_u18_value = 0U;
HAL_StatusTypeDef ssi_u16_status = HAL_OK;
HAL_StatusTypeDef ssi_u18_status = HAL_OK;
#if (RS422_PROTOCOL_TEST_ENABLE != 0U)
/* RS422 协议层调试变量：保存最后一次接收到的完整协议包和统计结果。 */
RS422_ProtocolPacket rs422_u9_rx_packet = {0U};
RS422_ProtocolPacket rs422_u10_rx_packet = {0U};
RS422_ProtocolStats rs422_u9_rx_stats = {0U};
RS422_ProtocolStats rs422_u10_rx_stats = {0U};
RS422_ProtocolStatus rs422_u9_poll_status = RS422_PROTOCOL_STATUS_NO_FRAME;
RS422_ProtocolStatus rs422_u10_poll_status = RS422_PROTOCOL_STATUS_NO_FRAME;
RS422_ProtocolStatus rs422_u9_last_status = RS422_PROTOCOL_STATUS_NO_FRAME;
RS422_ProtocolStatus rs422_u10_last_status = RS422_PROTOCOL_STATUS_NO_FRAME;
/* RS422 协议测试变量：用于 J12/J13 互连时定时发送固定数据并观察发送状态。 */
uint8_t rs422_protocol_test_u9_data[RS422_PROTOCOL_DATA_LENGTH] = {0U};
uint8_t rs422_protocol_test_u10_data[RS422_PROTOCOL_DATA_LENGTH] = {0U};
HAL_StatusTypeDef rs422_protocol_test_u9_tx_status = HAL_OK;
HAL_StatusTypeDef rs422_protocol_test_u10_tx_status = HAL_OK;
uint32_t rs422_protocol_test_tx_count = 0U;
#endif
#if (RS422_UART1_EXAMPLE_ENABLE != 0U)
/* RS422 串口1示例变量：用于 Keil Watch 观察 USART1/U9 的协议收发结果。 */
uint8_t rs422_uart1_example_tx_data[RS422_PROTOCOL_DATA_LENGTH] = {0U};
RS422_ProtocolPacket rs422_uart1_example_rx_packet = {0U};
RS422_ProtocolStats rs422_uart1_example_stats = {0U};
HAL_StatusTypeDef rs422_uart1_example_tx_status = HAL_OK;
RS422_ProtocolStatus rs422_uart1_example_rx_status = RS422_PROTOCOL_STATUS_NO_FRAME;
uint32_t rs422_uart1_example_tx_count = 0U;
#endif

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
#if (RS422_PROTOCOL_TEST_ENABLE != 0U)
static void Debug_Printf(const char *format, ...);
static void RS422_ProtocolTestInitData(void);
static void RS422_ProtocolTestRun(void);
static void RS422_ProtocolTestPrintPacket(const char *name,
                                          const RS422_ProtocolPacket *packet,
                                          const RS422_ProtocolStats *stats,
                                          RS422_ProtocolStatus status);
#endif
#if (RS422_UART1_EXAMPLE_ENABLE != 0U)
static void RS422_Uart1ExampleInit(void);
static void RS422_Uart1ExampleRun(void);
#endif

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#if (RS422_PROTOCOL_TEST_ENABLE != 0U)
/**
  * @brief  通过 USART2 输出格式化调试信息，方便 UARTAssist 观察协议测试结果。
  * @param  format: printf 风格格式字符串。
  * @retval None
  */
static void Debug_Printf(const char *format, ...)
{
  char buffer[192];
  va_list args;
  int length;

  va_start(args, format);
  length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (length <= 0)
  {
    return;
  }
  if (length >= (int)sizeof(buffer))
  {
    /* 调试字符串过长时只发送已写入缓冲区的有效内容，避免把结尾空字符发出去。 */
    length = (int)sizeof(buffer) - 1;
  }

  (void)HAL_UART_Transmit(&huart2, (uint8_t *)buffer, (uint16_t)length, 100U);
}

/**
  * @brief  初始化 RS422 协议测试用的 128 字节固定数据区。
  * @retval None
  */
static void RS422_ProtocolTestInitData(void)
{
  uint16_t index;

  for (index = 0U; index < RS422_PROTOCOL_DATA_LENGTH; index++)
  {
    rs422_protocol_test_u9_data[index] = (uint8_t)index;
    rs422_protocol_test_u10_data[index] = (uint8_t)(0xFFU - index);
  }
}

/**
  * @brief  打印一帧协议接收摘要和当前统计信息。
  * @param  name: 当前 RS422 端口名称字符串。
  * @param  packet: 最后一次成功解析的协议包。
  * @param  stats: 当前协议层接收统计。
  * @param  status: 最近一次协议轮询状态。
  * @retval None
  */
static void RS422_ProtocolTestPrintPacket(const char *name,
                                          const RS422_ProtocolPacket *packet,
                                          const RS422_ProtocolStats *stats,
                                          RS422_ProtocolStatus status)
{
  Debug_Printf("%s status=%u id=%u type=0x%02X dir=%u len=%u chk=0x%02X total=%lu good=%lu err=%lu ppm=%lu\r\n",
               name,
               (unsigned int)status,
               (unsigned int)packet->id,
               (unsigned int)packet->data_type,
               (unsigned int)packet->direction,
               (unsigned int)packet->length,
               (unsigned int)packet->checksum,
               (unsigned long)stats->total_bytes,
               (unsigned long)stats->good_frames,
               (unsigned long)stats->error_frames,
               (unsigned long)stats->error_rate_ppm);
}

/**
  * @brief  周期执行 J12/J13 互连状态下的 RS422 协议收发测试并打印结果。
  * @note   U9 和 U10 每 1 秒各发送一帧，USART2 负责向 UARTAssist 打印摘要。
  * @retval None
  */
static void RS422_ProtocolTestRun(void)
{
  static uint32_t last_tick = 0U;
  uint32_t now = HAL_GetTick();

  rs422_u9_poll_status = RS422_ProtocolPoll(RS422_PORT_U9, &rs422_u9_rx_packet);
  rs422_u10_poll_status = RS422_ProtocolPoll(RS422_PORT_U10, &rs422_u10_rx_packet);
  if (rs422_u9_poll_status != RS422_PROTOCOL_STATUS_NO_FRAME)
  {
    rs422_u9_last_status = rs422_u9_poll_status;
  }
  if (rs422_u10_poll_status != RS422_PROTOCOL_STATUS_NO_FRAME)
  {
    rs422_u10_last_status = rs422_u10_poll_status;
  }
  RS422_ProtocolGetStats(RS422_PORT_U9, &rs422_u9_rx_stats);
  RS422_ProtocolGetStats(RS422_PORT_U10, &rs422_u10_rx_stats);

  if ((now - last_tick) < RS422_PROTOCOL_TEST_INTERVAL_MS)
  {
    return;
  }
  last_tick = now;

  rs422_protocol_test_u9_tx_status =
      RS422_ProtocolSend(RS422_PORT_U9,
                         RS422_PROTOCOL_TEST_ID_U9,
                         (uint8_t)RS422_PROTOCOL_TYPE_55,
                         rs422_protocol_test_u9_data,
                         RS422_PROTOCOL_TEST_TX_TIMEOUT_MS);
  rs422_protocol_test_u10_tx_status =
      RS422_ProtocolSend(RS422_PORT_U10,
                         RS422_PROTOCOL_TEST_ID_U10,
                         (uint8_t)RS422_PROTOCOL_TYPE_AA,
                         rs422_protocol_test_u10_data,
                         RS422_PROTOCOL_TEST_TX_TIMEOUT_MS);
  rs422_protocol_test_tx_count++;

  Debug_Printf("RS422 proto test #%lu tx_u9=%u tx_u10=%u\r\n",
               (unsigned long)rs422_protocol_test_tx_count,
               (unsigned int)rs422_protocol_test_u9_tx_status,
               (unsigned int)rs422_protocol_test_u10_tx_status);
  RS422_ProtocolTestPrintPacket("U9 ", &rs422_u9_rx_packet, &rs422_u9_rx_stats, rs422_u9_last_status);
  RS422_ProtocolTestPrintPacket("U10", &rs422_u10_rx_packet, &rs422_u10_rx_stats, rs422_u10_last_status);
}
#endif

#if (RS422_UART1_EXAMPLE_ENABLE != 0U)
/**
  * @brief  初始化 RS422 串口1示例数据，串口1在本工程中对应 U9/USART1/J12。
  * @retval None
  */
static void RS422_Uart1ExampleInit(void)
{
  uint16_t index;

  for (index = 0U; index < RS422_PROTOCOL_DATA_LENGTH; index++)
  {
    /* 示例数据使用递增字节，便于在接收端观察 data[0..127] 是否连续。 */
    rs422_uart1_example_tx_data[index] = (uint8_t)index;
  }

  RS422_ProtocolClearStats(RS422_PORT_U9);
}

/**
  * @brief  RS422 串口1协议收发示例：周期发送一帧，同时轮询接收完整协议包。
  * @note   串口1对应 RS422_PORT_U9；收到 OK 后可在此处添加业务处理逻辑。
  * @retval None
  */
static void RS422_Uart1ExampleRun(void)
{
  static uint32_t last_tx_tick = 0U;
  uint32_t now = HAL_GetTick();

  rs422_uart1_example_rx_status =
      RS422_ProtocolPoll(RS422_PORT_U9, &rs422_uart1_example_rx_packet);
  RS422_ProtocolGetStats(RS422_PORT_U9, &rs422_uart1_example_stats);

  if (rs422_uart1_example_rx_status == RS422_PROTOCOL_STATUS_OK)
  {
    /* 收到完整协议包后，在这里处理 rs422_uart1_example_rx_packet.data。 */
  }

  if ((now - last_tx_tick) < RS422_UART1_EXAMPLE_INTERVAL_MS)
  {
    return;
  }
  last_tx_tick = now;

  rs422_uart1_example_tx_status =
      RS422_ProtocolSend(RS422_PORT_U9,
                         RS422_UART1_EXAMPLE_ID,
                         (uint8_t)RS422_PROTOCOL_TYPE_55,
                         rs422_uart1_example_tx_data,
                         RS422_UART1_EXAMPLE_TX_TIMEOUT_MS);
  if (rs422_uart1_example_tx_status == HAL_OK)
  {
    rs422_uart1_example_tx_count++;
  }
}
#endif

/* USER CODE END 0 */

/**
  * @brief  应用程序入口，完成 HAL、时钟和外设初始化后进入主循环。
  * @retval int 当前程序不会主动返回。
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_SPI2_Init();
  MX_SPI4_Init();
  /* USER CODE BEGIN 2 */
  ADS7951_Init();
  RS422_Init();
#if (RS422_UART1_EXAMPLE_ENABLE != 0U)
  /* 示例模式只使用串口1/U9，正常运行时该宏保持 0，不主动发送示例协议帧。 */
  RS422_Uart1ExampleInit();
#endif
#if (RS422_PROTOCOL_TEST_ENABLE != 0U)
  /* 测试模式下清空协议统计并准备固定测试数据，正常运行时不主动发测试帧。 */
  RS422_ProtocolTestInitData();
  RS422_ProtocolClearStats(RS422_PORT_U9);
  RS422_ProtocolClearStats(RS422_PORT_U10);
#endif
  SSI422_Init();
#if (RS422_PROTOCOL_TEST_ENABLE != 0U)
  Debug_Printf("\r\nRS422 protocol test start, debug uart=USART2 115200 8N1\r\n");
#endif
  adc_u6_status = ADS7951_U6_ReadChannel(0U, &adc_u6_ch0);
  adc_u8_status = ADS7951_U8_ReadChannel(3U, &adc_u8_ch3);
  adc_u6_status = ADS7951_U6_ReadAll(adc_u6_values);
  adc_u8_status = ADS7951_U8_ReadAll(adc_u8_values);
  ssi_u16_status = SSI422_U16_ReadBits(16U, &ssi_u16_value);
  ssi_u18_status = SSI422_U18_ReadBits(16U, &ssi_u18_value);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    ssi_u16_status = SSI422_U16_ReadBits(16U, &ssi_u16_value);
    ssi_u18_status = SSI422_U18_ReadBits(16U, &ssi_u18_value);
#if (RS422_PROTOCOL_TEST_ENABLE != 0U)
    RS422_ProtocolTestRun();
#endif
#if (RS422_UART1_EXAMPLE_ENABLE != 0U)
    RS422_Uart1ExampleRun();
#endif
  }
  /* USER CODE END 3 */
}

/**
  * @brief  配置系统主时钟、总线时钟以及 SPI/UART 外设时钟源。
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART3|RCC_PERIPHCLK_USART2
                              |RCC_PERIPHCLK_USART1|RCC_PERIPHCLK_SPI4
                              |RCC_PERIPHCLK_SPI3|RCC_PERIPHCLK_SPI2;
  PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
  PeriphClkInitStruct.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK1;
  PeriphClkInitStruct.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
  PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /**
  * @brief  定时器周期到达回调，用 TIM6 维护 HAL 的 1 ms 系统节拍。
  * @param  htim: 触发回调的定时器句柄。
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  HAL 或外设初始化出错时进入的错误处理函数。
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  断言失败回调，用于定位参数检查失败的源码位置。
  * @param  file: 发生断言失败的源文件名。
  * @param  line: 发生断言失败的源代码行号。
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
