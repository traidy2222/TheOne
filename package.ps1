$ErrorActionPreference='Stop'
$destination=Join-Path $PSScriptRoot ("build/packages/TheOne-Engine-"+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $destination | Out-Null
foreach($name in 'include','src','cmake','shaders','tools','tests','docs','CMakeLists.txt','README.md','run.ps1','package.ps1','.clang-format') {
    $source=Join-Path $PSScriptRoot $name
    $files=if(Test-Path -LiteralPath $source -PathType Container) { Get-ChildItem -LiteralPath $source -File -Recurse } else { Get-Item -LiteralPath $source }
    foreach($file in $files) {
        if($file.FullName -match '[\\/]__pycache__[\\/]' -or $file.Extension -eq '.pyc') { continue }
        $target=Join-Path $destination ([IO.Path]::GetRelativePath($PSScriptRoot,$file.FullName))
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}
$hashes=Get-ChildItem -LiteralPath $destination -File -Recurse | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower())  $([IO.Path]::GetRelativePath($destination,$_.FullName))"
}
$hashes | Set-Content -LiteralPath (Join-Path $destination 'SHA256SUMS.txt')
Compress-Archive -LiteralPath $destination -DestinationPath "$destination.zip"
Write-Output "Engine source SDK: $destination.zip"
