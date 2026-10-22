param([string]$Vivado='D:/AMDDesignTools/2026.1/Vivado',[string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root 'build/tx_power_tests'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try {
    # Remove only this test's generated counters before recompiling coverage.
    foreach($counter in @('test_tx_power.gcda','tx_power_machine.gcda')) {
        $counterPath=Join-Path $out $counter
        if(Test-Path -LiteralPath $counterPath){Remove-Item -LiteralPath $counterPath}
    }
    & $Compiler -std=c11 -Wall -Wextra -Werror --coverage -O0 "$root/esp32c61/tests/test_tx_power.c" "$root/common/tx_power_machine.c" -o tx_power.exe
    if($LASTEXITCODE){throw 'TX C build failed'}
    & ./tx_power.exe
    if($LASTEXITCODE){throw 'TX C self-check failed'}
    $coverage=& "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c tx_power_machine.gcno
    $coverage
    if($LASTEXITCODE){throw 'TX C coverage extraction failed'}
    $lines=[regex]::Match(($coverage -join "\n"),'Lines executed:([0-9.]+)%')
    $branches=[regex]::Match(($coverage -join "\n"),'Taken at least once:([0-9.]+)%')
    if(!$lines.Success -or !$branches.Success -or [double]$lines.Groups[1].Value -le 95 -or [double]$branches.Groups[1].Value -le 90){throw 'TX C coverage below project target'}
    & "$Vivado/bin/xvlog.bat" --sv "$root/build/src/imports/sources/tx_power_hw.sv" "$root/build/src/imports/sources/tx_top/tx_spi.v" "$root/build/src/sim/tb_tx_power_hw.sv" "$root/build/src/sim/tb_tx_standby_spi.sv"
    if($LASTEXITCODE){throw 'TX RTL compile failed'}
    & "$Vivado/bin/xelab.bat" work.tb_tx_power_hw -s tx_power_sim -debug typical
    if($LASTEXITCODE){throw 'TX RTL elaboration failed'}
    $result=& "$Vivado/bin/xsim.bat" tx_power_sim -runall 2>&1
    $result
    if($LASTEXITCODE -or !($result -match 'PASS tx_power_hw') -or ($result -match '(?i)^\s*(Fatal:|Error:|ERROR:)')){throw 'TX RTL self-check failed'}
    & "$Vivado/bin/xelab.bat" work.tb_tx_standby_spi -s tx_spi_power_sim -debug typical
    if($LASTEXITCODE){throw 'TX SPI elaboration failed'}
    $spi=& "$Vivado/bin/xsim.bat" tx_spi_power_sim -runall 2>&1
    $spi
    if($LASTEXITCODE -or !($spi -match 'PASS tx_standby_spi') -or ($spi -match '(?i)^\s*(Fatal:|Error:|ERROR:)')){throw 'TX SPI self-check failed'}
    & "$Vivado/bin/xvlog.bat" --sv "$root/build/src/imports/sources/tx_top/tx_cfg_top.v" "$root/build/src/imports/sources/tx_top/tx_cfg_streg.v" "$root/build/src/imports/sources/tx_top/tx_cfg_rtreg.v" "$root/build/src/imports/sources/tx_top/tx_cfg_rdreg.v" "$root/build/src/sim/tb_tx_cfg_top_capture_pi.sv" "$root/build/src/sim/tb_tx_cfg_rtreg_pi.sv"
    if($LASTEXITCODE){throw 'TX configuration compile failed'}
    & "$Vivado/bin/xvlog.bat" --sv -i "$root/build/src/imports/sources" "$root/build/src/imports/sources/para_top/para_reg.sv" "$root/build/src/imports/sources/tx_top/tx_top.v" "$root/build/src/imports/sources/tx_top/tx_ctrl.v" "$root/build/src/imports/release/ila_release_stubs.v" "$root/build/src/sim/tb_tx_readback_cdc.sv"
    if($LASTEXITCODE){throw 'TX readback compile failed'}
    foreach($bench in @('tb_tx_cfg_top_capture_pi','tb_tx_cfg_rtreg_pi','tb_tx_readback_cdc')) {
        & "$Vivado/bin/xelab.bat" "work.$bench" -s $bench -debug typical
        if($LASTEXITCODE){throw "$bench elaboration failed"}
        $cfg=& "$Vivado/bin/xsim.bat" $bench -runall 2>&1
        $cfg
        if($LASTEXITCODE -or !($cfg -match 'PASS:') -or ($cfg -match '(?i)^\s*(Fatal:|Error:|ERROR:)')){throw "$bench self-check failed"}
    }
} finally {Pop-Location}
