param([string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root ('build/health_tests/'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
Push-Location $out
try {
 & $Compiler -std=c11 -Wall -Wextra -Werror --coverage -O0 "-I$root/esp32c61/tests" "$root/esp32c61/tests/test_system_health.c" -o health.exe
 if($LASTEXITCODE){throw 'Health test compile failed'}
 & ./health.exe
 if($LASTEXITCODE){throw 'Health test failed'}
 $cov=& "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c test_system_health.gcno
 $cov
 if($LASTEXITCODE){throw 'Health coverage failed'}
 $part=[regex]::Match(($cov -join "`n"),"(?s)File '[^']*system_ctrl/system_health.c'.*?Lines executed:([0-9.]+)%.*?Taken at least once:([0-9.]+)%")
 if(!$part.Success -or [double]$part.Groups[1].Value -le 95 -or [double]$part.Groups[2].Value -le 90){throw 'Health core coverage below target'}
} finally {Pop-Location}
