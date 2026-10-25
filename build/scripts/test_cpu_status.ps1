param([string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root ('build/cpu_status_tests/'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
$cjson=Join-Path $root 'esp32c61/managed_components/espressif__cjson/cJSON'
Push-Location $out
try {
 & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 "-I$root/common" "-I$root/esp32c61/main" "-I$cjson" `
   "$root/esp32c61/tests/test_cpu_status.c" "$root/esp32c61/main/cpu_status_cache.c" "$root/esp32c61/main/cpu_status_json.c" "$cjson/cJSON.c" -o cpu_status.exe
 if($LASTEXITCODE){throw 'CPU status compile failed'}
 & ./cpu_status.exe
 if($LASTEXITCODE){throw 'CPU status self-check failed'}
 & "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c test_cpu_status.gcno cpu_status_cache.gcno cpu_status_json.gcno
 if($LASTEXITCODE){throw 'CPU status coverage failed'}
} finally {Pop-Location}
