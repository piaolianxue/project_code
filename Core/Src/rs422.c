#include "rs422.h"
#include "host_comm.h"
#include "usart.h"

typedef struct
{
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *tx_en_port;
    uint16_t tx_en_pin;
    GPIO_PinState tx_en_active;
    IRQn_Type irqn;
} RS422_PortConfig;

typedef struct
{
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;

    uint8_t rx_byte;
    uint8_t tx_byte;
    volatile uint8_t tx_busy;
} RS422_PortState;// RS422 端口状态

static const RS422_PortConfig rs422_config[RS422_PORT_COUNT] =
{
    /* TX_EN_x 的有效电平决定收发器进入发送模式时输出的方向控制电平。 */
    {
        &huart1,
        TX_EN_1_GPIO_Port, TX_EN_1_Pin, GPIO_PIN_SET,
        USART1_IRQn
    },
    {
        /* U10 的 THVD1452 实际接到 UART3_RX/TX，因此这里使用 USART3 句柄。 */
        &huart3,
        TX_EN_2_GPIO_Port, TX_EN_2_Pin, GPIO_PIN_SET,
        USART3_IRQn
    }
};

static RS422_PortState rs422_state[RS422_PORT_COUNT];// RS422 端口状态数组
static uint8_t rs422_rx_buffer[RS422_PORT_COUNT][RS422_RX_BUFFER_SIZE];
static uint8_t rs422_tx_buffer[RS422_PORT_COUNT][RS422_TX_BUFFER_SIZE];

/* 以下诊断变量用于确认 RS422 UART 是否真的进入发送、接收或错误回调。 */
volatile uint32_t rs422_diag_tx_complete_count[RS422_PORT_COUNT] = {0U};// 发送完成次数
volatile uint32_t rs422_diag_rx_complete_count[RS422_PORT_COUNT] = {0U};// 接收完成次数
volatile uint32_t rs422_diag_error_count[RS422_PORT_COUNT] = {0U};
volatile uint32_t rs422_diag_abort_count[RS422_PORT_COUNT] = {0U};// 异常中止次数
volatile uint32_t rs422_diag_last_error_code[RS422_PORT_COUNT] = {0U};
volatile uint32_t rs422_diag_last_rx_byte[RS422_PORT_COUNT] = {0U};
volatile uint32_t rs422_diag_start_receive_status[RS422_PORT_COUNT] = {0U};// HAL_UART_Receive_IT 返回值
volatile uint32_t rs422_diag_rx_overflow_count[RS422_PORT_COUNT] = {0U};
volatile uint32_t rs422_diag_tx_overflow_count[RS422_PORT_COUNT] = {0U};
volatile uint32_t rs422_diag_tx_total_bytes[RS422_PORT_COUNT] = {0U};
volatile uint32_t rs422_diag_requested_baud[RS422_PORT_COUNT] = {0U};
volatile uint32_t rs422_diag_actual_baud[RS422_PORT_COUNT] = {0U};

/**
  * @brief  判断 RS422 端口编号是否在有效范围内。
  * @param  port: RS422 端口枚举值。
  * @retval 1 表示有效，0 表示无效。
  */
static uint8_t RS422_IsValidPort(RS422_PortId port)
{
    return ((uint32_t)port < (uint32_t)RS422_PORT_COUNT) ? 1U : 0U;
}

/**
  * @brief  计算环形缓冲区的下一个索引位置。
  * @param  index: 当前索引。
  * @param  size: 环形缓冲区总长度。
  * @retval 递增并自动回绕后的索引。
  */
static uint16_t RS422_RingNext(uint16_t index, uint16_t size)
{
    index++;
    if (index >= size)
    {
        index = 0U;
    }

    return index;
}

/**
  * @brief  计算环形缓冲区中已经使用的字节数。
  * @param  head: 写入指针位置。
  * @param  tail: 读取指针位置。
  * @param  size: 环形缓冲区总长度。
  * @retval 当前已缓存的数据字节数。
  */
static uint16_t RS422_RingUsed(uint16_t head, uint16_t tail, uint16_t size)
{
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }

    return (uint16_t)(size - tail + head);
}

