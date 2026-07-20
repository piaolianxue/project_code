# I2C 与 CANFD 主机接口实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标：** 为 H743 主机新增 I2C 与 CANFD 从机通讯接口，不改变现有屏幕协议和 RS422/RS485 模式选择。

**架构：** 新增 CubeMX 风格的 `i2c`、`fdcan` 外设文件，再新增 `slave_comm` 应用层封装。`main.c` 只负责初始化和轮询，`host_comm` 不参与本次接口打通。

**技术栈：** STM32H743、STM32 HAL、Keil MDK-ARM、PowerShell 静态检查脚本。

---

## 文件结构

- 新建 `Core/Inc/i2c.h`：声明 `hi2c1` 和 `MX_I2C1_Init()`。
- 新建 `Core/Src/i2c.c`：初始化 I2C1，PB8/PB9 配置为 AF4 开漏上拉。
- 新建 `Core/Inc/fdcan.h`：声明 `hfdcan1` 和 `MX_FDCAN1_Init()`。
- 新建 `Core/Src/fdcan.c`：初始化 FDCAN1，PD0/PD1 配置为 AF9，启动 RX FIFO0。
- 新建 `Core/Inc/slave_comm.h`：公开 I2C/CANFD 主机接口、协议常量和 Watch 变量。
- 新建 `Core/Src/slave_comm.c`：实现 I2C 寄存器读写、CANFD 命令发送、CANFD 响应轮询和统计。
- 修改 `Core/Inc/stm32h7xx_hal_conf.h`：启用 `HAL_FDCAN_MODULE_ENABLED`。
- 修改 `Core/Src/main.c`：包含 `i2c.h`、`fdcan.h`、`slave_comm.h`，初始化并轮询新模块。
- 修改 `MDK-ARM/shuziliangceshi.uvprojx`：加入新源码、头文件和 HAL FDCAN 驱动源。
- 新建 `Tests/slave_comm_static_check.ps1`：静态检查协议常量、文件存在、外设引脚和工程引用。

## Task 1: 新增静态检查

**Files:**
- Create: `Tests/slave_comm_static_check.ps1`

- [ ] **Step 1: 写入失败检查脚本**

写入下面内容：

