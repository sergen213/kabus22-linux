<#
Extracts the original Kabus 22 game files from your own copy into a data folder for the Windows and Xbox builds
(docs/WINDOWS.md, docs/XBOX.md). -Source (alias -Iso) is one of:
  - the ISO image (mounted read-only for the extraction),
  - the original setup .exe with its -1.bin slices beside it, or a folder holding them (an opened ISO, the inserted disc),
  - an installed Windows game folder (it holds K22.exe).
The selection is the native installer's (installer/media.cpp `selected`): app/*.k22|*.wrs|*.dat, help/*.jpg|*.html,
gallery/*.jpg, menu_sound/*.ogg, Kabus 22 Mode/menu_ses/*.ogg; an installed folder adds k22.ini and the community patch's
_data/ assets and never takes saves, runtime state (ftz/rms/rmo.k22), programs or uninstaller files (installer/setup.cpp
`windowsAsset`). K22.exe is copied as data only: the engine reads its embedded _a4font PCX (assets.cpp `image_ptr`) and
never executes it. Only the media extraction needs innoextract 1.9 (downloaded once, SHA-256 pinned).

  powershell -ExecutionPolicy Bypass -File windows\prepare-data.ps1 -Source "D:\...\Kabus 22.iso" -Output D:\kabus22\game
#>
param(
  [Parameter(Mandatory)] [Alias('Iso')] [string] $Source,
  [Parameter(Mandatory)] [string] $Output,
  [string] $Work = (Join-Path $env:TEMP 'kabus22-prepare')
)
$ErrorActionPreference = 'Stop'
$innoUrl = 'https://github.com/dscharrer/innoextract/releases/download/1.9/innoextract-1.9-windows.zip'
$innoSha256 = '6989342c9b026a00a72a38f23b62a8e6a22cc5de69805cf47d68ac2fec993065'

# Copies the selected original files below $Root (the media's app folder or an installed game) into $Output.
function Copy-GameFiles([string] $Root, [bool] $Installed) {
  $Root = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
  $outputFull = [IO.Path]::GetFullPath($Output).TrimEnd('\')
  if (($outputFull + '\').StartsWith($Root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "The output folder must not lie inside the game folder: $Output" }
  $dataExtensions = '.wdl', '.wrs', '.ini', '.wmb', '.mdl', '.pcx', '.bmp', '.tga', '.jpg', '.jpeg', '.png', '.dds', '.ogg', '.wav', '.mp3', '.avi', '.txt', '.fx', '.dat', '.k22', '.ttf'
  $count = 0
  foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File) {
    $relative = $file.FullName.Substring($Root.Length + 1)
    $lower = $relative.ToLowerInvariant().Replace('\', '/')
    $ext = $file.Extension.ToLowerInvariant()
    if ($Installed -and $lower.StartsWith('_data/')) { $selected = $ext -in $dataExtensions }
    elseif ($Installed -and $lower -eq 'k22.ini') { $selected = $true }
    elseif ($Installed -and ((@('ftz.k22', 'rms.k22', 'rmo.k22') -contains $lower) -or $lower.StartsWith('unins'))) { $selected = $false }
    elseif ($lower.StartsWith('help/')) { $selected = $ext -in '.jpg', '.html' }
    elseif ($lower.StartsWith('gallery/')) { $selected = $ext -eq '.jpg' }
    elseif ($lower.StartsWith('menu_sound/') -or $lower.StartsWith('kabus 22 mode/menu_ses/')) { $selected = $ext -eq '.ogg' }
    else { $selected = $ext -in '.k22', '.wrs', '.dat' }
    if (-not $selected) { continue }
    if (-not $Installed -and $relative.Contains('$')) { throw "Colliding original member: $relative" }
    $target = Join-Path $Output $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target
    $count++
    if ($count % 20 -eq 0) { Write-Host "kabus22: copied $count files" }
  }
  $exe = Join-Path $Root 'K22.exe'
  if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "$Root lacks K22.exe, the source of the engine font" }
  if ($count -eq 0) { throw "$Root holds no original Kabus 22 game files" }
  Copy-Item -LiteralPath $exe -Destination (Join-Path $Output 'K22.exe')
  $mode = Join-Path $Output 'Kabus 22 Mode'
  if (Test-Path -LiteralPath $mode) { Copy-Item -LiteralPath $exe -Destination (Join-Path $mode 'K22.exe') }
  Write-Host "kabus22: $count game files + K22.exe (font source) in $Output"
}

if (Test-Path $Output) { throw "Output folder already exists: $Output" }
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
if ((Test-Path -LiteralPath $sourcePath -PathType Container) -and (Test-Path -LiteralPath (Join-Path $sourcePath 'K22.exe') -PathType Leaf)) {
  Write-Host "kabus22: reading the installed game folder $sourcePath"
  Copy-GameFiles $sourcePath $true
  return
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$zip = Join-Path $Work 'innoextract-1.9-windows.zip'
$inno = Join-Path $Work 'innoextract\innoextract.exe'
# The zip stays cached; it is re-verified on every run and the executable re-extracted from it, so a tampered or
# corrupted cache is never run.
if (-not (Test-Path $zip)) { Invoke-WebRequest -Uri $innoUrl -OutFile $zip -UseBasicParsing }
if ((Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant() -ne $innoSha256) { Remove-Item -LiteralPath $zip -Force; throw 'innoextract download does not match its pinned SHA-256 (removed; run again to download it afresh)' }
$innoDir = Join-Path $Work 'innoextract'
if (Test-Path $innoDir) { Remove-Item -Recurse -Force $innoDir }
Expand-Archive -Path $zip -DestinationPath $innoDir -Force

# The one Inno setup .exe with its -1.bin slices in a folder (an opened ISO or the disc).
function Find-Setup([string] $Folder) {
  $setups = @(Get-ChildItem -LiteralPath $Folder -Filter '*.exe' -File | Where-Object { Test-Path -LiteralPath (Join-Path $Folder ($_.BaseName + '-1.bin')) })
  if ($setups.Count -ne 1) { throw "$Folder must hold exactly one Inno setup .exe with its -1.bin slices, or be an installed game folder with K22.exe; found $($setups.Count) setups" }
  $setups[0].FullName
}
$raw = Join-Path $Work 'raw'
if (Test-Path $raw) { Remove-Item -Recurse -Force $raw }
function Expand-Setup([string] $Setup) {
  Write-Host "kabus22: extracting $Setup with innoextract (this takes a few minutes)"
  # Language variants of unselected programs (Kabus22.exe) share one path; selected members never collide.
  # Every extracted file is listed as progress. Windows PowerShell turns a native program's stderr into errors when its own
  # output is redirected (the Windows setup reads it): only the exit code decides here.
  $ErrorActionPreference = 'Continue'
  & $inno --extract --collisions=rename --output-dir $raw $Setup 2>&1 | ForEach-Object { Write-Host "$_" }
  if ($LASTEXITCODE -ne 0) { throw "innoextract failed with exit code $LASTEXITCODE" }
}
if (Test-Path -LiteralPath $sourcePath -PathType Container) { Expand-Setup (Find-Setup $sourcePath) }
elseif ([IO.Path]::GetExtension($sourcePath) -ieq '.exe') {
  if (-not (Test-Path -LiteralPath (Join-Path (Split-Path $sourcePath) ([IO.Path]::GetFileNameWithoutExtension($sourcePath) + '-1.bin')))) { throw "The setup's -1.bin slice is missing beside $sourcePath" }
  Expand-Setup $sourcePath
} else {
  try { $image = Mount-DiskImage -ImagePath $sourcePath -PassThru }
  catch { throw "Windows could not mount $sourcePath ($($_.Exception.Message)). Open the ISO in Explorer (double-click mounts it) and choose the mounted drive instead." }
  try {
    $drive = ($image | Get-Volume).DriveLetter + ':\'
    Expand-Setup (Find-Setup $drive)
  } finally {
    Dismount-DiskImage -ImagePath $image.ImagePath | Out-Null
  }
}
Copy-GameFiles (Join-Path $raw 'app') $false
Remove-Item -Recurse -Force $raw