/**
  * @brief  计算环形缓冲区剩余可写空间。
  * @param  head: 写入指针位置。
  * @param  tail: 读取指针位置。
  * @param  size: 环形缓冲区总长度。
  * @retval 可继续写入的字节数，保留 1 字节用于区分满/空。
  */
static uint16_t RS422_RingFree(uint16_t head, uint16_t tail, uint16_t size)
{
    return (uint16_t)(size - 1U - RS422_RingUsed(head, tail, size));
}

/**
  * @brief  将接收到的 1 字节压入指定端口的接收环形缓冲区。
  * @param  port: RS422 端口枚举值。
  * @param  data: 要写入接收缓冲区的字节。
  * @retval None
  */
static void RS422_RxPush(RS422_PortId port, uint8_t data)
{
    RS422_PortState *state = &rs422_state[port];
    uint16_t next = RS422_RingNext(state->rx_head, RS422_RX_BUFFER_SIZE);

    if (next != state->rx_tail)
    {
        rs422_rx_buffer[port][state->rx_head] = data;
        state->rx_head = next;
    }
    else
    {
        rs422_diag_rx_overflow_count[port]++;
    }
}

/**
  * @brief  从指定端口的发送环形缓冲区取出 1 字节。
  * @param  port: RS422 端口枚举值。
  * @param  data: 返回取出的字节。
  * @retval 1 表示取到数据，0 表示发送缓冲区为空。
  */
static uint8_t RS422_TxPop(RS422_PortId port, uint8_t *data)
{
    RS422_PortState *state = &rs422_state[port];

    if (state->tx_head == state->tx_tail)
    {
        return 0U;
    }

    *data = rs422_tx_buffer[port][state->tx_tail];
    state->tx_tail = RS422_RingNext(state->tx_tail, RS422_TX_BUFFER_SIZE);

    return 1U;
}

/**
  * @brief  将指定 RS422 收发器切换到接收模式。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
static void RS422_SetReceiveMode(RS422_PortId port)
{
    const RS422_PortConfig *config = &rs422_config[port];

    HAL_GPIO_WritePin(config->tx_en_port, config->tx_en_pin,
                      (config->tx_en_active == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/**
  * @brief  将指定 RS422 收发器切换到发送模式。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
static void RS422_SetTransmitMode(RS422_PortId port)
{
    const RS422_PortConfig *config = &rs422_config[port];

    HAL_GPIO_WritePin(config->tx_en_port, config->tx_en_pin, config->tx_en_active);
}

/**
  * @brief  启动或续接指定端口的中断发送流程。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
static void RS422_KickTx(RS422_PortId port)
{
    RS422_PortState *state = &rs422_state[port];

    if (state->tx_busy != 0U)
    {
        return;
    }

    if (RS422_TxPop(port, &state->tx_byte) == 0U)// 发送缓冲区为空
    {
        RS422_SetReceiveMode(port);
        return;
    }

    RS422_SetTransmitMode(port);
    state->tx_busy = 1U;

    if (HAL_UART_Transmit_IT(rs422_config[port].huart, &state->tx_byte, 1U) != HAL_OK)
    {
        state->tx_busy = 0U;
        RS422_SetReceiveMode(port);
    }
}

/**
  * @brief  根据 HAL UART 句柄查找对应的 RS422 端口。
  * @param  huart: HAL UART 句柄地址。
  * @retval 匹配的端口编号；未匹配时返回 RS422_PORT_COUNT。
  */
static RS422_PortId RS422_FindPortByUart(UART_HandleTypeDef *huart)
{
    RS422_PortId port;

    for (port = (RS422_PortId)0; port < RS422_PORT_COUNT; port++)
    {
        if (rs422_config[port].huart == huart)
        {
            return port;
        }
    }

    return RS422_PORT_COUNT;
}

