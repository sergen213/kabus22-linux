<#
Imports the original game data from your own Kabus 22 copy into the "game" folder beside kabus22.exe (docs/WINDOWS.md).
Without -Source it asks for the ISO image, the original setup .exe, or an installed game folder / the inserted disc.
prepare-data.ps1 does the extraction into game.new, which replaces game only when it succeeded.

  powershell -ExecutionPolicy Bypass -File import-data.ps1 [-Source "D:\...\Kabus 22.iso"]
#>
param([string] $Source)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$title = 'Kabus 22 - import game data'
$game = Join-Path $PSScriptRoot 'game'
$staging = Join-Path $PSScriptRoot 'game.new'
if (-not $Source) {
  $choice = [Windows.Forms.MessageBox]::Show("Kabus 22 needs the game data from your own copy of the game.`n`nYes: choose the ISO image or the original setup .exe`nNo: choose an installed Kabus 22 folder or the inserted disc", $title, 'YesNoCancel', 'Question')
  if ($choice -eq 'Yes') {
    $dialog = New-Object Windows.Forms.OpenFileDialog
    $dialog.Title = 'Kabus 22 ISO image or setup .exe'
    $dialog.Filter = 'Kabus 22 ISO or setup (*.iso;*.exe)|*.iso;*.exe'
    if ($dialog.ShowDialog() -ne 'OK') { exit 1 }
    $Source = $dialog.FileName
  } elseif ($choice -eq 'No') {
    $dialog = New-Object Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Installed Kabus 22 folder (with K22.exe) or the Kabus 22 disc'
    if ($dialog.ShowDialog() -ne 'OK') { exit 1 }
    $Source = $dialog.SelectedPath
  } else { exit 1 }
}
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
try {
  & (Join-Path $PSScriptRoot 'prepare-data.ps1') -Source $Source -Output $staging
} catch {
  if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
  Write-Host "kabus22: $($_.Exception.Message)"
  [Windows.Forms.MessageBox]::Show("The game data could not be imported:`n`n$($_.Exception.Message)", $title, 'OK', 'Error') | Out-Null
  exit 1
}
if (Test-Path -LiteralPath $game) { Remove-Item -LiteralPath $game -Recurse -Force }
Rename-Item -LiteralPath $staging -NewName 'game'
[Windows.Forms.MessageBox]::Show("The game data is ready in`n$game", $title, 'OK', 'Information') | Out-Null
