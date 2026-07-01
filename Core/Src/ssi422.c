#include "ssi422.h"
#include "spi.h"

typedef struct
{
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *de_port;
    uint16_t de_pin;
    GPIO_TypeDef *re_port;
    uint16_t re_pin;
} SSI422_PortConfig;

static const SSI422_PortConfig ssi422_config[SSI422_PORT_COUNT] =
{
    {
        &hspi4,
        CTRL_1_DE_GPIO_Port, CTRL_1_DE_Pin,
        CTRL_1_RE_GPIO_Port, CTRL_1_RE_Pin
    },
    {
        &hspi2,
        CTRL_2_DE_GPIO_Port, CTRL_2_DE_Pin,
        CTRL_2_RE_GPIO_Port, CTRL_2_RE_Pin
    }
};

/**
  * @brief  判断 SSI422 端口编号是否在有效范围内。
  * @param  port: SSI422 端口枚举值。
  * @retval 1 表示有效，0 表示无效。
  */
static uint8_t SSI422_IsValidPort(SSI422_PortId port)
{
    return ((uint32_t)port < (uint32_t)SSI422_PORT_COUNT) ? 1U : 0U;
}

/**
  * @brief  将指定 SSI422 端口切到编码器读数模式。
  * @param  port: SSI422 端口枚举值。
  * @retval None
  */
void SSI422_SetEncoderMode(SSI422_PortId port)
{
    const SSI422_PortConfig *config;

    if (SSI422_IsValidPort(port) == 0U)
    {
        return;
    }

    config = &ssi422_config[port];
    HAL_GPIO_WritePin(config->de_port, config->de_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(config->re_port, config->re_pin, GPIO_PIN_RESET);
}

/**
  * @brief  将指定 SSI422 端口切到监听模式。
  * @param  port: SSI422 端口枚举值。
  * @retval None
  */
void SSI422_SetListenMode(SSI422_PortId port)
{
    const SSI422_PortConfig *config;

    if (SSI422_IsValidPort(port) == 0U)
    {
        return;
    }

    config = &ssi422_config[port];
    HAL_GPIO_WritePin(config->de_port, config->de_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(config->re_port, config->re_pin, GPIO_PIN_RESET);
}

/**
  * @brief  初始化所有 SSI422 端口的收发控制脚状态。
  * @retval None
  */
void SSI422_Init(void)
{
    SSI422_SetEncoderMode(SSI422_PORT_U16);
    SSI422_SetEncoderMode(SSI422_PORT_U18);
}

/**
  * @brief  从指定 SSI422 端口读取若干个 16 位字。
  * @param  port: SSI422 端口枚举值。
  * @param  data: 接收数据缓冲区。
  * @param  word_count: 读取的 16 位字数量，不能超过 SSI422_MAX_READ_WORDS。
  * @param  timeout: SPI 阻塞传输超时时间，单位 ms。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef SSI422_ReadWords(SSI422_PortId port, uint16_t *data, uint16_t word_count, uint32_t timeout)
{
    uint16_t dummy[SSI422_MAX_READ_WORDS];
    uint16_t index;

    if ((SSI422_IsValidPort(port) == 0U) ||
        (data == NULL) ||
        (word_count == 0U) ||
        (word_count > SSI422_MAX_READ_WORDS))
    {
        return HAL_ERROR;
    }

    for (index = 0U; index < word_count; index++)
    {
        dummy[index] = 0xFFFFU;
        data[index] = 0U;
    }

    SSI422_SetEncoderMode(port);

    return HAL_SPI_TransmitReceive(ssi422_config[port].hspi,
                                   (uint8_t *)dummy,
                                   (uint8_t *)data,
                                   word_count,
                                   timeout);
}

/**
  * @brief  从指定 SSI422 端口按位数读取编码器原始值。
  * @param  port: SSI422 端口枚举值。
  * @param  bit_count: 需要读取的有效位数，范围 1 到 32。
  * @param  value: 返回右对齐后的读数。
  * @param  timeout: SPI 阻塞传输超时时间，单位 ms。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef SSI422_ReadBits(SSI422_PortId port, uint8_t bit_count, uint32_t *value, uint32_t timeout)
{
    uint16_t rx_words[SSI422_MAX_READ_WORDS];
    uint16_t word_count;
    uint32_t raw = 0U;
    uint16_t index;
    HAL_StatusTypeDef status;

    if ((value == NULL) || (bit_count == 0U) || (bit_count > 32U))
    {
        return HAL_ERROR;
    }

    word_count = (uint16_t)((bit_count + 15U) / 16U);
    status = SSI422_ReadWords(port, rx_words, word_count, timeout);
    if (status != HAL_OK)
    {
        return status;
    }

    for (index = 0U; index < word_count; index++)
    {
        raw = (raw << 16) | rx_words[index];
    }

    if (bit_count < 32U)
    {
        raw >>= (uint8_t)((word_count * 16U) - bit_count);
        raw &= ((1UL << bit_count) - 1UL);
    }

    *value = raw;
    return HAL_OK;
}

/**
  * @brief  使用默认超时时间读取 U16 端口的若干个 16 位字。
  * @param  data: 接收数据缓冲区。
  * @param  word_count: 读取的 16 位字数量。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef SSI422_U16_ReadWords(uint16_t *data, uint16_t word_count)
{
    return SSI422_ReadWords(SSI422_PORT_U16, data, word_count, SSI422_DEFAULT_TIMEOUT_MS);
}

/**
  * @brief  使用默认超时时间读取 U18 端口的若干个 16 位字。
  * @param  data: 接收数据缓冲区。
  * @param  word_count: 读取的 16 位字数量。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef SSI422_U18_ReadWords(uint16_t *data, uint16_t word_count)
{
    return SSI422_ReadWords(SSI422_PORT_U18, data, word_count, SSI422_DEFAULT_TIMEOUT_MS);
}

/**
  * @brief  使用默认超时时间读取 U16 端口的指定位数编码器值。
  * @param  bit_count: 需要读取的有效位数。
  * @param  value: 返回右对齐后的读数。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef SSI422_U16_ReadBits(uint8_t bit_count, uint32_t *value)
{
    return SSI422_ReadBits(SSI422_PORT_U16, bit_count, value, SSI422_DEFAULT_TIMEOUT_MS);
}

/**
  * @brief  使用默认超时时间读取 U18 端口的指定位数编码器值。
  * @param  bit_count: 需要读取的有效位数。
  * @param  value: 返回右对齐后的读数。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef SSI422_U18_ReadBits(uint8_t bit_count, uint32_t *value)
{
    return SSI422_ReadBits(SSI422_PORT_U18, bit_count, value, SSI422_DEFAULT_TIMEOUT_MS);
}
