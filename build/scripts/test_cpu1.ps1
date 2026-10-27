param([string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root ('build/cpu1_tests/'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
Push-Location $out
try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 "-I$root/common" "$root/esp32c61/tests/test_cpu1_control.c" "$root/common/cpu1_control.c" -o cpu1.exe
    if($LASTEXITCODE){throw 'CPU1 self-check compile failed'}
    & ./cpu1.exe
    if($LASTEXITCODE){throw 'CPU1 self-check failed'}
    $cov=& "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c cpu1_control.gcno
    $cov
    if($LASTEXITCODE){throw 'CPU1 coverage extraction failed'}
    $text=$cov -join "`n"
    $lines=[regex]::Match($text,'Lines executed:([0-9.]+)%')
    $branches=[regex]::Match($text,'Taken at least once:([0-9.]+)%')
    if(!$lines.Success -or !$branches.Success -or [double]$lines.Groups[1].Value -le 95 -or [double]$branches.Groups[1].Value -le 90){throw 'CPU1 coverage below project target'}
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 "-I$root/esp32c61/tests/stubs/cpu1" "-I$root/common" "$root/esp32c61/tests/test_cpu1_adapter.c" -o adapter.exe
    if($LASTEXITCODE){throw 'CPU1 startup adapter compile failed'}
    & ./adapter.exe
    if($LASTEXITCODE){throw 'CPU1 startup adapter self-check failed'}
    & "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c test_cpu1_adapter.gcno
    if($LASTEXITCODE){throw 'CPU1 startup adapter coverage extraction failed'}
} finally {Pop-Location}
