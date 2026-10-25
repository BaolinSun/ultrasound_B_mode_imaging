param([string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root ('build/cpu0_tests/'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
Push-Location $out
try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 "-I$root/common" "$root/esp32c61/tests/test_cpu0_idle.c" "$root/common/cpu0_idle_core.c" -o cpu0.exe
    if($LASTEXITCODE){throw 'CPU0 core compile failed'}
    & ./cpu0.exe
    if($LASTEXITCODE){throw 'CPU0 core self-check failed'}
    $cov=& "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c cpu0_idle_core.gcno
    $cov
    if($LASTEXITCODE){throw 'CPU0 coverage extraction failed'}
    $text=$cov -join "`n"
    $lines=[regex]::Match($text,'Lines executed:([0-9.]+)%')
    $branches=[regex]::Match($text,'Taken at least once:([0-9.]+)%')
    if(!$lines.Success -or !$branches.Success -or [double]$lines.Groups[1].Value -le 95 -or [double]$branches.Groups[1].Value -le 90){throw 'CPU0 core coverage below project target'}
    foreach($enabled in @(1,0)) {
        $adapter=Join-Path $out "adapter$enabled"
        New-Item -ItemType Directory -Path $adapter -Force | Out-Null
        Push-Location $adapter
        try {
            & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic -Wno-unused-function --coverage -O0 "-DCPU0_IDLE_WFI_ENABLE=$enabled" "-I$root/esp32c61/tests/stubs/cpu0" "$root/esp32c61/tests/test_cpu0_adapter.c" -o adapter.exe
            if($LASTEXITCODE){throw 'CPU0 adapter compile failed'}
            & ./adapter.exe
            if($LASTEXITCODE){throw 'CPU0 adapter self-check failed'}
            $adapterCov=& "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c test_cpu0_adapter.gcno
            $adapterCov
            if($LASTEXITCODE){throw 'CPU0 adapter coverage failed'}
            if($enabled) {
                $production=[regex]::Match(($adapterCov -join "`n"),"(?s)File '[^']*system_ctrl/cpu0_idle.c'.*?Lines executed:([0-9.]+)%.*?Taken at least once:([0-9.]+)%")
                if(!$production.Success -or [double]$production.Groups[1].Value -le 95 -or [double]$production.Groups[2].Value -le 90){throw 'CPU0 adapter coverage below project target'}
            }
        } finally {Pop-Location}
    }
    & $Compiler -std=c11 "-I$root/common" -DCPU0_IDLE_WFI_ENABLE=2 -include "$root/build/src/ps/system_ctrl/cpu0_idle_config.h" -c "$root/esp32c61/tests/test_cpu0_idle.c" -o invalid.o 2> invalid_macro.log
    if(!$LASTEXITCODE -or !(Select-String -LiteralPath invalid_macro.log -SimpleMatch 'CPU0_IDLE_WFI_ENABLE must be' -Quiet)){throw 'Invalid CPU0 WFI macro not rejected correctly'}
    Write-Host 'PASS: invalid CPU0 WFI macro rejected'
} finally {Pop-Location}
