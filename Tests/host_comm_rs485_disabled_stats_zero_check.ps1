$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -Raw (Join-Path $root 'Core/Src/host_comm.c')
$rs485 = Get-Content -Raw (Join-Path $root 'Core/Src/rs485_speed_test.c')

if ($source -notmatch 'HostComm_CollectPortStats') {
    throw 'HostComm_CollectPortStats must exist.'
}

if ($source -notmatch 'host_comm_uart1_enabled') {
    throw 'HostComm stats must consult UART1 enable state.'
}

if ($source -notmatch 'host_comm_uart3_enabled') {
    throw 'HostComm stats must consult UART3 enable state.'
}

if ($rs485 -notmatch 'rs485_speed_test_slave_stats\[slave_index\]\.enabled\s*==\s*0U') {
    throw 'RS485 stats must return zero counters for a disabled logical slave.'
}

if ($rs485 -notmatch 'stats->tx_bytes\s*=\s*rs485_speed_test_slave_stats\[slave_index\]\.tx_frames\s*\*\s*RS422_PROTOCOL_FRAME_LENGTH\s*;') {
    throw 'RS485 TX bytes must be calculated from the selected slave TX frames.'
}

if ($rs485 -notmatch 'stats->rx_bytes\s*=\s*rs485_speed_test_slave_stats\[slave_index\]\.rx_frames\s*\*\s*RS422_PROTOCOL_FRAME_LENGTH\s*;') {
    throw 'RS485 RX bytes must be calculated from the selected slave RX frames.'
}

if ($rs485 -match 'stats->tx_bytes\s*=\s*rs422_diag_tx_total_bytes\[RS485_SPEED_TEST_PORT\]\s*;') {
    throw 'RS485 logical stats must not expose shared physical TX totals.'
}

if ($rs485 -match 'stats->rx_bytes\s*=\s*rs485_speed_test_protocol_stats\.total_bytes\s*;') {
    throw 'RS485 logical stats must not expose shared physical RX totals.'
}

Write-Host 'host_comm disabled RS485 stats zeroing check passed.'
