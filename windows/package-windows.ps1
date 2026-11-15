<#
Packages a Windows desktop build (docs/WINDOWS.md) as the portable zip kabus22-<version>-windows-x64.zip and, with -Iscc,
the setup Kabus22-<version>-Windows-Setup-x64.exe (windows/installer/kabus22.iss), both from the same `cmake --install`
folder. Neither ever contains game data.

  powershell -ExecutionPolicy Bypass -File windows\package-windows.ps1 -Build build\win64 -Out dist [-Version 0.4.18-rc.1]
    [-Iscc "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"]
#>
param(
  [Parameter(Mandatory)] [string] $Build,
  [Parameter(Mandatory)] [string] $Out,
  [string] $Version,
  [string] $Iscc
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = (Select-String -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Pattern '^project\(kabus22_native VERSION ([0-9]+\.[0-9]+\.[0-9]+) ').Matches[0].Groups[1].Value
if (-not $Version) { $Version = $project }
$Version = $Version -replace '^v', ''  # a release tag vX.Y.Z[-pre]
if (($Version -split '[-+]')[0] -ne $project) { throw "Version $Version does not match the CMake project version $project" }

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
$stage = Join-Path $Out 'Kabus22'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
# Only the shipped component (cmake/KabusWindows.cmake); ODE's development files are a separate component.
& cmake --install $Build --prefix $stage --component Kabus22
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed with exit code $LASTEXITCODE" }

# The folder must run on a clean Windows: the engine, its vcpkg DLLs and the app-local MSVC runtime.
foreach ($pattern in 'kabus22.exe', 'SDL2.dll', 'freetype*.dll', 'avcodec-*.dll', 'avformat-*.dll', 'avutil-*.dll', 'swresample-*.dll', 'swscale-*.dll', 'epoxy*.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'Kabus22.cmd', 'import-data.ps1', 'prepare-data.ps1', 'README.md', 'licenses\kabus22-LICENSE.txt', 'licenses\ode-LICENSE-BSD.TXT', 'licenses\vcpkg-ports.txt', 'licenses\vcpkg\ffmpeg.txt', 'licenses\vcpkg\sdl2.txt', 'licenses\vcpkg\freetype.txt', 'licenses\vcpkg\libepoxy.txt') {
  if (-not (Get-ChildItem -Path (Join-Path $stage $pattern) -ErrorAction SilentlyContinue)) { throw "The staged folder lacks $pattern" }
}
# Nothing else ships (no headers, import libraries, CMake files, PDBs and never game data): top level only the engine, DLLs and
# scripts; licences only as text files below licenses\.
$allowedTop = 'kabus22.exe', 'Kabus22.cmd', 'import-data.ps1', 'prepare-data.ps1', 'README.md'
foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
  $relative = $file.FullName.Substring($stage.Length + 1)
  $ok = if ($relative.Contains('\')) { $relative -match '^licenses\\(vcpkg\\)?[^\\]+\.(txt|TXT)$' } else { ($allowedTop -contains $relative) -or ($file.Extension -ieq '.dll') }
  if (-not $ok -or $file.Name -ieq 'K22.exe') { throw "Unexpected file in the staged folder (not shipped): $relative" }
}

$zip = Join-Path $Out "kabus22-$Version-windows-x64.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
Write-Host "kabus22: $zip"

if ($Iscc) {
  & $Iscc /Qp "/DAppVersion=$Version" "/DAppNumericVersion=$project" "/DStage=$stage" "/DOutputDir=$Out" (Join-Path $PSScriptRoot 'installer\kabus22.iss')
  if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
  Write-Host "kabus22: $(Join-Path $Out "Kabus22-$Version-Windows-Setup-x64.exe")"
}
