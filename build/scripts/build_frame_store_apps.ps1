param([string]$Bsp='', [string]$Tools='D:/AMDDesignTools/2026.1', [ValidateSet('header','0','1')][string]$Wfi='header')
$ErrorActionPreference='Stop'
$root=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)).Replace('\','/')
if(!$Bsp) {$Bsp="$root/workspace/ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0"}
$Bsp=[IO.Path]::GetFullPath($Bsp).Replace('\','/')
if(!(Test-Path "$Bsp/cortexa9_toolchain.cmake")) {throw 'Missing Vitis standalone BSP'}
$env:PATH="$Tools/Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin;$Tools/Vitis/bin;$env:PATH"
$env:CC='';$env:CXX=''
$cmake="$Tools/Vitis/tps/win64/cmake-4.0.1/bin/cmake.exe"
foreach($mode in @('usb','sdio')) {
    $value=if($mode -eq 'usb'){0}else{1}
    $suffix=if($Wfi -eq 'header'){''}else{"_wfi$Wfi"}
    $wfiFlag=if($Wfi -eq 'header'){''}else{" -DCPU0_IDLE_WFI_ENABLE=$Wfi"}
    $out="$root/build/vitis_frame_store_$mode$suffix"
    & $cmake -S "$root/workspace/B_mode_focus_imaging/src" -B $out -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$Tools/Vitis/bin/ninja.exe" `
        "-DCMAKE_TOOLCHAIN_FILE=$Bsp/cortexa9_toolchain.cmake" `
        "-DCMAKE_MODULE_PATH=$Bsp" "-DCMAKE_LIBRARY_PATH=$Bsp/lib" `
        "-DCMAKE_INCLUDE_PATH=$Bsp/include" "-DCMAKE_SPECS_FILE=$Bsp/Xilinx.spec" `
        "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map,$out/B_mode_focus_imaging.map" `
        "-DTOOLCHAIN_C_FLAGS= -DSDT -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -DDATA_OUTPUT_MODE=$value$wfiFlag" `
        "-DCMAKE_C_FLAGS= -DSDT -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -DDATA_OUTPUT_MODE=$value$wfiFlag -specs=$Bsp/Xilinx.spec -I$Bsp/include"
    if($LASTEXITCODE) {throw "$mode configure failed"}
    & $cmake --build $out -j 4
    if($LASTEXITCODE) {throw "$mode build failed"}
    & "$PSScriptRoot/check_frame_store_elf.ps1" -Elf "$out/B_mode_focus_imaging.elf" -Tools $Tools
}
# This script deliberately does not pack BOOT.BIN or access hardware.
