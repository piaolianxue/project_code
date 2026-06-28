#include "rs422.h"
#include "usart.h"

typedef struct
{
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *tx_en_port;
    uint16_t tx_en_pin;
    GPIO_PinState tx_en_active;
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
} RS422_PortState;

static const RS422_PortConfig rs422_config[RS422_PORT_COUNT] =
{
    {
        &huart1,
        TX_EN_1_GPIO_Port, TX_EN_1_Pin, GPIO_PIN_SET
    },
    {
        &huart2,
        TX_EN_2_GPIO_Port, TX_EN_2_Pin, GPIO_PIN_SET
    }
};

static RS422_PortState rs422_state[RS422_PORT_COUNT];
static uint8_t rs422_rx_buffer[RS422_PORT_COUNT][RS422_RX_BUFFER_SIZE];
static uint8_t rs422_tx_buffer[RS422_PORT_COUNT][RS422_TX_BUFFER_SIZE];

static uint8_t RS422_IsValidPort(RS422_PortId port)
{
    return ((uint32_t)port < (uint32_t)RS422_PORT_COUNT) ? 1U : 0U;
}

static uint16_t RS422_RingNext(uint16_t index, uint16_t size)
{
    index++;
    if (index >= size)
    {
        index = 0U;
    }

    return index;
}

static uint16_t RS422_RingUsed(uint16_t head, uint16_t tail, uint16_t size)
{
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }

    return (uint16_t)(size - tail + head);
}

static uint16_t RS422_RingFree(uint16_t head, uint16_t tail, uint16_t size)
{
    return (uint16_t)(size - 1U - RS422_RingUsed(head, tail, size));
}

static void RS422_RxPush(RS422_PortId port, uint8_t data)
{
    RS422_PortState *state = &rs422_state[port];
    uint16_t next = RS422_RingNext(state->rx_head, RS422_RX_BUFFER_SIZE);

    if (next != state->rx_tail)
    {
        rs422_rx_buffer[port][state->rx_head] = data;
        state->rx_head = next;
    }
}

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

static void RS422_SetReceiveMode(RS422_PortId port)
{
    const RS422_PortConfig *config = &rs422_config[port];

    HAL_GPIO_WritePin(config->tx_en_port, config->tx_en_pin,
                      (config->tx_en_active == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void RS422_SetTransmitMode(RS422_PortId port)
{
    const RS422_PortConfig *config = &rs422_config[port];

    HAL_GPIO_WritePin(config->tx_en_port, config->tx_en_pin, config->tx_en_active);
}

static void RS422_KickTx(RS422_PortId port)
{
    RS422_PortState *state = &rs422_state[port];

    if (state->tx_busy != 0U)
    {
        return;
    }

    if (RS422_TxPop(port, &state->tx_byte) == 0U)
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

void RS422_Init(void)
{
    RS422_PortId port;

    for (port = (RS422_PortId)0; port < RS422_PORT_COUNT; port++)
    {
        rs422_state[port].rx_head = 0U;
        rs422_state[port].rx_tail = 0U;
        rs422_state[port].tx_head = 0U;
        rs422_state[port].tx_tail = 0U;
        rs422_state[port].tx_busy = 0U;
        RS422_SetReceiveMode(port);
    }

    (void)RS422_StartReceiveAll();
}

HAL_StatusTypeDef RS422_StartReceive(RS422_PortId port)
{
    RS422_PortState *state;

    if (RS422_IsValidPort(port) == 0U)
    {
        return HAL_ERROR;
    }

    RS422_SetReceiveMode(port);
    state = &rs422_state[port];

    return HAL_UART_Receive_IT(rs422_config[port].huart, &state->rx_byte, 1U);
}

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
        __enable_irq();
        return HAL_BUSY;
    }

    for (index = 0U; index < size; index++)
    {
        rs422_tx_buffer[port][state->tx_head] = data[index];
        state->tx_head = RS422_RingNext(state->tx_head, RS422_TX_BUFFER_SIZE);
    }
    __enable_irq();

    RS422_KickTx(port);
    return HAL_OK;
}

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
        if (state->rx_head == state->rx_tail)
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

void RS422_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    rs422_state[port].tx_busy = 0U;
    RS422_KickTx(port);
}

void RS422_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    RS422_RxPush(port, rs422_state[port].rx_byte);
    (void)HAL_UART_Receive_IT(huart, &rs422_state[port].rx_byte, 1U);
}

void RS422_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    (void)HAL_UART_AbortReceive_IT(huart);
}

void RS422_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_PortId port = RS422_FindPortByUart(huart);

    if (port == RS422_PORT_COUNT)
    {
        return;
    }

    (void)HAL_UART_Receive_IT(huart, &rs422_state[port].rx_byte, 1U);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_TxCpltCallback(huart);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_RxCpltCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_ErrorCallback(huart);
}

void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
    RS422_UART_AbortReceiveCpltCallback(huart);
}