```powershell
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Read-ProjectFile($relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path $path)) {
        throw "Missing required file: $relativePath"
    }
    return Get-Content -Raw $path
}

$i2cHeader = Read-ProjectFile 'Core/Inc/i2c.h'
$i2cSource = Read-ProjectFile 'Core/Src/i2c.c'
$fdcanHeader = Read-ProjectFile 'Core/Inc/fdcan.h'
$fdcanSource = Read-ProjectFile 'Core/Src/fdcan.c'
$slaveHeader = Read-ProjectFile 'Core/Inc/slave_comm.h'
$slaveSource = Read-ProjectFile 'Core/Src/slave_comm.c'
$halConf = Read-ProjectFile 'Core/Inc/stm32h7xx_hal_conf.h'
$mainSource = Read-ProjectFile 'Core/Src/main.c'
$project = Read-ProjectFile 'MDK-ARM/shuziliangceshi.uvprojx'

if ($i2cHeader -notmatch 'extern\s+I2C_HandleTypeDef\s+hi2c1;') {
    throw 'i2c.h must expose hi2c1.'
}
if ($i2cSource -notmatch 'hi2c1\.Instance\s*=\s*I2C1') {
    throw 'i2c.c must initialize I2C1.'
}
if ($i2cSource -notmatch 'GPIO_InitStruct\.Pin\s*=\s*GPIO_PIN_8\|GPIO_PIN_9') {
    throw 'I2C1 MSP must configure PB8/PB9.'
}
if ($i2cSource -notmatch 'GPIO_AF4_I2C1') {
    throw 'I2C1 pins must use AF4.'
}

if ($fdcanHeader -notmatch 'extern\s+FDCAN_HandleTypeDef\s+hfdcan1;') {
    throw 'fdcan.h must expose hfdcan1.'
}
if ($fdcanSource -notmatch 'hfdcan1\.Instance\s*=\s*FDCAN1') {
    throw 'fdcan.c must initialize FDCAN1.'
}
if ($fdcanSource -notmatch 'GPIO_InitStruct\.Pin\s*=\s*GPIO_PIN_0\|GPIO_PIN_1') {
    throw 'FDCAN1 MSP must configure PD0/PD1.'
}
if ($fdcanSource -notmatch 'GPIO_AF9_FDCAN1') {
    throw 'FDCAN1 pins must use AF9.'
}
if ($fdcanSource -notmatch 'HAL_FDCAN_Start\(&hfdcan1\)') {
    throw 'FDCAN1 init must start the peripheral.'
}

if ($halConf -notmatch '#define\s+HAL_FDCAN_MODULE_ENABLED') {
    throw 'HAL FDCAN module must be enabled.'
}
if ($project -notmatch 'stm32h7xx_hal_fdcan\.c') {
    throw 'Keil project must include stm32h7xx_hal_fdcan.c.'
}
if ($project -notmatch 'slave_comm\.c') {
    throw 'Keil project must include slave_comm.c.'
}
if ($project -notmatch 'i2c\.c') {
    throw 'Keil project must include i2c.c.'
}
if ($project -notmatch 'fdcan\.c') {
    throw 'Keil project must include fdcan.c.'
}

if ($slaveHeader -notmatch 'SLAVE_COMM_I2C_ADDRESS_7BIT\s+0x43U') {
    throw 'slave_comm.h must define I2C 7-bit address 0x43.'
}
if ($slaveHeader -notmatch 'SLAVE_COMM_CAN_COMMAND_ID\s+0x18EF4301UL') {
    throw 'slave_comm.h must define CAN command ID 0x18EF4301.'
}
if ($slaveHeader -notmatch 'SLAVE_COMM_CAN_RESPONSE_ID\s+0x18EF0143UL') {
    throw 'slave_comm.h must define CAN response ID 0x18EF0143.'
}
if ($slaveHeader -notmatch 'SLAVE_COMM_CAN_COMMAND_PING\s+=\s+0x01U') {
    throw 'slave_comm.h must define PING command 0x01.'
}
if ($slaveHeader -notmatch 'SLAVE_COMM_CAN_COMMAND_READ_REG\s+=\s+0x02U') {
    throw 'slave_comm.h must define READ_REG command 0x02.'
}
if ($slaveHeader -notmatch 'SLAVE_COMM_CAN_COMMAND_WRITE_REG\s+=\s+0x03U') {
    throw 'slave_comm.h must define WRITE_REG command 0x03.'
}
if ($slaveHeader -notmatch 'SLAVE_COMM_CAN_COMMAND_SET_CAN_MODE\s+=\s+0x04U') {
    throw 'slave_comm.h must define SET_CAN_MODE command 0x04.'
}
if ($slaveHeader -notmatch 'SLAVE_COMM_CAN_COMMAND_CLEAR_FLAGS\s+=\s+0x05U') {
    throw 'slave_comm.h must define CLEAR_FLAGS command 0x05.'
}

foreach ($api in @(
    'SlaveComm_Init',
    'SlaveComm_Poll',
    'SlaveComm_I2C_WriteRegs',
    'SlaveComm_I2C_ReadRegs',
    'SlaveComm_CanPing',
    'SlaveComm_CanReadReg',
    'SlaveComm_CanWriteReg',
    'SlaveComm_CanSetCanMode',
    'SlaveComm_CanClearFlags')) {
    if ($slaveHeader -notmatch $api) {
        throw "slave_comm.h must declare $api."
    }
    if ($slaveSource -notmatch $api) {
        throw "slave_comm.c must implement $api."
    }
}

if ($mainSource -notmatch '#include\s+"i2c\.h"') {
    throw 'main.c must include i2c.h.'
}
if ($mainSource -notmatch '#include\s+"fdcan\.h"') {
    throw 'main.c must include fdcan.h.'
}
if ($mainSource -notmatch '#include\s+"slave_comm\.h"') {
    throw 'main.c must include slave_comm.h.'
}
if ($mainSource -notmatch 'MX_I2C1_Init\(\)') {
    throw 'main.c must initialize I2C1.'
}
if ($mainSource -notmatch 'MX_FDCAN1_Init\(\)') {
    throw 'main.c must initialize FDCAN1.'
}
if ($mainSource -notmatch 'SlaveComm_Init\(\)') {
    throw 'main.c must initialize SlaveComm.'
}
if ($mainSource -notmatch 'SlaveComm_Poll\(\)') {
    throw 'main.c must poll SlaveComm.'
}

Write-Host 'slave_comm static check passed.'
```

