$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root 'Core/Inc/host_comm.h')
$source = Get-Content -Raw (Join-Path $root 'Core/Src/host_comm.c')

if ($header -match '0x81U|HOST_COMM_CMD_UPLOAD_STATS') {
    throw 'UART7 screen protocol V1.3-0709 does not define a one-byte 0x81 upload command.'
}

if ($header -notmatch 'HOST_COMM_DOWNLOAD_INSTRUCTION\s+0xB111U') {
    throw 'UART7 screen download instruction must be B1 11 per V1.3-0709.'
}

if ($header -notmatch 'HOST_COMM_UPLOAD_INSTRUCTION\s+0xB110U') {
    throw 'UART7 screen upload instruction must be B1 10 per V1.3-0709.'
}

if ($source -notmatch 'HostComm_WriteU16BE\(&frame\[index\], HOST_COMM_UPLOAD_INSTRUCTION\)') {
    throw 'Upload frames must encode EE B1 10 ... FF FC FF FF.'
}

$expectedControlIds = @{
    HOST_COMM_CONTROL_BAUD_UART1   = '0x00010010UL'
    HOST_COMM_CONTROL_BAUD_UART2   = '0x00010005UL'
    HOST_COMM_CONTROL_BAUD_UART3   = '0x00010015UL'
    HOST_COMM_CONTROL_BAUD_UART4   = '0x0001001BUL'
    HOST_COMM_CONTROL_ENABLE_UART1 = '0x00010011UL'
    HOST_COMM_CONTROL_ENABLE_UART2 = '0x00010006UL'
    HOST_COMM_CONTROL_ENABLE_UART3 = '0x00010016UL'
    HOST_COMM_CONTROL_ENABLE_UART4 = '0x0001001CUL'
    HOST_COMM_CONTROL_START_TEST   = '0x0001001FUL'
    HOST_COMM_CONTROL_STOP_TEST    = '0x0001002DUL'
    HOST_COMM_UPLOAD_TX_UART1      = '0x0001000BUL'
    HOST_COMM_UPLOAD_TX_UART2      = '0x00010002UL'
    HOST_COMM_UPLOAD_TX_UART3      = '0x00010013UL'
    HOST_COMM_UPLOAD_TX_UART4      = '0x00010019UL'
    HOST_COMM_UPLOAD_RX_UART1      = '0x0001000CUL'
    HOST_COMM_UPLOAD_RX_UART2      = '0x0001000EUL'
    HOST_COMM_UPLOAD_RX_UART3      = '0x00010017UL'
    HOST_COMM_UPLOAD_RX_UART4      = '0x0001001DUL'
    HOST_COMM_UPLOAD_ERR_UART1     = '0x0001000DUL'
    HOST_COMM_UPLOAD_ERR_UART2     = '0x00010012UL'
    HOST_COMM_UPLOAD_ERR_UART3     = '0x00010018UL'
    HOST_COMM_UPLOAD_ERR_UART4     = '0x0001001EUL'
}

foreach ($entry in $expectedControlIds.GetEnumerator()) {
    $pattern = [regex]::Escape($entry.Key) + '\s+' + [regex]::Escape($entry.Value)
    if ($source -notmatch $pattern) {
        throw "$($entry.Key) must be $($entry.Value) per current upper-computer screen configuration."
    }
}

foreach ($name in @('host_comm_uart1_enabled', 'host_comm_uart2_enabled', 'host_comm_uart3_enabled', 'host_comm_uart4_enabled')) {
    if ($source -notmatch "volatile\s+uint8_t\s+$name\s*=\s*0U;") {
        throw "$name must default to disabled."
    }

    if ($source -notmatch "$name\s*=\s*0U;") {
        throw "$name must be reset to disabled in HostComm_Init."
    }
}

Write-Host 'host_comm UART7 protocol layout check passed.'
