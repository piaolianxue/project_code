#include "ads7951.h"
#include "spi.h"

#define ADS7951_CHANNEL_MASK           0x0FU
#define ADS7951_RESULT_MASK            0x0FFFU

/**
  * @brief  生成 ADS7951 手动模式下的 16 位控制命令。
  * @param  channel: 要采样的通道号，函数内部只取低 4 位。
  * @retval 可直接通过 SPI 发送给 ADS7951 的控制字。
  */
static uint16_t ADS7951_BuildManualCommand(uint8_t channel)
{
    return ADS7951_MODE_MANUAL |
           ADS7951_PROG_RANGE_GPIO |
           (((uint16_t)(channel & ADS7951_CHANNEL_MASK)) << 7) |
           ADS7951_RANGE_VREF |
           ADS7951_POWER_NORMAL |
           ADS7951_OUTPUT_CHANNEL;
}

/**
  * @brief  通过 SPI3 与 ADS7951 交换一个 16 位数据帧。
  * @param  tx_data: 本次发送的控制字或占位数据。
  * @param  rx_data: 接收数据存放地址，传 NULL 时丢弃接收值。
  * @retval HAL 状态，HAL_OK 表示传输成功。
  */
static HAL_StatusTypeDef ADS7951_Transfer16(uint16_t tx_data, uint16_t *rx_data)
{
    uint16_t rx_dummy = 0U;

    if (rx_data == NULL)
    {
        rx_data = &rx_dummy;
    }

    return HAL_SPI_TransmitReceive(&hspi3, (uint8_t *)&tx_data, (uint8_t *)rx_data, 1U, ADS7951_SPI_TIMEOUT_MS);
}

/**
  * @brief  初始化两片 ADS7951 的片选脚为无效电平。
  * @retval None
  */
void ADS7951_Init(void)
{
    HAL_GPIO_WritePin(ADS7951_U6_CS_GPIO_Port, ADS7951_U6_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ADS7951_U8_CS_GPIO_Port, ADS7951_U8_CS_Pin, GPIO_PIN_SET);
}

/**
  * @brief  读取指定 ADS7951 芯片的单个原始 ADC 通道值。
  * @param  cs_port: 目标芯片片选 GPIO 端口。
  * @param  cs_pin: 目标芯片片选 GPIO 引脚。
  * @param  channel: ADC 通道号，范围 0 到 ADS7951_CHANNEL_COUNT - 1。
  * @param  value: 返回 12 位转换结果的地址。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
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

/**
  * @brief  读取 U6 这片 ADS7951 的指定通道。
  * @param  channel: ADC 通道号。
  * @param  value: 返回 12 位转换结果的地址。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef ADS7951_U6_ReadChannel(uint8_t channel, uint16_t *value)
{
    return ADS7951_ReadRaw(ADS7951_U6_CS_GPIO_Port, ADS7951_U6_CS_Pin, channel, value);
}

/**
  * @brief  读取 U8 这片 ADS7951 的指定通道。
  * @param  channel: ADC 通道号。
  * @param  value: 返回 12 位转换结果的地址。
  * @retval HAL 状态，HAL_OK 表示读取成功。
  */
HAL_StatusTypeDef ADS7951_U8_ReadChannel(uint8_t channel, uint16_t *value)
{
    return ADS7951_ReadRaw(ADS7951_U8_CS_GPIO_Port, ADS7951_U8_CS_Pin, channel, value);
}

/**
  * @brief  顺序读取指定 ADS7951 芯片的全部通道。
  * @param  cs_port: 目标芯片片选 GPIO 端口。
  * @param  cs_pin: 目标芯片片选 GPIO 引脚。
  * @param  values: 存放全部通道 12 位转换结果的数组。
  * @retval HAL 状态，任一通道失败时立即返回该错误状态。
  */
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

/**
  * @brief  读取 U6 这片 ADS7951 的全部 ADC 通道。
  * @param  values: 存放全部通道 12 位转换结果的数组。
  * @retval HAL 状态，HAL_OK 表示全部读取成功。
  */
HAL_StatusTypeDef ADS7951_U6_ReadAll(uint16_t values[ADS7951_CHANNEL_COUNT])
{
    return ADS7951_ReadAll(ADS7951_U6_CS_GPIO_Port, ADS7951_U6_CS_Pin, values);
}

/**
  * @brief  读取 U8 这片 ADS7951 的全部 ADC 通道。
  * @param  values: 存放全部通道 12 位转换结果的数组。
  * @retval HAL 状态，HAL_OK 表示全部读取成功。
  */
HAL_StatusTypeDef ADS7951_U8_ReadAll(uint16_t values[ADS7951_CHANNEL_COUNT])
{
    return ADS7951_ReadAll(ADS7951_U8_CS_GPIO_Port, ADS7951_U8_CS_Pin, values);
}

/**
  * @brief  兼容旧接口的单通道读取函数，片选端口固定为 GPIOC。
  * @param  cs_pin: GPIOC 上连接 ADS7951 片选的引脚。
  * @param  channel: ADC 通道号。
  * @retval 12 位转换结果；读取失败时返回当前默认值 0。
  */
uint16_t ADS7951_Read_Channel(uint16_t cs_pin, uint8_t channel)
{
    uint16_t value = 0U;

    (void)ADS7951_ReadRaw(GPIOC, cs_pin, channel, &value);
    return value;
}