- [ ] **Step 2: 运行脚本确认失败**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tests\slave_comm_static_check.ps1
```

预期：失败，提示缺少 `Core/Inc/i2c.h`。

- [ ] **Step 3: 提交检查脚本**

运行：

```powershell
git add Tests/slave_comm_static_check.ps1
git commit -m "test: add slave communication static check"
```

## Task 2: 新增 I2C1 外设

**Files:**
- Create: `Core/Inc/i2c.h`
- Create: `Core/Src/i2c.c`

- [ ] **Step 1: 创建 `Core/Inc/i2c.h`**

写入：

```c
#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern I2C_HandleTypeDef hi2c1;

void MX_I2C1_Init(void);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: 创建 `Core/Src/i2c.c`**

关键内容：

```c
#include "i2c.h"

I2C_HandleTypeDef hi2c1;

void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x307075B1;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) {
        Error_Handler();
    }
}
```

同时在同文件实现 `HAL_I2C_MspInit()` / `HAL_I2C_MspDeInit()`，为 PB8/PB9 配置 `GPIO_MODE_AF_OD`、`GPIO_PULLUP`、`GPIO_SPEED_FREQ_LOW`、`GPIO_AF4_I2C1`，并开关 `__HAL_RCC_I2C1_CLK_ENABLE()`。

- [ ] **Step 3: 运行静态检查**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tests\slave_comm_static_check.ps1
```

预期：失败，继续提示缺少 `Core/Inc/fdcan.h`。

## Task 3: 新增 FDCAN1 外设

**Files:**
- Create: `Core/Inc/fdcan.h`
- Create: `Core/Src/fdcan.c`
- Modify: `Core/Inc/stm32h7xx_hal_conf.h`

- [ ] **Step 1: 启用 HAL FDCAN**

把 `Core/Inc/stm32h7xx_hal_conf.h` 中：

```c
/* #define HAL_FDCAN_MODULE_ENABLED   */
```

改为：

```c
#define HAL_FDCAN_MODULE_ENABLED
```

- [ ] **Step 2: 创建 `Core/Inc/fdcan.h`**

写入：

```c
#ifndef __FDCAN_H__
#define __FDCAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern FDCAN_HandleTypeDef hfdcan1;

void MX_FDCAN1_Init(void);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 3: 创建 `Core/Src/fdcan.c`**

关键内容：

