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

static uint8_t SSI422_IsValidPort(SSI422_PortId port)
{
    return ((uint32_t)port < (uint32_t)SSI422_PORT_COUNT) ? 1U : 0U;
}

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

void SSI422_Init(void)
{
    SSI422_SetEncoderMode(SSI422_PORT_U16);
    SSI422_SetEncoderMode(SSI422_PORT_U18);
}

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

HAL_StatusTypeDef SSI422_U16_ReadWords(uint16_t *data, uint16_t word_count)
{
    return SSI422_ReadWords(SSI422_PORT_U16, data, word_count, SSI422_DEFAULT_TIMEOUT_MS);
}

HAL_StatusTypeDef SSI422_U18_ReadWords(uint16_t *data, uint16_t word_count)
{
    return SSI422_ReadWords(SSI422_PORT_U18, data, word_count, SSI422_DEFAULT_TIMEOUT_MS);
}

HAL_StatusTypeDef SSI422_U16_ReadBits(uint8_t bit_count, uint32_t *value)
{
    return SSI422_ReadBits(SSI422_PORT_U16, bit_count, value, SSI422_DEFAULT_TIMEOUT_MS);
}

HAL_StatusTypeDef SSI422_U18_ReadBits(uint8_t bit_count, uint32_t *value)
{
    return SSI422_ReadBits(SSI422_PORT_U18, bit_count, value, SSI422_DEFAULT_TIMEOUT_MS);
}
