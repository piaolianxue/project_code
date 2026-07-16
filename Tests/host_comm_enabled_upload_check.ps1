$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -Raw (Join-Path $root 'Core/Src/host_comm.c')

if ($source -notmatch 'static HAL_StatusTypeDef HostComm_SendPortStatsIfEnabled\(uint8_t port,') {
    throw 'HostComm must provide an enabled-state gate before uploading port stats.'
}

foreach ($case in @(
    @{ Port = '1U'; Flag = 'host_comm_uart1_enabled' },
    @{ Port = '2U'; Flag = 'host_comm_uart2_enabled' },
    @{ Port = '3U'; Flag = 'host_comm_uart3_enabled' },
    @{ Port = '4U'; Flag = 'host_comm_uart4_enabled' }
)) {
    $pattern = 'case\s+' + [regex]::Escape($case.Port) + '\s*:\s*return\s+' + $case.Flag + '\s*;'
    if ($source -notmatch $pattern) {
        throw "HostComm upload gate must check $($case.Flag)."
    }
}

if ($source -match 'status\s*=\s*HostComm_SendPortStats\(1U,') {
    throw 'HostComm_SendStats must not upload UART1 stats without checking enable state.'
}

if ($source -notmatch 'HostComm_SendPortStatsIfEnabled\(1U,') {
    throw 'HostComm_SendStats must use the enabled-state upload wrapper for UART1.'
}

Write-Host 'host_comm enabled upload gate check passed.'
