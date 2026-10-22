param([string]$Vivado='D:/AMDDesignTools/2026.1/Vivado', [string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root 'build/afe_power_tests'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try {
    & $Compiler -std=c11 -Wall -Wextra -Werror --coverage -O0 "$root/esp32c61/tests/test_afe_power.c" "$root/common/afe_power_machine.c" -o afe_power.exe
    if($LASTEXITCODE){throw 'AFE C build failed'}
    & ./afe_power.exe
    if($LASTEXITCODE){throw 'AFE C self-check failed'}
    & "$Vivado/bin/xvlog.bat" --sv "$root/build/src/imports/sources/afe_power_hw.sv" "$root/build/src/sim/tb_afe_power_hw.sv"
    if($LASTEXITCODE){throw 'AFE RTL compile failed'}
    & "$Vivado/bin/xelab.bat" work.tb_afe_power_hw -s afe_power_sim -debug typical
    if($LASTEXITCODE){throw 'AFE RTL elaboration failed'}
    $result=& "$Vivado/bin/xsim.bat" afe_power_sim -runall 2>&1
    $result
    if($LASTEXITCODE -or !($result -match 'PASS afe_power_hw') -or ($result -match '(?i)^\s*(Fatal:|Error:|ERROR:)')){throw 'AFE RTL self-check failed'}
} finally {Pop-Location}
