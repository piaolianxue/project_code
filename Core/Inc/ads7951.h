#ifndef __ADS7951_H
#define __ADS7951_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define ADS7951_CHANNEL_COUNT          8U
#define ADS7951_MAX_VALUE              4095U
#define ADS7951_SPI_TIMEOUT_MS         100U

/* Mode-control word bits for ADS7951 manual mode. */
#define ADS7951_MODE_MANUAL            0x1000U
#define ADS7951_PROG_RANGE_GPIO        0x0800U
#define ADS7951_RANGE_VREF             0x0000U
#define ADS7951_RANGE_2VREF            0x0040U
#define ADS7951_POWER_NORMAL           0x0000U
#define ADS7951_POWER_DOWN             0x0020U
#define ADS7951_OUTPUT_CHANNEL         0x0000U
#define ADS7951_OUTPUT_GPIO            0x0010U

#define ADS7951_U6_CS_GPIO_Port        ADAC_CS_1_GPIO_Port
#define ADS7951_U6_CS_Pin              ADAC_CS_1_Pin
#define ADS7951_U8_CS_GPIO_Port        ADC_CS_2_GPIO_Port
#define ADS7951_U8_CS_Pin              ADC_CS_2_Pin

void ADS7951_Init(void);
HAL_StatusTypeDef ADS7951_ReadRaw(GPIO_TypeDef *cs_port, uint16_t cs_pin, uint8_t channel, uint16_t *value);
HAL_StatusTypeDef ADS7951_U6_ReadChannel(uint8_t channel, uint16_t *value);
HAL_StatusTypeDef ADS7951_U8_ReadChannel(uint8_t channel, uint16_t *value);
HAL_StatusTypeDef ADS7951_U6_ReadAll(uint16_t values[ADS7951_CHANNEL_COUNT]);
HAL_StatusTypeDef ADS7951_U8_ReadAll(uint16_t values[ADS7951_CHANNEL_COUNT]);

/* Compatibility wrapper for the previous PC8/PC9-only call style. */
uint16_t ADS7951_Read_Channel(uint16_t cs_pin, uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* __ADS7951_H */
