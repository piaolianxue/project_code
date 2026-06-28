#include "ads7951.h"
#include "spi.h"

#define ADS7951_CHANNEL_MASK           0x0FU
#define ADS7951_RESULT_MASK            0x0FFFU

static uint16_t ADS7951_BuildManualCommand(uint8_t channel)
{
    return ADS7951_MODE_MANUAL |
           ADS7951_PROG_RANGE_GPIO |
           (((uint16_t)(channel & ADS7951_CHANNEL_MASK)) << 7) |
           ADS7951_RANGE_VREF |
           ADS7951_POWER_NORMAL |
           ADS7951_OUTPUT_CHANNEL;
}

static HAL_StatusTypeDef ADS7951_Transfer16(uint16_t tx_data, uint16_t *rx_data)
{
    uint16_t rx_dummy = 0U;

    if (rx_data == NULL)
    {
        rx_data = &rx_dummy;
    }

    return HAL_SPI_TransmitReceive(&hspi3, (uint8_t *)&tx_data, (uint8_t *)rx_data, 1U, ADS7951_SPI_TIMEOUT_MS);
}

void ADS7951_Init(void)
{
    HAL_GPIO_WritePin(ADS7951_U6_CS_GPIO_Port, ADS7951_U6_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ADS7951_U8_CS_GPIO_Port, ADS7951_U8_CS_Pin, GPIO_PIN_SET);
}

HAL_StatusTypeDef ADS7951_ReadRaw(GPIO_TypeDef *cs_port, uint16_t cs_pin, uint8_t channel, uint16_t *value)
{
    uint16_t tx_cmd;
    uint16_t rx_discard = 0U;
    uint16_t rx_data = 0U;
    HAL_StatusTypeDef status;

    if ((cs_port == NULL) || (value == NULL) || (channel >= ADS7951_CHANNEL_COUNT))
    {
        return HAL_ERROR;
    }

    tx_cmd = ADS7951_BuildManualCommand(channel);

    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_RESET);
    status = ADS7951_Transfer16(tx_cmd, &rx_discard);
    if (status == HAL_OK)
    {
        status = ADS7951_Transfer16(tx_cmd, &rx_data);
    }
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);

    if (status == HAL_OK)
    {
        *value = rx_data & ADS7951_RESULT_MASK;
    }

    return status;
}

HAL_StatusTypeDef ADS7951_U6_ReadChannel(uint8_t channel, uint16_t *value)
{
    return ADS7951_ReadRaw(ADS7951_U6_CS_GPIO_Port, ADS7951_U6_CS_Pin, channel, value);
}

HAL_StatusTypeDef ADS7951_U8_ReadChannel(uint8_t channel, uint16_t *value)
{
    return ADS7951_ReadRaw(ADS7951_U8_CS_GPIO_Port, ADS7951_U8_CS_Pin, channel, value);
}

static HAL_StatusTypeDef ADS7951_ReadAll(GPIO_TypeDef *cs_port, uint16_t cs_pin, uint16_t values[ADS7951_CHANNEL_COUNT])
{
    HAL_StatusTypeDef status;
    uint8_t channel;

    if (values == NULL)
    {
        return HAL_ERROR;
    }

    for (channel = 0U; channel < ADS7951_CHANNEL_COUNT; channel++)
    {
        status = ADS7951_ReadRaw(cs_port, cs_pin, channel, &values[channel]);
        if (status != HAL_OK)
        {
            return status;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef ADS7951_U6_ReadAll(uint16_t values[ADS7951_CHANNEL_COUNT])
{
    return ADS7951_ReadAll(ADS7951_U6_CS_GPIO_Port, ADS7951_U6_CS_Pin, values);
}

HAL_StatusTypeDef ADS7951_U8_ReadAll(uint16_t values[ADS7951_CHANNEL_COUNT])
{
    return ADS7951_ReadAll(ADS7951_U8_CS_GPIO_Port, ADS7951_U8_CS_Pin, values);
}

uint16_t ADS7951_Read_Channel(uint16_t cs_pin, uint8_t channel)
{
    uint16_t value = 0U;

    (void)ADS7951_ReadRaw(GPIOC, cs_pin, channel, &value);
    return value;
}

