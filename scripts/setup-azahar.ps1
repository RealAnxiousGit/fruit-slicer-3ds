# Installs a portable copy of the Azahar emulator into .tools\azahar for testing the game.
$ErrorActionPreference = 'Stop'
$version = '2126.2'
$root = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $root '.tools\azahar'
if (-not (Test-Path (Join-Path $dest 'azahar.exe'))) {
    $tmp = Join-Path $root '.tools\azahar.zip'
    New-Item -ItemType Directory -Force (Split-Path $tmp) | Out-Null
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $url = "https://github.com/azahar-emu/azahar/releases/download/$version/azahar-windows-msvc-$version.zip"
    Write-Output "Downloading $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $tmp
    $unpacked = Join-Path $root '.tools\azahar-unpack'
    Expand-Archive -Force $tmp $unpacked
    $exe = Get-ChildItem $unpacked -Recurse -Filter 'azahar.exe' | Select-Object -First 1
    if (-not $exe) { throw 'azahar.exe was not in the download' }
    Move-Item $exe.Directory.FullName $dest
    Remove-Item -Recurse -Force $unpacked, $tmp -ErrorAction SilentlyContinue
}
# A "user" folder beside the exe makes Azahar portable: settings and the virtual SD card stay here.
New-Item -ItemType Directory -Force (Join-Path $dest 'user') | Out-Null
# Homebrew audio refuses to start without this file. Azahar's emulated DSP doesn't read its contents,
# so a blank placeholder is enough here. Real hardware needs a genuine dump (see README).
$sd3ds = Join-Path $dest 'user\sdmc\3ds'
New-Item -ItemType Directory -Force $sd3ds | Out-Null
$firm = Join-Path $sd3ds 'dspfirm.cdc'
if (-not (Test-Path $firm)) { [IO.File]::WriteAllBytes($firm, (New-Object byte[] 4096)) }

# Adds a "Keyboard" controls profile and makes it the active one. Everything else in the file is kept.
# Touch stays on the mouse. Keys are Qt key codes; Circle Pad and C-Stick use a modifier nothing else needs.
$cfgDir = Join-Path $dest 'user\config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
$cfg = Join-Path $cfgDir 'qt-config.ini'
$keys = [ordered]@{
    button_a = 90; button_b = 88; button_x = 67; button_y = 86   # Z X C V
    button_up = 16777235; button_down = 16777237; button_left = 16777234; button_right = 16777236
    button_l = 81; button_r = 69; button_zl = 49; button_zr = 51  # Q E 1 3
    button_start = 16777220; button_select = 16777219             # Enter, Backspace
    button_home = 66; button_power = 78                           # B N (Power's default V is Y here)
}
function Pad($u, $d, $l, $r) {
    '"down:code${0}{1}$1engine$0keyboard,engine:analog_from_button,left:code${0}{2}$1engine$0keyboard,modifier:code$077$1engine$0keyboard,modifier_scale:0.500000,right:code${0}{3}$1engine$0keyboard,up:code${0}{4}$1engine$0keyboard"' -f '0', $d, $l, $r, $u
}
$block = New-Object System.Collections.ArrayList
[void]$block.Add('profile\default=false')
[void]$block.Add('profile=1')
[void]$block.Add('profiles\2\name\default=false')
[void]$block.Add('profiles\2\name=Keyboard')
foreach ($k in $keys.Keys) {
    [void]$block.Add("profiles\2\$k\default=false")
    [void]$block.Add("profiles\2\$k=`"code:$($keys[$k]),engine:keyboard`"")
}
[void]$block.Add('profiles\2\circle_pad\default=false')
[void]$block.Add('profiles\2\circle_pad=' + (Pad 87 83 65 68))    # W S A D
[void]$block.Add('profiles\2\c_stick\default=false')
[void]$block.Add('profiles\2\c_stick=' + (Pad 73 75 74 76))       # I K J L
[void]$block.Add('profiles\size=2')
$lines = if (Test-Path $cfg) { @(Get-Content $cfg) } else { @('[Controls]') }
if ($lines -notcontains '[Controls]') { $lines = @('[Controls]') + $lines }
$kept = $lines | Where-Object { $_ -notmatch '^profile(\\default)?=' -and $_ -notmatch '^profiles\\2\\' -and $_ -notmatch '^profiles\\size=' }
# Use Vulkan by default for testing and smooth hardware acceleration; disable vsync so late frames don't halve framerate.
$speed = [ordered]@{ graphics_api = '2'; use_vsync = 'false'; shaders_accurate_mul = 'false'; async_shader_compilation = 'true'; use_disk_shader_cache = 'true'; use_hw_shader = 'true' }
$layout = [ordered]@{ layout_option = '0'; aspect_ratio = '0'; screen_top_stretch = 'false'; screen_bottom_stretch = 'false' }
$pattern = '^(' + (($speed.Keys + $layout.Keys | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')(\\default)?='
$kept = $kept | Where-Object { $_ -notmatch $pattern }
if ($kept -notcontains '[Renderer]') { $kept = @($kept) + '[Renderer]' }
if ($kept -notcontains '[Layout]') { $kept = @($kept) + '[Layout]' }
$out = New-Object System.Collections.ArrayList
foreach ($l in $kept) {
    [void]$out.Add($l)
    if ($l -eq '[Controls]') { foreach ($b in $block) { [void]$out.Add($b) } }
    if ($l -eq '[Renderer]') {
        foreach ($k in $speed.Keys) { [void]$out.Add("$k\default=false"); [void]$out.Add("$k=$($speed[$k])") }
    }
    if ($l -eq '[Layout]') {
        foreach ($k in $layout.Keys) { [void]$out.Add("$k\default=false"); [void]$out.Add("$k=$($layout[$k])") }
    }
}
[IO.File]::WriteAllLines($cfg, [string[]]$out.ToArray())
Write-Output "Azahar is ready in $dest"
