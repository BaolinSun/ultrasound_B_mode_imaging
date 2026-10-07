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
Write-Host "C test output and coverage: $testOutput"
& $Python -m unittest discover -s $PSScriptRoot -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Python tests failed' }
& $Node --test (Join-Path $PSScriptRoot 'test_web.mjs')
if ($LASTEXITCODE -ne 0) { throw 'Web interaction tests failed' }
