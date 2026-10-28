param([string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root ('build/startup_recovery_tests/'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
Push-Location $out
try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 "-I$root/common" `
      "-I$root/esp32c61/tests/stubs/host" "-I$root/esp32c61/tests/stubs/startup_recovery" `
      "$root/esp32c61/tests/test_startup_recovery.c" -o recovery.exe
    if($LASTEXITCODE){throw 'Startup recovery compile failed'}
    & ./recovery.exe
    if($LASTEXITCODE){throw 'Startup recovery self-check failed'}
    $coverage=& "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c test_startup_recovery.gcno
    $coverage
    if($LASTEXITCODE){throw 'Startup recovery coverage failed'}
    $production=[regex]::Match(($coverage -join "`n"),"(?s)File '[^']*system_ctrl/startup_recovery.c'.*?Lines executed:([0-9.]+)%.*?Taken at least once:([0-9.]+)%")
    if(!$production.Success -or [double]$production.Groups[1].Value -le 95 -or [double]$production.Groups[2].Value -le 90){throw 'Startup recovery coverage below project target'}
} finally {Pop-Location}
