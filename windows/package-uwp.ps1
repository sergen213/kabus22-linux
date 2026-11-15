<#
Builds the signed Xbox UWP package (docs/XBOX.md) from a UWP build directory and a prepared game data folder
(windows/prepare-data.ps1). The game data is bundled under <package>/game (your own copy, for private sideloading only).

  powershell -ExecutionPolicy Bypass -File windows\package-uwp.ps1 -Build D:/kabus22/xbox-work/build/uwp -Game D:/kabus22/xbox-work/game -Out D:/kabus22/xbox-work/package
Output: <Out>/Kabus22.msix, <Out>/Kabus22-dev.cer (the signing certificate), and the VCLibs dependency path in <Out>/dependencies.txt.
#>
param(
  [Parameter(Mandatory)] [string] $Build,
  [Parameter(Mandatory)] [string] $Game,
  [Parameter(Mandatory)] [string] $Out,
  [string] $Publisher = 'CN=Kabus22 Dev',
  [switch] $NoGame
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repo = Split-Path -Parent $PSScriptRoot
$sdkBin = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'x64\makeappx.exe') } | Sort-Object Name | Select-Object -Last 1
if (-not $sdkBin) { throw 'Windows SDK makeappx.exe not found' }
$makeappx = Join-Path $sdkBin.FullName 'x64\makeappx.exe'
$signtool = Join-Path $sdkBin.FullName 'x64\signtool.exe'
$vclibs = 'C:\Program Files (x86)\Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64\Microsoft.VCLibs.x64.14.00.appx'
if (-not (Test-Path $vclibs)) { throw "VCLibs framework package missing (Visual Studio UWP workload): $vclibs" }

$exe = Join-Path $Build 'kabus22.exe'
if (-not (Test-Path $exe)) { throw "No UWP kabus22.exe in $Build" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
# Every package gets a higher fourth version field, so deploy-xbox.ps1 updates in place and the console keeps LocalState
# (saves, launcher.ini, controller.ini); reinstalling the same version would need an uninstall, which deletes them. The counter
# lives in the build directory, so full and -NoGame packages of one build never go backwards.
$revisionFile = Join-Path $Build 'package-revision.txt'
$revision = 1 + [int](Get-Content $revisionFile -ErrorAction SilentlyContinue | Select-Object -First 1)
if ($revision -gt 65535) { throw "Package revision exceeds 65535; delete $revisionFile after uninstalling the game" }
Set-Content -Path $revisionFile -Value $revision
$version = (Select-String -Path (Join-Path $repo 'CMakeLists.txt') -Pattern 'project\(kabus22_native VERSION ([0-9.]+)').Matches[0].Groups[1].Value + ".$revision"
$layout = Join-Path $Out 'layout'
if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
New-Item -ItemType Directory -Path (Join-Path $layout 'Assets') | Out-Null
Copy-Item $exe $layout
Get-ChildItem -Path $Build -Filter '*.dll' -File | Copy-Item -Destination $layout
(Get-Content -Raw (Join-Path $PSScriptRoot 'uwp\AppxManifest.xml.in')).Replace('@VERSION@', $version).Replace('@PUBLISHER@', $Publisher) |
  Set-Content -Encoding UTF8 (Join-Path $layout 'AppxManifest.xml')

# Tiles: the game's own title text on black (no artwork is stored in the repository).
function New-Tile([string] $name, [int] $width, [int] $height) {
  $bitmap = New-Object System.Drawing.Bitmap $width, $height
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $graphics.Clear([System.Drawing.Color]::Black)
  $font = New-Object System.Drawing.Font 'Times New Roman', ([Math]::Max(6, [Math]::Min($width / 5.5, $height / 2.6))), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
  $format = New-Object System.Drawing.StringFormat
  $format.Alignment = [System.Drawing.StringAlignment]::Center; $format.LineAlignment = [System.Drawing.StringAlignment]::Center
  $graphics.DrawString('KABUS 22', $font, [System.Drawing.Brushes]::DarkRed, (New-Object System.Drawing.RectangleF 0, 0, $width, $height), $format)
  $bitmap.Save((Join-Path $layout "Assets\$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $graphics.Dispose(); $bitmap.Dispose()
}
New-Tile 'StoreLogo' 50 50
New-Tile 'Square44x44Logo' 44 44
New-Tile 'Square150x150Logo' 150 150
New-Tile 'Wide310x150Logo' 310 150
New-Tile 'SplashScreen' 620 300

if (-not $NoGame) {
  # Hard links: the 2 GB of data is not copied when the layout is on the game folder's volume.
  $gameRoot = (Resolve-Path $Game).Path
  foreach ($file in Get-ChildItem -Path $gameRoot -Recurse -File) {
    $target = Join-Path (Join-Path $layout 'game') $file.FullName.Substring($gameRoot.Length + 1)
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    try { New-Item -ItemType HardLink -Path $target -Target $file.FullName | Out-Null } catch { Copy-Item -LiteralPath $file.FullName -Destination $target }
  }
}

$msix = Join-Path $Out 'Kabus22.msix'
& $makeappx pack /o /h SHA256 /d $layout /p $msix | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makeappx failed with exit code $LASTEXITCODE" }

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $Publisher -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) {
  $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher -KeyUsage DigitalSignature -FriendlyName 'Kabus22 Xbox sideload' `
    -CertStoreLocation Cert:\CurrentUser\My -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}') -NotAfter (Get-Date).AddYears(5)
}
Export-Certificate -Cert $cert -FilePath (Join-Path $Out 'Kabus22-dev.cer') | Out-Null
& $signtool sign /q /fd SHA256 /sha1 $cert.Thumbprint $msix
if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
Set-Content -Path (Join-Path $Out 'dependencies.txt') -Value $vclibs
Write-Host "kabus22: $msix ($([Math]::Round((Get-Item $msix).Length / 1MB)) MB), version $version"
