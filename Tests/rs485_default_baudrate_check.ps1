$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root 'Core/Inc/rs485_speed_test.h')
$source = Get-Content -Raw (Join-Path $root 'Core/Src/rs485_speed_test.c')
$hostSource = Get-Content -Raw (Join-Path $root 'Core/Src/host_comm.c')

if ($header -notmatch 'RS485_SPEED_TEST_DEFAULT_BAUDRATE\s+2000000U') {
    throw 'RS485 slave communication default baudrate must be 2000000.'
}

if ($source -notmatch 'rs485_speed_test_baud_table\[[^\]]+\]\s*=\s*\{[\s\r\n]*RS485_SPEED_TEST_DEFAULT_BAUDRATE[\s\r\n]*\}') {
    throw 'RS485 baud table must use the default baudrate macro.'
}

if ($source -notmatch 'RS422_SetBaudRate\(RS485_SPEED_TEST_PORT,\s*RS485_SPEED_TEST_DEFAULT_BAUDRATE\)') {
    throw 'RS485 init must apply the default baudrate.'
}

if ($source -notmatch 'baudrate\s*=\s*RS485_SPEED_TEST_DEFAULT_BAUDRATE') {
    throw 'RS485 zero-baudrate fallback must use the default baudrate.'
}

if ($hostSource -notmatch 'host_comm_uart1_baudrate\s*=\s*RS485_SPEED_TEST_DEFAULT_BAUDRATE') {
    throw 'HostComm UART1 default must use the RS485 default baudrate.'
}

if ($hostSource -notmatch 'host_comm_uart3_baudrate\s*=\s*RS485_SPEED_TEST_DEFAULT_BAUDRATE') {
    throw 'HostComm UART3 default must use the RS485 default baudrate.'
}

Write-Host 'rs485 default baudrate check passed.'
