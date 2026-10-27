param([string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root ('build/provisioning_tests/'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
Push-Location $out
try {
 foreach($suite in @('store','wifi')) {
  New-Item -ItemType Directory -Path $suite | Out-Null
  Push-Location $suite
  try {
  & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic --coverage -O0 "-I$root/common" "-I$root/esp32c61/main" "-I$root/esp32c61/tests/stubs/provisioning" `
    "$root/esp32c61/tests/test_provisioning_$suite.c" "$root/esp32c61/main/device_identity.c" "$root/esp32c61/main/config_model.c" -o "$suite.exe"
  if($LASTEXITCODE){throw "$suite compile failed"}
  & "./$suite.exe"
  if($LASTEXITCODE){throw "$suite self-check failed"}
  $coverage=@("test_provisioning_$suite.gcno")
  if($suite -eq 'store') {$coverage+='device_identity.gcno'}
  & "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c @coverage
  if($LASTEXITCODE){throw "$suite coverage failed"}
  } finally {Pop-Location}
 }
} finally {Pop-Location}