```c
#include "fdcan.h"

FDCAN_HandleTypeDef hfdcan1;

void MX_FDCAN1_Init(void)
{
    FDCAN_FilterTypeDef filter;

    hfdcan1.Instance = FDCAN1;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_FD_BRS;
    hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission = ENABLE;
    hfdcan1.Init.TransmitPause = DISABLE;
    hfdcan1.Init.ProtocolException = DISABLE;
    hfdcan1.Init.NominalPrescaler = 24;
    hfdcan1.Init.NominalSyncJumpWidth = 1;
    hfdcan1.Init.NominalTimeSeg1 = 16;
    hfdcan1.Init.NominalTimeSeg2 = 3;
    hfdcan1.Init.DataPrescaler = 6;
    hfdcan1.Init.DataSyncJumpWidth = 1;
    hfdcan1.Init.DataTimeSeg1 = 12;
    hfdcan1.Init.DataTimeSeg2 = 3;
    hfdcan1.Init.MessageRAMOffset = 0;
    hfdcan1.Init.StdFiltersNbr = 0;
    hfdcan1.Init.ExtFiltersNbr = 1;
    hfdcan1.Init.RxFifo0ElmtsNbr = 4;
    hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_64;
    hfdcan1.Init.RxFifo1ElmtsNbr = 0;
    hfdcan1.Init.RxBuffersNbr = 0;
    hfdcan1.Init.TxEventsNbr = 0;
    hfdcan1.Init.TxBuffersNbr = 0;
    hfdcan1.Init.TxFifoQueueElmtsNbr = 4;
    hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_64;
    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) {
        Error_Handler();
    }

    filter.IdType = FDCAN_EXTENDED_ID;
    filter.FilterIndex = 0;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = 0x18EF0143UL;
    filter.FilterID2 = 0x1FFFFFFFUL;
    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) {
        Error_Handler();
    }
}
```

同时在同文件实现 `HAL_FDCAN_MspInit()` / `HAL_FDCAN_MspDeInit()`，为 PD0/PD1 配置 `GPIO_MODE_AF_PP`、`GPIO_NOPULL`、`GPIO_SPEED_FREQ_VERY_HIGH`、`GPIO_AF9_FDCAN1`，并开关 `__HAL_RCC_FDCAN_CLK_ENABLE()`。

- [ ] **Step 4: 运行静态检查**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tests\slave_comm_static_check.ps1
```

预期：失败，继续提示缺少 `Core/Inc/slave_comm.h`。

## Task 4: 新增 `slave_comm` 协议接口

**Files:**
- Create: `Core/Inc/slave_comm.h`
- Create: `Core/Src/slave_comm.c`

- [ ] **Step 1: 创建 `Core/Inc/slave_comm.h`**

写入公开常量、命令枚举、统计结构、Watch 变量和 API：

```c
#ifndef __SLAVE_COMM_H
#define __SLAVE_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SLAVE_COMM_I2C_ADDRESS_7BIT        0x43U
#define SLAVE_COMM_I2C_ADDRESS             (SLAVE_COMM_I2C_ADDRESS_7BIT << 1U)
#define SLAVE_COMM_CAN_COMMAND_ID          0x18EF4301UL
#define SLAVE_COMM_CAN_RESPONSE_ID         0x18EF0143UL
#define SLAVE_COMM_CAN_MAX_DATA_LENGTH     64U
#define SLAVE_COMM_I2C_TIMEOUT_MS          100U

typedef enum
{
    SLAVE_COMM_CAN_COMMAND_PING = 0x01U,
    SLAVE_COMM_CAN_COMMAND_READ_REG = 0x02U,
    SLAVE_COMM_CAN_COMMAND_WRITE_REG = 0x03U,
    SLAVE_COMM_CAN_COMMAND_SET_CAN_MODE = 0x04U,
    SLAVE_COMM_CAN_COMMAND_CLEAR_FLAGS = 0x05U
} SlaveComm_CanCommand;

typedef struct
{
    uint32_t i2c_tx_count;
    uint32_t i2c_rx_count;
    uint32_t i2c_error_count;
    uint32_t can_tx_count;
    uint32_t can_rx_count;
    uint32_t can_error_count;
    uint32_t can_ignored_count;
    uint32_t last_can_id;
    uint32_t last_can_sequence;
    uint32_t last_can_command;
    uint32_t last_can_length;
    HAL_StatusTypeDef last_i2c_status;
    HAL_StatusTypeDef last_can_status;
} SlaveComm_Stats;

