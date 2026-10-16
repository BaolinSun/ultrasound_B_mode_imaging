param(
    [string]$Compiler = 'D:\AMDDesignTools\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe',
    [string]$Python = 'python',
    [string]$Node = 'node'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testOutput = Join-Path (Join-Path $PSScriptRoot 'output') (Get-Date -Format 'yyyyMMdd-HHmmssfff')
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Supply -Compiler with a host GCC path.' }
$env:PATH = "$(Split-Path $Compiler -Parent);$env:PATH"
$env:PYTHONUTF8 = '1'
$socketOutput = Join-Path $testOutput 'tcp-socket'
New-Item -ItemType Directory -Force -Path $socketOutput | Out-Null
Push-Location $socketOutput
try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g `
        '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $PSScriptRoot 'socket_stubs') `
        (Join-Path $PSScriptRoot 'test_tcp_socket_tuning.c') (Join-Path $projectRoot 'main/tcp_socket_tuning.c') `
        -o test_socket.exe
    if ($LASTEXITCODE -ne 0) { throw 'TCP socket adapter compilation failed' }
    & ./test_socket.exe
    if ($LASTEXITCODE -ne 0) { throw 'TCP socket adapter self-check failed' }
    & (Join-Path (Split-Path $Compiler -Parent) 'gcov.exe') -b -c tcp_socket_tuning.gcno
    if ($LASTEXITCODE -ne 0) { throw 'TCP socket adapter coverage failed' }
    foreach ($queueLimit in @(128,160)) {
        & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic "-DTCP_SND_QUEUELEN=$queueLimit" `
            '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $PSScriptRoot 'socket_stubs') `
            (Join-Path $PSScriptRoot 'test_tcp_socket_tuning.c') (Join-Path $projectRoot 'main/tcp_socket_tuning.c') `
            -o "test_socket_$queueLimit.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Large TCP queue compilation failed' }
        & "./test_socket_$queueLimit.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Large TCP queue self-check failed' }
    }
} finally { Pop-Location }
$executable = Join-Path $testOutput 'test_core.exe'
Push-Location $testOutput
try {
& $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g `
    '-I' (Join-Path $projectRoot 'main') (Join-Path $PSScriptRoot 'test_core.c') `
    (Join-Path $projectRoot 'main\raw_transfer.c') (Join-Path $projectRoot 'main\config_model.c') `
    -o $executable
if ($LASTEXITCODE -ne 0) { throw 'C test compilation failed' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'C core tests failed' }
$gcov = Join-Path (Split-Path $Compiler -Parent) 'gcov.exe'
& $gcov -b -c 'raw_transfer.gcno' 'config_model.gcno'
if ($LASTEXITCODE -ne 0) { throw 'C coverage report failed' }
} finally { Pop-Location }
$sdkBsp=Join-Path (Split-Path $projectRoot -Parent) 'workspace/ubmi_p/ps7_cortexa9_0/standalone_ps7_cortexa9_0/bsp'
if (Test-Path (Join-Path $sdkBsp 'include/xil_printf.h')) {
    Push-Location $testOutput
    try {
        & $Compiler -std=c11 '-I' (Join-Path $PSScriptRoot 'stubs/formatter') '-I' (Join-Path $sdkBsp 'include') `
            (Join-Path $PSScriptRoot 'test_xil_log_format.c') (Join-Path $sdkBsp 'libsrc/standalone/src/common/xil_printf.c') `
            -o test_actual_formatter.exe
        if ($LASTEXITCODE -ne 0) { throw 'Actual SDK formatter compilation failed' }
        & ./test_actual_formatter.exe
        if ($LASTEXITCODE -ne 0) { throw 'Actual SDK formatter regression failed' }
    } finally { Pop-Location }
}
foreach ($suite in @('framed','tcp_transfer','sdio_bridge','bridge_crc')) {
    $extra = if ($suite -eq 'framed') { 'framed_transfer.c' } elseif ($suite -eq 'bridge_crc') { 'bridge_crc.c' } elseif ($suite -eq 'tcp_transfer') { 'tcp_transfer.c' } else { 'sdio_bridge.c' }
    [string[]]$crcSources = if ($suite -eq 'sdio_bridge') { @((Join-Path $projectRoot 'main/bridge_crc.c')) } else { @() }
    [string[]]$crcFlags = if ($suite -eq 'bridge_crc') { '-DCRC_TEST_ORACLE=1' } else { @() }
    $suiteOutput = Join-Path $testOutput $suite
    New-Item -ItemType Directory -Force -Path $suiteOutput | Out-Null
    $binary = Join-Path $suiteOutput "test_$suite.exe"
    Push-Location $suiteOutput
    try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g `
        '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $PSScriptRoot 'stubs') `
        (Join-Path $PSScriptRoot "test_$suite.c") (Join-Path $projectRoot "main\$extra") `
        (Join-Path $projectRoot 'main\raw_transfer.c') @crcSources @crcFlags -o $binary
    if ($LASTEXITCODE -ne 0) { throw "$suite compilation failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "$suite tests failed" }
    if ($suite -eq 'sdio_bridge') {
        foreach ($fault in @('mutex','task','task-init','dma','pool','partial-pool','init','start','register','load','write',
                             'runtime-init','runtime-start','runtime-register','runtime-load')) {
            & $binary $fault
            if ($LASTEXITCODE -ne 0) { throw "SDIO $fault injection failed" }
        }
    }
    & $gcov -b -c ([System.IO.Path]::GetFileNameWithoutExtension($extra)+'.gcno')
    if ($LASTEXITCODE -ne 0) { throw "$suite coverage report failed" }
    if ($suite -eq 'tcp_transfer') {
        & $Compiler -std=c11 -DWIFI_FRAME_TRANSPORT=99 '-I' (Join-Path $projectRoot 'main') `
            -c (Join-Path $PSScriptRoot 'test_tcp_transfer.c') -o invalid_tcp.o 2> invalid_tcp.log
        if ($LASTEXITCODE -eq 0) { throw 'Invalid Wi-Fi transport macro accepted' }
        if (-not (Select-String -LiteralPath invalid_tcp.log -SimpleMatch 'WIFI_FRAME_TRANSPORT must be' -Quiet)) {
            throw 'Invalid transport failed for unexpected reason'
        }
        Write-Host 'PASS: invalid Wi-Fi transport macro rejected'
        foreach ($tcpStage in @(4096,11520)) {
            & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic "-DTCP_STAGE_BYTES=$tcpStage" `
                '-I' (Join-Path $projectRoot 'main') (Join-Path $PSScriptRoot 'test_tcp_transfer.c') `
                (Join-Path $projectRoot 'main/tcp_transfer.c') (Join-Path $projectRoot 'main/raw_transfer.c') `
                -o "test_stage_$tcpStage.exe"
            if ($LASTEXITCODE -ne 0) { throw 'TCP stage variation compile failed' }
            & "./test_stage_$tcpStage.exe"
            if ($LASTEXITCODE -ne 0) { throw 'TCP stage variation test failed' }
        }
    }
    if ($suite -eq 'bridge_crc') {
        & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic -DCRC_TEST_ALLOC=1 -DCRC_TEST_ORACLE=1 `
            '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $PSScriptRoot 'stubs') `
            (Join-Path $PSScriptRoot 'test_bridge_crc.c') (Join-Path $projectRoot 'main/bridge_crc.c') `
            (Join-Path $projectRoot 'main/raw_transfer.c') -o test_crc_alloc.exe
        if ($LASTEXITCODE -ne 0) { throw 'CRC allocator variation compile failed' }
        & ./test_crc_alloc.exe
        if ($LASTEXITCODE -ne 0) { throw 'CRC allocation fallback tests failed' }
    }
    } finally { Pop-Location }
}
Write-Host "C test output and coverage: $testOutput"
$cmdOutput=Join-Path $testOutput 'sdio-command'
New-Item -ItemType Directory -Force -Path $cmdOutput | Out-Null
Push-Location $cmdOutput
try {
    $cmdSources=Join-Path (Split-Path $projectRoot -Parent) 'build/src/ps/data_transfer'
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g '-I' $cmdSources `
        (Join-Path $PSScriptRoot 'test_sdio_cmd_engine.c') (Join-Path $cmdSources 'sdio_cmd_engine.c') -o test_command.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDIO command engine compilation failed' }
    & ./test_command.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDIO command engine tests failed' }
    & $gcov -b -c sdio_cmd_engine.gcno
    if ($LASTEXITCODE -ne 0) { throw 'SDIO command engine coverage failed' }
} finally { Pop-Location }
foreach ($outputMode in @('SDIO','USB')) {
$hostOutput = Join-Path $testOutput "host-$outputMode"
New-Item -ItemType Directory -Force -Path $hostOutput | Out-Null
Push-Location $hostOutput
try {
    $hostSources = Join-Path (Split-Path $projectRoot -Parent) 'build/src/ps/data_transfer'
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g "-DDATA_OUTPUT_MODE=DATA_OUTPUT_$outputMode" `
        '-I' $hostSources '-I' (Join-Path $PSScriptRoot 'stubs/host') `
        (Join-Path $PSScriptRoot 'test_sdio_host_bridge.c') (Join-Path $hostSources 'sdio_bridge.c') `
        -o test_host.exe
    if ($LASTEXITCODE -ne 0) { throw 'Zynq host bridge compilation failed' }
    & ./test_host.exe
    if ($LASTEXITCODE -ne 0) { throw 'Zynq host bridge tests failed' }
    $hostNm=Join-Path (Split-Path $Compiler -Parent) 'nm.exe'
    $frameSymbol=& $hostNm -S test_host.exe | Select-String '\btest_frame$'
    if ($LASTEXITCODE -ne 0) { throw 'Host symbol read failed' }
    if ($frameSymbol) {
        throw "$outputMode build must not allocate a synthetic DDR test frame"
    }
    & $gcov -b -c sdio_bridge.gcno
    if ($LASTEXITCODE -ne 0) { throw 'Zynq host bridge coverage report failed' }
} finally { Pop-Location }
}
# Invalid mode must fail at compile time, not silently select a data path.
Push-Location $testOutput
try {
    & $Compiler -std=c11 -DDATA_OUTPUT_MODE=99 '-I' $hostSources `
        '-I' (Join-Path $PSScriptRoot 'stubs/host') -c (Join-Path $hostSources 'sdio_bridge.c') `
        -o invalid_mode.o 2> invalid_mode.log
    if ($LASTEXITCODE -eq 0) { throw 'Invalid output mode was accepted' }
    if (-not (Select-String -LiteralPath invalid_mode.log -SimpleMatch 'DATA_OUTPUT_MODE must be' -Quiet)) {
        throw 'Invalid-mode compilation failed for an unexpected reason'
    }
    Write-Host 'PASS: invalid output macro rejected'
} finally { Pop-Location }
foreach ($suite in @('uart_framer','uart_bridge')) {
    $suiteOutput=Join-Path $testOutput $suite
    New-Item -ItemType Directory -Force -Path $suiteOutput | Out-Null
    Push-Location $suiteOutput
    try {
        $sources=@((Join-Path $PSScriptRoot "test_$suite.c"),(Join-Path $projectRoot 'main/uart_log_framer.c'))
        if ($suite -eq 'uart_bridge') {
            $sources+=(Join-Path $projectRoot 'main/zynq_uart_bridge.c')
            $sources+=(Join-Path $projectRoot 'main/temperature_cache.c')
        }
        & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g `
            '-I' (Join-Path $PSScriptRoot 'stubs/uart') '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $projectRoot '../common') @sources -o test_uart.exe
        if ($LASTEXITCODE -ne 0) { throw "$suite compile failed" }
        $uartOutput=& ./test_uart.exe
        if ($LASTEXITCODE -ne 0) { throw "$suite failed" }
        $uartOutput | Write-Host
        if ($suite -eq 'uart_bridge') {
            if (($uartOutput -join "`n") -match 'lost partial|hidden') { throw 'Lost/disconnected bytes were incorrectly forwarded' }
            if (($uartOutput -join "`n") -notmatch '\[ZYNQ\] next') { throw 'UART did not recover after overflow' }
            foreach ($fault in 1..5) {
                & ./test_uart.exe $fault
                if ($LASTEXITCODE -ne 0) { throw "UART init failure $fault failed" }
            }
            & $gcov -b -c zynq_uart_bridge.gcno
            if ($LASTEXITCODE -ne 0) { throw 'UART bridge coverage failed' }
            & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic -DCONFIG_ZYNQ_UART_BRIDGE_ENABLE=0 `
                '-I' (Join-Path $PSScriptRoot 'stubs/uart') '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $projectRoot '../common') @sources -o test_disabled.exe
            if ($LASTEXITCODE -ne 0) { throw 'Disabled UART bridge compilation failed' }
            & ./test_disabled.exe
            if ($LASTEXITCODE -ne 0) { throw 'Disabled UART bridge failed' }
        }
        & $gcov -b -c uart_log_framer.gcno
        if ($LASTEXITCODE -ne 0) { throw 'UART framing coverage failed' }
    } finally { Pop-Location }
}
$zynqOutput=Join-Path $testOutput 'zynq-uart'
New-Item -ItemType Directory -Force -Path $zynqOutput | Out-Null
Push-Location $zynqOutput
try {
    $psSources=Join-Path (Split-Path $projectRoot -Parent) 'build/src/ps'
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g `
        '-I' (Join-Path $PSScriptRoot 'stubs/zynq_uart') '-I' (Join-Path $psSources 'uart') `
        '-I' (Join-Path $PSScriptRoot 'stubs/ps_control') '-I' (Join-Path $PSScriptRoot 'stubs/temperature') `
        '-I' (Join-Path $projectRoot '../common') '-include' (Join-Path $PSScriptRoot 'stubs/ps_control/prelude.h') `
        (Join-Path $PSScriptRoot 'test_ps_wifi_control.c') (Join-Path $psSources 'uart/uart_log.c') `
        (Join-Path $psSources 'uart/uart.c') -o test_zynq_uart.exe
    if ($LASTEXITCODE -ne 0) { throw 'Zynq UART log tests compilation failed' }
    & ./test_zynq_uart.exe
    if ($LASTEXITCODE -ne 0) { throw 'Zynq UART log tests failed' }
    & $gcov -b -c uart_log.gcno uart.gcno
    if ($LASTEXITCODE -ne 0) { throw 'Zynq UART log coverage failed' }
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic -ffunction-sections -fdata-sections `
        '-I' (Join-Path $PSScriptRoot 'stubs/zynq_uart') '-I' (Join-Path $psSources 'uart') `
        '-I' (Join-Path $PSScriptRoot 'stubs/ps_control') '-I' (Join-Path $PSScriptRoot 'stubs/temperature') `
        '-include' (Join-Path $PSScriptRoot 'stubs/ps_control/prelude.h') `
        (Join-Path $PSScriptRoot 'test_zynq_uart.c') (Join-Path $psSources 'uart/uart_log.c') `
        (Join-Path $PSScriptRoot 'stubs/zynq_uart/unused_rx.c') `
        (Join-Path $psSources 'outbyte.c') (Join-Path $PSScriptRoot 'stubs/zynq_uart/bsp_outbyte.c') `
        '-Wl,--gc-sections,--wrap=outbyte' -o test_original_log.exe
    if($LASTEXITCODE -ne 0) { throw 'Original UART init/outbyte regression compile failed' }
    & ./test_original_log.exe
    if($LASTEXITCODE -ne 0) { throw 'Original UART init/outbyte regression failed' }
} finally { Pop-Location }
& (Join-Path $PSScriptRoot 'run_temperature_tests.ps1') -Compiler $Compiler -OutputDirectory (Join-Path $testOutput 'temperature')
$controlOutput=Join-Path $testOutput 'wifi-control'
New-Item -ItemType Directory -Path $controlOutput -Force | Out-Null
& $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic '-I' (Join-Path $PSScriptRoot 'stubs/bram') `
    '-I' (Join-Path $PSScriptRoot 'stubs/ps_control') (Join-Path $PSScriptRoot 'test_beamform_control.c') `
    -o (Join-Path $controlOutput 'beamform.exe')
if($LASTEXITCODE -ne 0) { throw 'Beamform MMIO regression compile failed' }
& (Join-Path $controlOutput 'beamform.exe')
if($LASTEXITCODE -ne 0) { throw 'Beamform MMIO regression failed' }
foreach($suite in @('wifi_protocol','wifi_control_service')) {
    [string[]]$extra=if($suite -eq 'wifi_control_service') { @('-I',(Join-Path $PSScriptRoot 'stubs/control')) } else { @() }
    $binary=Join-Path $controlOutput "$suite.exe"
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic '-I' (Join-Path $projectRoot '../common') `
        '-I' (Join-Path $projectRoot 'main') @extra (Join-Path $PSScriptRoot "test_$suite.c") -o $binary
    if($LASTEXITCODE -ne 0) { throw "$suite compile failed" }
    & $binary
    if($LASTEXITCODE -ne 0) { throw "$suite failed" }
    if($suite -eq 'wifi_control_service') {
        foreach($fault in @('mutex','queue','task')) {
            & $binary $fault
            if($LASTEXITCODE -ne 0) { throw "$suite $fault failed" }
        }
    }
}
& $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic '-I' (Join-Path $PSScriptRoot 'stubs/zynq_uart') `
    '-I' (Join-Path $PSScriptRoot 'stubs/temperature') (Join-Path $PSScriptRoot 'test_afe_spi_timeout.c') `
    -o (Join-Path $controlOutput 'afe_spi.exe')
if($LASTEXITCODE -ne 0) { throw 'AFE SPI timeout test compile failed' }
& (Join-Path $controlOutput 'afe_spi.exe')
if($LASTEXITCODE -ne 0) { throw 'AFE SPI timeout test failed' }
$cjson=Join-Path $projectRoot 'managed_components/espressif__cjson/cJSON'
if(Test-Path (Join-Path $cjson 'cJSON.c')) {
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic '-I' (Join-Path $projectRoot '../common') `
        '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $PSScriptRoot 'stubs/control_http') '-I' $cjson `
        (Join-Path $PSScriptRoot 'test_wifi_control_http.c') (Join-Path $cjson 'cJSON.c') -o (Join-Path $controlOutput 'http.exe')
    if($LASTEXITCODE -ne 0) { throw 'Control HTTP test compile failed' }
    & (Join-Path $controlOutput 'http.exe')
    if($LASTEXITCODE -ne 0) { throw 'Control HTTP test failed' }
} else { Write-Warning 'Control HTTP native test requires the installed managed cJSON component' }
& $Python -m unittest discover -s $PSScriptRoot -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Python tests failed' }
& $Node --test (Join-Path $PSScriptRoot 'test_web.mjs')
if ($LASTEXITCODE -ne 0) { throw 'Web interaction tests failed' }
