param(
    [switch]$Test,
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [string[]]$Target=@(),
    [string]$TestRegex='',
    [switch]$AllTests
)
$ErrorActionPreference='Stop'
if($Test -and -not $TestRegex -and -not $AllTests) {
    throw 'Select a focused -TestRegex and -Target, or explicitly use -AllTests.'
}
if($TestRegex -and -not $Target -and -not $AllTests) {
    throw 'Focused tests require -Target so unrelated modules/tests are not built.'
}
if($AllTests -and ($TestRegex -or $Target)) {
    throw 'Use -AllTests alone, or select -Target and -TestRegex for focused validation.'
}
Push-Location $PSScriptRoot
try {
    $sdkArguments=@()
    if (-not $env:VULKAN_SDK -and (Test-Path 'C:\VulkanSDK')) {
        $sdk=Get-ChildItem 'C:\VulkanSDK' -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if($sdk) { $sdkArguments=@("-DVulkan_ROOT=$($sdk.FullName)") }
    }
    & cmake -S . -B build -A x64 -DTHEONE_BUILD_TESTS=ON -DTHEONE_BUILD_TOOLS=ON @sdkArguments
    if($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    $buildArguments=@('--build','build','--config',$Configuration,'--parallel','8')
    if($Target) { $buildArguments+=@('--target')+$Target }
    & cmake @buildArguments
    if($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if($Test -or $TestRegex -or $AllTests) {
        $testArguments=@('--test-dir','build','-C',$Configuration,'--output-on-failure','--no-tests=error')
        if($TestRegex) { $testArguments+=@('-R',$TestRegex) }
        & ctest @testArguments
        if($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    } else { Write-Output 'Engine libraries and tools built. Games have their own projects and launchers.' }
} finally { Pop-Location }