extern volatile uint32_t slave_comm_i2c_tx_count;
extern volatile uint32_t slave_comm_i2c_rx_count;
extern volatile uint32_t slave_comm_i2c_error_count;
extern volatile uint32_t slave_comm_can_tx_count;
extern volatile uint32_t slave_comm_can_rx_count;
extern volatile uint32_t slave_comm_can_error_count;
extern volatile uint32_t slave_comm_can_ignored_count;
extern volatile uint32_t slave_comm_can_sequence;
extern volatile uint32_t slave_comm_last_can_id;
extern volatile uint32_t slave_comm_last_can_sequence;
extern volatile uint32_t slave_comm_last_can_command;
extern volatile uint32_t slave_comm_last_can_length;
extern volatile HAL_StatusTypeDef slave_comm_last_i2c_status;
extern volatile HAL_StatusTypeDef slave_comm_last_can_status;

void SlaveComm_Init(void);
void SlaveComm_Poll(void);
void SlaveComm_GetStats(SlaveComm_Stats *stats);
HAL_StatusTypeDef SlaveComm_I2C_WriteRegs(uint8_t reg, const uint8_t *data, uint16_t length);
HAL_StatusTypeDef SlaveComm_I2C_ReadRegs(uint8_t reg, uint8_t *data, uint16_t length);
HAL_StatusTypeDef SlaveComm_CanPing(void);
HAL_StatusTypeDef SlaveComm_CanReadReg(uint8_t reg, uint8_t length);
HAL_StatusTypeDef SlaveComm_CanWriteReg(uint8_t reg, const uint8_t *data, uint8_t length);
HAL_StatusTypeDef SlaveComm_CanSetCanMode(uint8_t mode);
HAL_StatusTypeDef SlaveComm_CanClearFlags(uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: 创建 `Core/Src/slave_comm.c`**

实现要点：

```c
#include "slave_comm.h"
#include "i2c.h"
#include "fdcan.h"

#include <string.h>

static uint8_t slave_comm_can_rx_data[SLAVE_COMM_CAN_MAX_DATA_LENGTH];

volatile uint32_t slave_comm_i2c_tx_count = 0U;
volatile uint32_t slave_comm_i2c_rx_count = 0U;
volatile uint32_t slave_comm_i2c_error_count = 0U;
volatile uint32_t slave_comm_can_tx_count = 0U;
volatile uint32_t slave_comm_can_rx_count = 0U;
volatile uint32_t slave_comm_can_error_count = 0U;
volatile uint32_t slave_comm_can_ignored_count = 0U;
volatile uint32_t slave_comm_can_sequence = 0U;
volatile uint32_t slave_comm_last_can_id = 0U;
volatile uint32_t slave_comm_last_can_sequence = 0U;
volatile uint32_t slave_comm_last_can_command = 0U;
volatile uint32_t slave_comm_last_can_length = 0U;
volatile HAL_StatusTypeDef slave_comm_last_i2c_status = HAL_OK;
volatile HAL_StatusTypeDef slave_comm_last_can_status = HAL_OK;
```

I2C 写接口用局部 TX 缓冲区拼接寄存器地址和数据，调用 `HAL_I2C_Master_Transmit()`。

I2C 读接口先用 `HAL_I2C_Master_Transmit()` 发送 1 字节寄存器地址，再用 `HAL_I2C_Master_Receive()` 读取数据。

CANFD 发送共用内部函数 `SlaveComm_CanSendCommand()`，Byte0 自动填递增 sequence，Byte1 填命令，Byte2 后复制参数。

`SlaveComm_Poll()` 用 `HAL_FDCAN_GetRxFifoFillLevel()` 检查 FIFO0，有数据则 `HAL_FDCAN_GetRxMessage()`；只接受扩展 ID `0x18EF0143`，其余计入 ignored。

- [ ] **Step 3: 运行静态检查**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tests\slave_comm_static_check.ps1
```

预期：失败，继续提示 `main.c` 或 Keil 工程尚未接入。

## Task 5: 接入 `main.c` 和 Keil 工程

**Files:**
- Modify: `Core/Src/main.c`
- Modify: `MDK-ARM/shuziliangceshi.uvprojx`

- [ ] **Step 1: 修改 `main.c` include**

在现有 include 区加入：

```c
#include "i2c.h"
#include "fdcan.h"
#include "slave_comm.h"
```

- [ ] **Step 2: 修改 `main.c` 初始化顺序**

在 UART 初始化后、业务初始化前加入：

```c
MX_I2C1_Init();
MX_FDCAN1_Init();
```

在 `HostComm_Init()` 附近加入：

```c
SlaveComm_Init();
```

- [ ] **Step 3: 修改 `main.c` 主循环**

在 `HostComm_Poll();` 附近加入：

```c
SlaveComm_Poll();
```

- [ ] **Step 4: 修改 Keil 工程**

在 `Application/User/Core` 分组加入：

```xml
<File>
  <FileName>i2c.h</FileName>
  <FileType>5</FileType>
  <FilePath>..\Core\Inc\i2c.h</FilePath>
</File>
<File>
  <FileName>fdcan.h</FileName>
  <FileType>5</FileType>
  <FilePath>..\Core\Inc\fdcan.h</FilePath>
</File>
<File>
  <FileName>slave_comm.h</FileName>
  <FileType>5</FileType>
  <FilePath>..\Core\Inc\slave_comm.h</FilePath>
</File>
<File>
  <FileName>i2c.c</FileName>
  <FileType>1</FileType>
  <FilePath>..\Core\Src\i2c.c</FilePath>
</File>
<File>
  <FileName>fdcan.c</FileName>
  <FileType>1</FileType>
  <FilePath>..\Core\Src\fdcan.c</FilePath>
</File>
<File>
  <FileName>slave_comm.c</FileName>
  <FileType>1</FileType>
  <FilePath>..\Core\Src\slave_comm.c</FilePath>
</File>
```

在 `Drivers/STM32H7xx_HAL_Driver` 分组加入：

```xml
<File>
  <FileName>stm32h7xx_hal_fdcan.c</FileName>
  <FileType>1</FileType>
  <FilePath>../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_fdcan.c</FilePath>
</File>
```

- [ ] **Step 5: 运行静态检查**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tests\slave_comm_static_check.ps1
```

预期：通过。

## Task 6: 构建验证和提交

**Files:**
- All touched files from Tasks 1-5

- [ ] **Step 1: 运行相关静态检查**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tests\slave_comm_static_check.ps1
powershell -ExecutionPolicy Bypass -File Tests\rs485_default_baudrate_check.ps1
powershell -ExecutionPolicy Bypass -File Tests\host_comm_protocol_check.ps1
```

预期：三个脚本通过。

- [ ] **Step 2: 运行 Keil 构建**

运行：

```powershell
& 'D:\Keil5\UV4\UV4.exe' -b 'D:\YC\Code\ShuZiLiangCeShi\project_code\MDK-ARM\shuziliangceshi.uvprojx' -t 'shuziliangceshi'
```

预期：构建返回码为 0，日志显示 `0 Error(s)`。已有 HAL 未使用参数 warning 可以保留。

- [ ] **Step 3: 检查本次相关 diff**

运行：

```powershell
git diff -- Core/Inc/i2c.h Core/Src/i2c.c Core/Inc/fdcan.h Core/Src/fdcan.c Core/Inc/slave_comm.h Core/Src/slave_comm.c Core/Inc/stm32h7xx_hal_conf.h Core/Src/main.c MDK-ARM/shuziliangceshi.uvprojx Tests/slave_comm_static_check.ps1
```

确认只包含 I2C/CANFD 主机接口相关改动。

- [ ] **Step 4: 提交实现**

运行：

```powershell
git add Core/Inc/i2c.h Core/Src/i2c.c Core/Inc/fdcan.h Core/Src/fdcan.c Core/Inc/slave_comm.h Core/Src/slave_comm.c Core/Inc/stm32h7xx_hal_conf.h Core/Src/main.c MDK-ARM/shuziliangceshi.uvprojx Tests/slave_comm_static_check.ps1
git commit -m "feat: add I2C and CANFD slave interfaces"
```

不提交已有构建产物、`host_comm.c` 的既有改动或未跟踪的移植说明文档。