static uint32_t RS422_GetKernelClock(UART_HandleTypeDef *huart)
{
    UART_ClockSourceTypeDef clocksource;
    PLL2_ClocksTypeDef pll2_clocks;
    PLL3_ClocksTypeDef pll3_clocks;

    UART_GETCLOCKSOURCE(huart, clocksource);

    switch (clocksource)
    {
        case UART_CLOCKSOURCE_D2PCLK1:
            return HAL_RCC_GetPCLK1Freq();

        case UART_CLOCKSOURCE_D2PCLK2:
            return HAL_RCC_GetPCLK2Freq();

        case UART_CLOCKSOURCE_PLL2:
            HAL_RCCEx_GetPLL2ClockFreq(&pll2_clocks);
            return pll2_clocks.PLL2_Q_Frequency;

        case UART_CLOCKSOURCE_PLL3:
            HAL_RCCEx_GetPLL3ClockFreq(&pll3_clocks);
            return pll3_clocks.PLL3_Q_Frequency;

        case UART_CLOCKSOURCE_HSI:
            if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIDIV) != 0U)
            {
                return (uint32_t)(HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3U));
            }
            return (uint32_t)HSI_VALUE;

        case UART_CLOCKSOURCE_CSI:
            return (uint32_t)CSI_VALUE;

        case UART_CLOCKSOURCE_LSE:
            return (uint32_t)LSE_VALUE;

        default:
            return 0U;
    }
}

static void RS422_ResetPortState(RS422_PortId port)
{
    rs422_state[port].rx_head = 0U;
    rs422_state[port].rx_tail = 0U;
    rs422_state[port].tx_head = 0U;
    rs422_state[port].tx_tail = 0U;
    rs422_state[port].tx_busy = 0U;
}

static uint32_t RS422_CalcActualBaud(UART_HandleTypeDef *huart, uint32_t clock);

static uint32_t RS422_ReadActualBaud(UART_HandleTypeDef *huart)
{
    uint32_t clock;

    clock = RS422_GetKernelClock(huart);
    if (clock == 0U)
    {
        return huart->Init.BaudRate;
    }

    return RS422_CalcActualBaud(huart, clock);
}

static void RS422_UpdateBaudDiagnostics(RS422_PortId port)
{
    rs422_diag_requested_baud[port] = rs422_config[port].huart->Init.BaudRate;
    rs422_diag_actual_baud[port] = RS422_ReadActualBaud(rs422_config[port].huart);
}

static uint32_t RS422_GetEffectivePrescaler(UART_HandleTypeDef *huart)
{
    uint32_t index = (uint32_t)huart->Init.ClockPrescaler;

    if (index < (uint32_t)(sizeof(UARTPrescTable) / sizeof(UARTPrescTable[0])))
    {
        return UARTPrescTable[index];
    }

    return 1U;
}

static uint32_t RS422_CalcActualBaud(UART_HandleTypeDef *huart, uint32_t clock)
{
    uint32_t prescaler;
    uint64_t scaled_clock;
    uint32_t usartdiv;

    if (clock == 0U)
    {
        return 0U;
    }

    prescaler = RS422_GetEffectivePrescaler(huart);
    if (prescaler == 0U)
    {
        return 0U;
    }

    scaled_clock = (uint64_t)clock / (uint64_t)prescaler;
    if (scaled_clock == 0ULL)
    {
        return 0U;
    }

    if (UART_INSTANCE_LOWPOWER(huart))
    {
        usartdiv = huart->Instance->BRR;
        if (usartdiv == 0U)
        {
            return 0U;
        }

        return (uint32_t)(((scaled_clock * 256ULL) + ((uint64_t)usartdiv / 2ULL)) /
                          (uint64_t)usartdiv);
    }

    if (huart->Init.OverSampling == UART_OVERSAMPLING_8)
    {
        usartdiv = (uint32_t)((huart->Instance->BRR & 0xFFF0U) |
                              ((huart->Instance->BRR & 0x0007U) << 1U));
        if (usartdiv == 0U)
        {
            return 0U;
        }

        return (uint32_t)((((scaled_clock * 2ULL) + ((uint64_t)usartdiv / 2ULL)) /
                           (uint64_t)usartdiv));
    }

    usartdiv = huart->Instance->BRR;
    if (usartdiv == 0U)
    {
        return 0U;
    }

    return (uint32_t)((scaled_clock + ((uint64_t)usartdiv / 2ULL)) /
                      (uint64_t)usartdiv);
}

