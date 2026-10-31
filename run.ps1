param([switch]$Test, [ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $sdkArguments=@()
    if (-not $env:VULKAN_SDK -and (Test-Path 'C:\VulkanSDK')) {
        $sdk=Get-ChildItem 'C:\VulkanSDK' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if($sdk) { $sdkArguments=@("-DVulkan_ROOT=$($sdk.FullName)") }
    }
    & cmake -S . -B build -A x64 @sdkArguments
    if($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    & cmake --build build --config $Configuration --parallel 8
    if($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if($Test) {
        & ctest --test-dir build -C $Configuration --output-on-failure
        if($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    } else {
        & ".\build\$Configuration\theone.exe" --scene assets/sandbox.one
        if($LASTEXITCODE -ne 0) { throw 'Engine exited with an error' }
    }
} finally { Pop-Location }
