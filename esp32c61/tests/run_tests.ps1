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
foreach ($suite in @('framed','sdio_bridge')) {
    $extra = if ($suite -eq 'framed') { 'framed_transfer.c' } else { 'sdio_bridge.c' }
    $suiteOutput = Join-Path $testOutput $suite
    New-Item -ItemType Directory -Force -Path $suiteOutput | Out-Null
    $binary = Join-Path $suiteOutput "test_$suite.exe"
    Push-Location $suiteOutput
    try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g `
        '-I' (Join-Path $projectRoot 'main') '-I' (Join-Path $PSScriptRoot 'stubs') `
        (Join-Path $PSScriptRoot "test_$suite.c") (Join-Path $projectRoot "main\$extra") `
        (Join-Path $projectRoot 'main\raw_transfer.c') -o $binary
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
    } finally { Pop-Location }
}
Write-Host "C test output and coverage: $testOutput"
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
    if (($outputMode -eq 'USB' -and $frameSymbol) -or ($outputMode -eq 'SDIO' -and -not $frameSymbol)) {
        throw "DDR allocation does not match compiled $outputMode mode"
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
& $Python -m unittest discover -s $PSScriptRoot -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Python tests failed' }
& $Node --test (Join-Path $PSScriptRoot 'test_web.mjs')
if ($LASTEXITCODE -ne 0) { throw 'Web interaction tests failed' }
