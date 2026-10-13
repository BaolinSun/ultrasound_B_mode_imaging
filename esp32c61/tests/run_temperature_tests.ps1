param(
 [string]$Compiler='D:\AMDDesignTools\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe',
 [string]$OutputDirectory=(Join-Path $PSScriptRoot ('output/temperature-'+(Get-Date -Format 'yyyyMMdd-HHmmssfff')))
)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ps=Join-Path $root 'build/src/ps/system_ctrl'
$main=Join-Path $root 'esp32c61/main'
$bsp=Join-Path $root 'workspace/ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0/include'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
Push-Location $OutputDirectory
try {
 & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g '-I' $ps '-I' $main '-I' (Join-Path $root 'common') `
   (Join-Path $PSScriptRoot 'test_temperature.c') (Join-Path $ps 'temp.c') (Join-Path $main 'temperature_cache.c') -o test_temperature.exe
 if($LASTEXITCODE -ne 0){throw 'Temperature model compilation failed'}
 & ./test_temperature.exe
 if($LASTEXITCODE -ne 0){throw 'Temperature model tests failed'}
 & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 -g '-I' (Join-Path $PSScriptRoot 'stubs/temperature') '-I' $ps '-I' $bsp `
   '-include' (Join-Path $PSScriptRoot 'stubs/temperature/preinclude.h') `
   (Join-Path $PSScriptRoot 'test_temperature_hw.c') (Join-Path $ps 'temp_hw.c') -o test_temperature_hw.exe
 if($LASTEXITCODE -ne 0){throw 'Temperature register adapter compilation failed'}
 & ./test_temperature_hw.exe
 if($LASTEXITCODE -ne 0){throw 'Temperature hardware fault injection failed'}
 & (Join-Path (Split-Path $Compiler -Parent) 'gcov.exe') -b -c test_temperature.gcno temp.gcno temperature_cache.gcno temp_hw.gcno
 if($LASTEXITCODE -ne 0){throw 'Temperature coverage failed'}
} finally {Pop-Location}