/**
  * @brief  初始化全部 RS422 端口状态并启动中断接收。
  * @retval None
  */
void RS422_Init(void)
{
    RS422_PortId port;

    for (port = (RS422_PortId)0; port < RS422_PORT_COUNT; port++)
    {
        RS422_ResetPortState(port);
        rs422_diag_tx_complete_count[port] = 0U;
        rs422_diag_rx_complete_count[port] = 0U;
        rs422_diag_error_count[port] = 0U;
        rs422_diag_abort_count[port] = 0U;
        rs422_diag_rx_overflow_count[port] = 0U;
        rs422_diag_tx_overflow_count[port] = 0U;
        rs422_diag_tx_total_bytes[port] = 0U;
        rs422_diag_last_error_code[port] = 0U;
        rs422_diag_last_rx_byte[port] = 0U;
        rs422_diag_start_receive_status[port] = 0U;
        RS422_SetReceiveMode(port);
        RS422_UpdateBaudDiagnostics(port);
    }

    (void)RS422_StartReceiveAll();
}

/**
  * @brief  启动指定 RS422 端口的单字节中断接收。
  * @param  port: RS422 端口枚举值。
  * @retval HAL 状态，HAL_OK/HAL_BUSY 由底层 UART 接收接口返回。
  */
HAL_StatusTypeDef RS422_StartReceive(RS422_PortId port)
{
    RS422_PortState *state;
    HAL_StatusTypeDef status;

    if (RS422_IsValidPort(port) == 0U)
    {
        return HAL_ERROR;
    }

    RS422_SetReceiveMode(port);
    state = &rs422_state[port];

    status = HAL_UART_Receive_IT(rs422_config[port].huart, &state->rx_byte, 1U);
    rs422_diag_start_receive_status[port] = (uint32_t)status;
    return status;
}

/**
  * @brief  依次启动所有 RS422 端口的中断接收。
  * @retval HAL 状态，所有端口正常或忙时返回 HAL_OK。
  */
HAL_StatusTypeDef RS422_StartReceiveAll(void)
{
    HAL_StatusTypeDef status = HAL_OK;
    HAL_StatusTypeDef item_status;
    RS422_PortId port;

    for (port = (RS422_PortId)0; port < RS422_PORT_COUNT; port++)
    {
        item_status = RS422_StartReceive(port);
        if ((item_status != HAL_OK) && (item_status != HAL_BUSY))
        {
            status = item_status;
        }
    }

    return status;
}

/**
  * @brief  将数据写入发送缓冲区并通过中断方式发送。
  * @param  port: RS422 端口枚举值。
  * @param  data: 待发送数据缓冲区。
  * @param  size: 待发送字节数。
  * @retval HAL_OK 表示成功入队，HAL_BUSY 表示发送缓冲区空间不足。
  */
