<#
Extracts the original Kabus 22 game files from your own ISO into a data folder for the Windows and Xbox builds
(docs/XBOX.md). The file selection is the native installer's (installer/media.cpp `selected`): app/*.k22|*.wrs|*.dat,
help/*.jpg|*.html, gallery/*.jpg, menu_sound/*.ogg, Kabus 22 Mode/menu_ses/*.ogg. K22.exe is copied as data only:
the engine reads its embedded _a4font PCX (assets.cpp `image_ptr`) and never executes it.

  powershell -ExecutionPolicy Bypass -File windows\prepare-data.ps1 -Iso "D:\...\Kabus 22.iso" -Output D:\kabus22\xbox-work\game
#>
param(
  [Parameter(Mandatory)] [string] $Iso,
  [Parameter(Mandatory)] [string] $Output,
  [string] $Work = (Join-Path $env:TEMP 'kabus22-prepare')
)
$ErrorActionPreference = 'Stop'
$innoUrl = 'https://github.com/dscharrer/innoextract/releases/download/1.9/innoextract-1.9-windows.zip'
$innoSha256 = '6989342c9b026a00a72a38f23b62a8e6a22cc5de69805cf47d68ac2fec993065'

if (Test-Path $Output) { throw "Output folder already exists: $Output" }
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$zip = Join-Path $Work 'innoextract-1.9-windows.zip'
$inno = Join-Path $Work 'innoextract\innoextract.exe'
if (-not (Test-Path $inno)) {
  Invoke-WebRequest -Uri $innoUrl -OutFile $zip -UseBasicParsing
  if ((Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant() -ne $innoSha256) { throw 'innoextract download does not match its pinned SHA-256' }
  Expand-Archive -Path $zip -DestinationPath (Join-Path $Work 'innoextract') -Force
}

$image = Mount-DiskImage -ImagePath (Resolve-Path $Iso).Path -PassThru
try {
  $drive = ($image | Get-Volume).DriveLetter + ':\'
  $setups = @(Get-ChildItem -Path $drive -Filter '*.exe' -File | Where-Object { Test-Path (Join-Path $drive ($_.BaseName + '-1.bin')) })
  if ($setups.Count -ne 1) { throw "The ISO must contain exactly one Inno setup with -1.bin slices, found $($setups.Count)" }
  $raw = Join-Path $Work 'raw'
  if (Test-Path $raw) { Remove-Item -Recurse -Force $raw }
  # Language variants of unselected programs (Kabus22.exe) share one path; selected members never collide.
  & $inno --extract --silent --collisions=rename --output-dir $raw $setups[0].FullName
  if ($LASTEXITCODE -ne 0) { throw "innoextract failed with exit code $LASTEXITCODE" }
} finally {
  Dismount-DiskImage -ImagePath $image.ImagePath | Out-Null
}

$app = Join-Path $raw 'app'
$copied = 0
foreach ($file in Get-ChildItem -Path $app -Recurse -File) {
  $relative = $file.FullName.Substring($app.Length + 1)
  $lower = $relative.ToLowerInvariant().Replace('\', '/')
  $ext = $file.Extension.ToLowerInvariant()
  if ($lower.StartsWith('help/')) { $selected = $ext -in '.jpg', '.html' }
  elseif ($lower.StartsWith('gallery/')) { $selected = $ext -eq '.jpg' }
  elseif ($lower.StartsWith('menu_sound/') -or $lower.StartsWith('kabus 22 mode/menu_ses/')) { $selected = $ext -eq '.ogg' }
  else { $selected = $ext -in '.k22', '.wrs', '.dat' }
  if (-not $selected) { continue }
  if ($relative.Contains('$')) { throw "Colliding original member: $relative" }
  $target = Join-Path $Output $relative
  New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
  Copy-Item -LiteralPath $file.FullName -Destination $target
  $copied++
}
$exe = Join-Path $app 'K22.exe'
if (-not (Test-Path $exe)) { throw 'The original media lacks app\K22.exe, the source of the engine font' }
Copy-Item -LiteralPath $exe -Destination (Join-Path $Output 'K22.exe')
$mode = Join-Path $Output 'Kabus 22 Mode'
if (Test-Path $mode) { Copy-Item -LiteralPath $exe -Destination (Join-Path $mode 'K22.exe') }
Remove-Item -Recurse -Force $raw
Write-Host "kabus22: $copied game files + K22.exe (font source) in $Output"
