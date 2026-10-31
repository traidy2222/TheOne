param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
    $destination=Join-Path $PSScriptRoot "build\packages\TheOne-0.3.0-$stamp"
    New-Item -ItemType Directory -Path $destination | Out-Null
    foreach($name in 'theone.exe','one_import.exe','one_inspect.exe') {
        $source=Join-Path $PSScriptRoot "build\$Configuration\$name"
        if(-not (Test-Path -LiteralPath $source)) { throw "Build $Configuration first: missing $name" }
        Copy-Item -LiteralPath $source -Destination $destination
    }
    Copy-Item -LiteralPath "build\$Configuration\assets","build\$Configuration\shaders" -Destination $destination -Recurse
    Copy-Item -LiteralPath README.md,docs -Destination $destination -Recurse
    $dependencyRoot=Join-Path $PSScriptRoot 'build\_deps'
    $notices=& rg --files -uu $dependencyRoot -g 'LICENSE*' -g '*COPYING*' -g 'copying*'
    foreach($notice in $notices) {
        $relative=[IO.Path]::GetRelativePath($dependencyRoot,$notice)
        if($relative -notmatch '^[^\\]+-src\\') { continue }
        $target=Join-Path (Join-Path $destination 'licenses') $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $notice -Destination $target
    }
    Copy-Item -LiteralPath CMakeLists.txt -Destination (Join-Path $destination 'dependency-pins.cmake')
    @'
The One 0.3.0 - Windows x64 development sandbox

Launch theone.exe. Running requires a Vulkan-capable GPU/driver and Microsoft
Visual C++ runtime. The Vulkan SDK is only needed when building from source.

WASD move, right mouse look, Tab walk/fly, Space jump, F interact,
F1 editor, F5 reload, F6/F9 save/load gameplay, Escape exit.

Drop GLB/glTF into the window to import. Edit transforms in the inspector.
Scene/game saves are written under saves/ in the current working directory.

one_inspect.exe asset.glb report.json
one_import.exe source.obj output.glb

README.md and docs describe source development, tested capabilities and limits.
'@ | Set-Content -LiteralPath (Join-Path $destination 'START_HERE.txt')
    $hashes=Get-ChildItem -LiteralPath $destination -File -Recurse | ForEach-Object {
        $hash=Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
        "$($hash.Hash.ToLower())  $([IO.Path]::GetRelativePath($destination,$_.FullName))"
    }
    $hashes | Set-Content -LiteralPath (Join-Path $destination 'SHA256SUMS.txt')
    # Exercise the copied package from a different working directory.
    $testDirectory=Join-Path $PSScriptRoot "build\package-check-$stamp"
    New-Item -ItemType Directory -Path $testDirectory | Out-Null
    Push-Location $testDirectory
    try {
        & (Join-Path $destination 'theone.exe') --smoke-test
        if($LASTEXITCODE -ne 0) { throw 'Packaged executable smoke test failed' }
    } finally { Pop-Location }
    Compress-Archive -LiteralPath $destination -DestinationPath "$destination.zip"
    Write-Output "Package verified: $destination.zip"
} finally { Pop-Location }
