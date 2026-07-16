$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root 'Core/Inc/rs485_speed_test.h')
$source = Get-Content -Raw (Join-Path $root 'Core/Src/rs485_speed_test.c')
$hostSource = Get-Content -Raw (Join-Path $root 'Core/Src/host_comm.c')

if ($header -notmatch 'RS485_SPEED_TEST_ROLE_MASTER') {
    throw 'rs485_speed_test.h must expose master/slave role definitions.'
}

if ($header -notmatch 'RS485_SPEED_TEST_DEFAULT_ROLE\s+RS485_SPEED_TEST_ROLE_MASTER') {
    throw 'Master firmware must default RS485 speed test role to MASTER.'
}

if ($header -notmatch 'RS485_SPEED_TEST_MASTER_SLAVE_COUNT\s+2U') {
    throw 'Master firmware must declare two addressable RS485 slaves.'
}

if ($source -notmatch 'RS485_SPEED_TEST_PORT\s+RS422_PORT_U9') {
    throw 'Master RS485 polling must use the physical U9 bus by default.'
}

if ($source -notmatch 'rs485_speed_test_master_slave_table\[[^\]]+\]\s*=\s*\{[\s\r\n]*1U,\s*[\r\n\s]*2U[\s\r\n]*\}') {
    throw 'Master RS485 polling must use slave address table {1U, 2U}.'
}

if ($source -notmatch 'packet->id\s*!=\s*\(uint8_t\)rs485_speed_test_current_slave_id') {
    throw 'Master packet handler must ignore frames that do not match the current slave id.'
}

if ($source -notmatch 'RS485_SpeedTestAdvanceMasterSlave\(\)') {
    throw 'Master must advance to the next slave after echo or timeout.'
}

if ($source -notmatch 'void\s+RS485_SpeedTest_Start\(RS485_SpeedTestInstanceId instance,\s*uint32_t baudrate\)') {
    throw 'Compatibility Start API must remain available for HostComm.'
}

if ($source -notmatch 'instance\s*==\s*RS485_SPEED_TEST_INSTANCE_UART3[\s\S]*return\s+1U;') {
    throw 'Compatibility mapping must map UART3 to slave index 1.'
}

if ($hostSource -notmatch 'RS485_SpeedTest_Start\(RS485_SPEED_TEST_INSTANCE_UART1') {
    throw 'HostComm must start UART1 control as slave-1 polling.'
}

if ($hostSource -notmatch 'RS485_SpeedTest_Start\(RS485_SPEED_TEST_INSTANCE_UART3') {
    throw 'HostComm must start UART3 control as slave-2 polling.'
}

if ($hostSource -notmatch 'RS485_SpeedTest_SetRole\(RS485_SPEED_TEST_ROLE_MASTER\)') {
    throw 'HostComm_StartTest must force RS485 speed test role to MASTER.'
}

if ($hostSource -notmatch 'RS485_SpeedTest_StopAll\(\)') {
    throw 'HostComm_StopTest must stop the RS485 polling state machine.'
}

Write-Host 'rs485 master address polling layout check passed.'
