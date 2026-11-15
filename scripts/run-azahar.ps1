# Opens a build in a visible Azahar window. Defaults to dist\FruitSlicer3DS.3dsx, downloading the
# latest release if there is no local build.
param([string]$Rom)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'setup-azahar.ps1')
$exe = Join-Path $root '.tools\azahar\azahar.exe'
if (-not $Rom) {
    $Rom = Join-Path $root 'dist\FruitSlicer3DS.3dsx'
    if (-not (Test-Path $Rom)) {
        New-Item -ItemType Directory -Force (Split-Path $Rom) | Out-Null
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $url = 'https://github.com/RealAnxiousGit/fruit-slicer-3ds/releases/latest/download/FruitSlicer3DS.3dsx'
        Write-Output "No local build, downloading $url"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $Rom
    }
}
if (-not (Test-Path $Rom)) { throw "No build at $Rom" }
Start-Process $exe -WorkingDirectory (Split-Path $exe) -ArgumentList '-w', "`"$((Resolve-Path $Rom).Path)`""