HAL_StatusTypeDef RS422_Transmit_IT(RS422_PortId port, const uint8_t *data, uint16_t size)
{
    RS422_PortState *state;
    uint16_t index;

    if ((RS422_IsValidPort(port) == 0U) || (data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    state = &rs422_state[port];

    __disable_irq();
    if (RS422_RingFree(state->tx_head, state->tx_tail, RS422_TX_BUFFER_SIZE) < size)
    {
        rs422_diag_tx_overflow_count[port]++;
        __enable_irq();
        return HAL_BUSY;
    }

    for (index = 0U; index < size; index++)
    {
        rs422_tx_buffer[port][state->tx_head] = data[index];
        state->tx_head = RS422_RingNext(state->tx_head, RS422_TX_BUFFER_SIZE);
    }
    rs422_diag_tx_total_bytes[port] += size;
    __enable_irq();

    RS422_KickTx(port);
    return HAL_OK;
}

/**
  * @brief  以阻塞等待方式发送 RS422 数据。
  * @param  port: RS422 端口枚举值。
  * @param  data: 待发送数据缓冲区。
  * @param  size: 待发送字节数。
  * @param  timeout: 等待发送完成的超时时间，单位 ms。
  * @retval HAL 状态，超时未发完时返回 HAL_TIMEOUT。
  */
HAL_StatusTypeDef RS422_Transmit(RS422_PortId port, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    HAL_StatusTypeDef status;

    status = RS422_Transmit_IT(port, data, size);
    if (status != HAL_OK)
    {
        return status;
    }

    while (RS422_IsTxBusy(port) != 0U)
    {
        if ((HAL_GetTick() - start) > timeout)
        {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef RS422_SetBaudRate(RS422_PortId port, uint32_t baud_rate)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;
    uint32_t old_baud_rate;
    uint8_t irq_was_enabled;

    if ((RS422_IsValidPort(port) == 0U) || (baud_rate == 0U))
    {
        return HAL_ERROR;
    }

    huart = rs422_config[port].huart;
    old_baud_rate = huart->Init.BaudRate;
    irq_was_enabled = (uint8_t)((NVIC_GetEnableIRQ(rs422_config[port].irqn) != 0U) ? 1U : 0U);

    HAL_NVIC_DisableIRQ(rs422_config[port].irqn);
    (void)HAL_UART_Abort(huart);
    RS422_ResetPortState(port);
    RS422_SetReceiveMode(port);

    huart->Init.BaudRate = baud_rate;
    status = HAL_UART_Init(huart);
    if (status != HAL_OK)
    {
        huart->Init.BaudRate = old_baud_rate;
        (void)HAL_UART_Init(huart);
        RS422_UpdateBaudDiagnostics(port);
        (void)RS422_StartReceive(port);
        if (irq_was_enabled != 0U)
        {
            NVIC_ClearPendingIRQ(rs422_config[port].irqn);
            HAL_NVIC_EnableIRQ(rs422_config[port].irqn);
        }
        return status;
    }

    RS422_UpdateBaudDiagnostics(port);
    if (irq_was_enabled != 0U)
    {
        NVIC_ClearPendingIRQ(rs422_config[port].irqn);
        HAL_NVIC_EnableIRQ(rs422_config[port].irqn);
    }

    return RS422_StartReceive(port);
}

uint32_t RS422_GetRequestedBaudRate(RS422_PortId port)
{
    if (RS422_IsValidPort(port) == 0U)
    {
        return 0U;
    }

    return rs422_diag_requested_baud[port];
}

uint32_t RS422_GetActualBaudRate(RS422_PortId port)
{
    if (RS422_IsValidPort(port) == 0U)
    {
        return 0U;
    }

    RS422_UpdateBaudDiagnostics(port);
    return rs422_diag_actual_baud[port];
}

void RS422_ClearTx(RS422_PortId port)
{
    if (RS422_IsValidPort(port) == 0U)
    {
        return;
    }

    __disable_irq();
    rs422_state[port].tx_head = 0U;
    rs422_state[port].tx_tail = 0U;
    __enable_irq();
}

/**
  * @brief  查询指定 RS422 端口接收缓冲区中可读字节数。
  * @param  port: RS422 端口枚举值。
  * @retval 当前可读取的字节数，端口无效时返回 0。
  */
uint16_t RS422_Available(RS422_PortId port)
{
    uint16_t used;

    if (RS422_IsValidPort(port) == 0U)
    {
        return 0U;
    }

    __disable_irq();
    used = RS422_RingUsed(rs422_state[port].rx_head, rs422_state[port].rx_tail, RS422_RX_BUFFER_SIZE);
    __enable_irq();

    return used;
}

/**
  * @brief  从指定 RS422 端口接收缓冲区读取数据。
  * @param  port: RS422 端口枚举值。
  * @param  data: 接收数据输出缓冲区。
  * @param  max_size: 本次最多读取的字节数。
  * @retval 实际读取到的字节数。
  */
uint16_t RS422_Read(RS422_PortId port, uint8_t *data, uint16_t max_size)
{
    RS422_PortState *state;
    uint16_t count = 0U;

    if ((RS422_IsValidPort(port) == 0U) || (data == NULL) || (max_size == 0U))
    {
        return 0U;
    }

    state = &rs422_state[port];

    while (count < max_size)
    {
        __disable_irq();
        if (state->rx_head == state->rx_tail)// 读取缓冲区为空
        {
            __enable_irq();
            break;
        }

        data[count] = rs422_rx_buffer[port][state->rx_tail];
        state->rx_tail = RS422_RingNext(state->rx_tail, RS422_RX_BUFFER_SIZE);
        __enable_irq();
        count++;
    }

    return count;
}

/**
  * @brief  清空指定 RS422 端口的接收缓冲区。
  * @param  port: RS422 端口枚举值。
  * @retval None
  */
void RS422_ClearRx(RS422_PortId port)
{
    if (RS422_IsValidPort(port) == 0U)
    {
        return;
    }

    __disable_irq();
    rs422_state[port].rx_head = 0U;
    rs422_state[port].rx_tail = 0U;
    __enable_irq();
}

/**
  * @brief  查询指定 RS422 端口是否仍有数据正在发送或等待发送。
  * @param  port: RS422 端口枚举值。
  * @retval 1 表示发送忙，0 表示空闲。
  */
uint8_t RS422_IsTxBusy(RS422_PortId port)
{
    uint8_t busy;

    if (RS422_IsValidPort(port) == 0U)
    {
        return 0U;
    }

    __disable_irq();
    busy = (rs422_state[port].tx_busy != 0U) ||
           (rs422_state[port].tx_head != rs422_state[port].tx_tail);
    __enable_irq();

    return busy;
}

/**
  * @brief  RS422 UART 发送完成处理，继续发送队列中的下一个字节。
  * @param  huart: 触发回调的 HAL UART 句柄。
  * @retval None
  */
void RS422_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    rs422_diag_tx_complete_count[port]++;
    rs422_state[port].tx_busy = 0U;
    RS422_KickTx(port);
}

/**
  * @brief  RS422 UART 接收完成处理，缓存数据并重新挂起下一字节接收。
  * @param  huart: 触发回调的 HAL UART 句柄。
  * @retval None
  */
void RS422_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    rs422_diag_rx_complete_count[port]++;
    rs422_diag_last_rx_byte[port] = rs422_state[port].rx_byte;
    RS422_RxPush(port, rs422_state[port].rx_byte);
    (void)HAL_UART_Receive_IT(huart, &rs422_state[port].rx_byte, 1U);
}

/**
  * @brief  RS422 UART 错误处理，先中止当前接收等待后续恢复。
  * @param  huart: 触发错误回调的 HAL UART 句柄。
  * @retval None
  */
void RS422_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    rs422_diag_error_count[port]++;
    rs422_diag_last_error_code[port] = HAL_UART_GetError(huart);
    (void)HAL_UART_AbortReceive_IT(huart);
}

/**
  * @brief  RS422 UART 接收中止完成处理，重新启动单字节中断接收。
  * @param  huart: 触发回调的 HAL UART 句柄。
  * @retval None
  */
void RS422_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    rs422_diag_abort_count[port]++;
    rs422_diag_start_receive_status[port] =
        (uint32_t)HAL_UART_Receive_IT(huart, &rs422_state[port].rx_byte, 1U);
}

/**
  * @brief  HAL UART 发送完成总回调，转发给 RS422 驱动处理。
  * @param  huart: 触发回调的 HAL UART 句柄。
  * @retval None
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_TxCpltCallback(huart);
}

/**
  * @brief  HAL UART 接收完成总回调，转发给 RS422 驱动处理。
  * @param  huart: 触发回调的 HAL UART 句柄。
  * @retval None
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_RxCpltCallback(huart);
    HostComm_UART_RxCpltCallback(huart);
}

/**
  * @brief  HAL UART 错误总回调，转发给 RS422 驱动处理。
  * @param  huart: 触发错误回调的 HAL UART 句柄。
  * @retval None
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_ErrorCallback(huart);
    HostComm_UART_ErrorCallback(huart);
}

/**
  * @brief  HAL UART 接收中止完成总回调，转发给 RS422 驱动处理。
  * @param  huart: 触发回调的 HAL UART 句柄。
  * @retval None
  */
void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_AbortReceiveCpltCallback(huart);
    HostComm_UART_AbortReceiveCpltCallback(huart);
}
