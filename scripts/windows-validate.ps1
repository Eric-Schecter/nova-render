# Same CMake/Ninja/MSVC toolchain used by CLion. Run from an MSVC developer shell,
# or supply VsDevCmd, CMake and Ninja paths explicitly.
param(
    [Parameter(Mandatory=$true)][string]$VcpkgRoot,
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [string]$VsDevCmd,
    [switch]$Vulkan,
    [switch]$Hardware,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$VulkanICD,
    [string]$VulkanLoaderDirectory,
    [ValidateSet('arm64','x64')][string]$Architecture = $(if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { 'arm64' } else { 'x64' })
)
$ErrorActionPreference = 'Stop'
if ($VsDevCmd) {
    $vsRoot = Split-Path (Split-Path (Split-Path $VsDevCmd -Parent) -Parent) -Parent
    $hostArch = 'x64'
    if (($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') -and
        (Test-Path "$vsRoot/VC/Tools/MSVC/*/bin/Hostarm64/arm64/cl.exe")) { $hostArch = 'arm64' }
    # A batch file avoids PowerShell 5.1 / cmd nested-quote differences for Program Files.
    $environmentScript = Join-Path ([IO.Path]::GetTempPath()) ('nova-msvc-' + [guid]::NewGuid().ToString('N') + '.cmd')
    try {
        @('@echo off', "call `"$VsDevCmd`" -arch=$Architecture -host_arch=$hostArch >nul", 'if errorlevel 1 exit /b 1', 'set') |
            Set-Content -Path $environmentScript -Encoding Default
        $environment = & cmd.exe /d /c $environmentScript
        if ($LASTEXITCODE) { throw 'MSVC environment initialization failed' }
    } finally {
        Remove-Item -LiteralPath $environmentScript -ErrorAction SilentlyContinue
    }
    foreach ($line in $environment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}
$Ninja = (Get-Command $Ninja).Source
$CMake = (Get-Command $CMake).Source
$env:PATH = (Split-Path $Ninja) + ';' + $env:PATH
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root $(if ($Vulkan) { 'build-windows-vulkan' } else { 'build-windows' })
$enableHardware = if ($Hardware) { 'ON' } else { 'OFF' }
$enableVulkan = if ($Vulkan) { 'ON' } else { 'OFF' }
& $CMake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DNOVA_ENABLE_VULKAN=$enableVulkan" `
    "-DNOVA_TEST_DX12_HARDWARE=$enableHardware" "-DNOVA_BUILD_RAY_PROBES=$enableHardware" `
    "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DVCPKG_TARGET_TRIPLET=$Architecture-windows" `
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake"
if ($LASTEXITCODE) { throw 'Configure failed' }
& $CMake --build $build --parallel
if ($LASTEXITCODE) { throw 'Build failed' }
if ($VulkanLoaderDirectory) { $env:PATH = $VulkanLoaderDirectory + ';' + $env:PATH }
if ($VulkanICD) { $env:VK_DRIVER_FILES = (Resolve-Path $VulkanICD).Path }
$CTest = Join-Path (Split-Path $CMake) 'ctest.exe'
& $CTest --test-dir $build --output-on-failure --output-log (Join-Path $build 'matrix.log')
if ($LASTEXITCODE) { throw 'Platform/API matrix failed; inspect matrix.log and Testing/Temporary/LastTest.log' }
