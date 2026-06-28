#ifndef __SSI422_H
#define __SSI422_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SSI422_DEFAULT_TIMEOUT_MS 100U
#define SSI422_MAX_READ_WORDS     4U

typedef enum
{
    SSI422_PORT_U16 = 0,
    SSI422_PORT_U18,
    SSI422_PORT_COUNT
} SSI422_PortId;

void SSI422_Init(void);
void SSI422_SetEncoderMode(SSI422_PortId port);
void SSI422_SetListenMode(SSI422_PortId port);

HAL_StatusTypeDef SSI422_ReadWords(SSI422_PortId port, uint16_t *data, uint16_t word_count, uint32_t timeout);
HAL_StatusTypeDef SSI422_ReadBits(SSI422_PortId port, uint8_t bit_count, uint32_t *value, uint32_t timeout);

HAL_StatusTypeDef SSI422_U16_ReadWords(uint16_t *data, uint16_t word_count);
HAL_StatusTypeDef SSI422_U18_ReadWords(uint16_t *data, uint16_t word_count);
HAL_StatusTypeDef SSI422_U16_ReadBits(uint8_t bit_count, uint32_t *value);
HAL_StatusTypeDef SSI422_U18_ReadBits(uint8_t bit_count, uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif /* __SSI422_H */
