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
