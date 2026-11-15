<#
Installs and starts the Xbox package (windows/package-uwp.ps1 output) on a Dev Mode console through Windows Device Portal
(docs/XBOX.md). The console's Device Portal must be enabled with a username and password (Dev Home > Remote Access).

  powershell -ExecutionPolicy Bypass -File windows\deploy-xbox.ps1 -Console 192.168.2.28 -Package D:/kabus22/xbox-work/package
  ... asks for the Device Portal username and password; pass -Credential (Get-Credential NAME) to supply them from a script.
  ... without -Package   starts the installed game again.
  ... -EngineArgs "--language en"   writes LocalState/args.txt (extra engine arguments, space-separated, no spaces inside
                                    one argument) before starting; -EngineArgs "" clears it.
  ... -Log               prints LocalState/kabus22.log (the engine's stdout/stderr) and does nothing else.
  ... -Screenshot FILE   saves the console's current screen as PNG and does nothing else.
The password never appears on a command line: curl reads it from a config file only the current user can open, and that
file, the session cookie jar and the response file are deleted when the script ends.
#>
param(
  [Parameter(Mandatory)] [string] $Console,
  [Parameter(Mandatory)] [System.Management.Automation.PSCredential] $Credential,
  [string] $Package,
  [string] $EngineArgs,
  [switch] $NoLaunch,
  [switch] $Log,
  [string] $Screenshot
)
$ErrorActionPreference = 'Stop'
$base = "https://${Console}:11443"
# A private file: inheritance removed, full control for the current user only (set before any secret is written).
function New-PrivateFile {
  $path = (New-TemporaryFile).FullName
  & icacls.exe $path /inheritance:r /grant:r "*$([Security.Principal.WindowsIdentity]::GetCurrent().User.Value):F" | Out-Null
  if ($LASTEXITCODE -ne 0) { Remove-Item -LiteralPath $path -Force; throw "icacls failed to restrict $path (exit $LASTEXITCODE)" }
  $path
}
$jar = $null; $body = $null; $config = $null
try {
$jar = New-PrivateFile
$body = New-PrivateFile
$config = New-PrivateFile
# curl config syntax: a double-quoted value takes backslash escapes.
$network = $Credential.GetNetworkCredential() # UserName without the "\" Get-Credential may prefix
$secret = "$($network.UserName):$($network.Password)"
$quoted = $secret.Replace('\', '\\').Replace('"', '\"').Replace("`r", '\r').Replace("`n", '\n').Replace("`t", '\t')
[IO.File]::WriteAllText($config, "user = `"$quoted`"`n")
Remove-Variable network, secret, quoted
function Invoke-Wdp([string] $method, [string] $path, [string[]] $extra = @()) {
  if ($method -ne 'GET' -and $extra -notcontains '-F' -and $extra -notcontains '--data-binary') { $extra += @('-H', 'Content-Length: 0') } # WDP answers 411 otherwise
  $token = (Get-Content $jar -ErrorAction SilentlyContinue | Where-Object { $_ -match "`tCSRF-Token`t" } | ForEach-Object { ($_ -split "`t")[-1] }) | Select-Object -Last 1
  Clear-Content -LiteralPath $body
  $curlArgs = @('-sk', '-K', $config, '-b', $jar, '-c', $jar, '-X', $method, '-o', $body, '-w', '%{http_code}', '-H', "X-CSRF-Token: $token") + $extra + @("$base$path")
  $code = & curl.exe @curlArgs
  if ($LASTEXITCODE -ne 0) { throw "Device Portal $method $path failed (curl exit $LASTEXITCODE)" }
  [pscustomobject]@{ Code = [int]$code; Body = (Get-Content -Raw $body -ErrorAction SilentlyContinue) }
}
function B64([string] $s) { [Uri]::EscapeDataString([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($s))) }
function Find-Kabus { (Invoke-Wdp GET '/api/app/packagemanager/packages').Body | ConvertFrom-Json | Select-Object -ExpandProperty InstalledPackages | Where-Object { $_.PackageFullName -like 'Kabus22.Native_*' } | Select-Object -First 1 }
function LocalState([string] $query) { "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$([Uri]::EscapeDataString($installed.PackageFullName))&path=$([Uri]::EscapeDataString('\LocalState'))$query" }

if ((Invoke-Wdp GET '/api/os/info').Code -ne 200) { throw 'Device Portal rejected the credentials or is unreachable' }
$installed = Find-Kabus
if ($Log) {
  if (-not $installed) { throw 'Kabus 22 is not installed' }
  $r = Invoke-Wdp GET (LocalState '&filename=kabus22.log')
  if ($r.Code -ne 200) { throw "No kabus22.log yet (HTTP $($r.Code))" }
  $r.Body; return
}
if ($Screenshot) {
  & curl.exe -sk -K $config -o $Screenshot -w '%{http_code}' "$base/ext/screenshot" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "Screenshot failed (curl exit $LASTEXITCODE)" }
  Write-Host "kabus22: screenshot $Screenshot"; return
}

function Start-Kabus {
  $r = Invoke-Wdp POST ("/api/taskmanager/app?appid=$(B64 $installed.PackageRelativeId)&package=$(B64 $installed.PackageFullName)")
  if ($r.Code -ne 200) { throw "Launch failed: HTTP $($r.Code) $($r.Body)" }
}
function Stop-Kabus { Invoke-Wdp DELETE "/api/taskmanager/app?package=$(B64 $installed.PackageFullName)" | Out-Null }
if ($Package) {
  # Game mode (about 5 GB instead of 1 GB, exclusive CPU cores, full GPU): the console types newly deployed UWP packages
  # as Game when DefaultUWPContentTypeToGame is on (the same as Dev Home > View details > App type: Game).
  $setting = ((Invoke-Wdp GET '/ext/settings').Body | ConvertFrom-Json).Settings | Where-Object { $_.Name -eq 'DefaultUWPContentTypeToGame' }
  if ($setting -and $setting.Value -ne 'true') {
    $json = Join-Path $env:TEMP 'kabus22-setting.json'
    [IO.File]::WriteAllText($json, '{"Value":"True"}')
    $r = Invoke-Wdp PUT '/ext/settings/DefaultUWPContentTypeToGame' @('-H', 'Content-Type: application/json', '--data-binary', "@$json")
    if ($r.Code -ne 200) { throw "Setting DefaultUWPContentTypeToGame failed: HTTP $($r.Code) $($r.Body)" }
    Write-Host 'kabus22: console now deploys UWP packages as Game (DefaultUWPContentTypeToGame)'
  }
  $msix = Join-Path $Package 'Kabus22.msix'
  $cer = Join-Path $Package 'Kabus22-dev.cer'
  $deps = Get-Content (Join-Path $Package 'dependencies.txt')
  # No uninstall: a higher package version (package-uwp.ps1 revision) installs over the old one and keeps LocalState.
  if ($installed) { Stop-Kabus }
  Write-Host "kabus22: uploading $msix ($([Math]::Round((Get-Item $msix).Length / 1MB)) MB)"
  $form = @('-F', "Kabus22.msix=@$msix", '-F', "Kabus22-dev.cer=@$cer")
  foreach ($dep in $deps) { $form += '-F'; $form += "$(Split-Path -Leaf $dep)=@$dep" }
  $r = Invoke-Wdp POST '/api/app/packagemanager/package?package=Kabus22.msix' $form
  if ($r.Code -ne 200 -and $r.Code -ne 202) { throw "Upload failed: HTTP $($r.Code) $($r.Body)" }
  do {
    Start-Sleep -Seconds 3
    $state = Invoke-Wdp GET '/api/app/packagemanager/state'
  } while ($state.Code -eq 204)
  if ($state.Code -eq 200) {
    $result = $state.Body | ConvertFrom-Json
    if ($result.PSObject.Properties['Success'] -and -not $result.Success) { throw "Install failed: $($state.Body)" }
  }
  $installed = Find-Kabus
  if (-not $installed) { throw "Install did not register the package: HTTP $($state.Code) $($state.Body)" }
  Write-Host "kabus22: installed $($installed.PackageFullName)"
}
if (-not $installed) { throw 'Kabus 22 is not installed; pass -Package' }

if ($PSBoundParameters.ContainsKey('EngineArgs')) {
  $file = Join-Path $env:TEMP 'args.txt'
  [IO.File]::WriteAllText($file, (($EngineArgs -split '\s+' | Where-Object { $_ } | ForEach-Object { "$_`n" }) -join ''))
  $listing = "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=$([Uri]::EscapeDataString($installed.PackageFullName))"
  if (((Invoke-Wdp GET $listing).Body | ConvertFrom-Json).Items.Name -notcontains 'LocalState') {
    # A fresh install has no app data folders (WDP cannot create them) until the app first runs.
    Start-Kabus
    for ($i = 0; $i -lt 30 -and ((Invoke-Wdp GET $listing).Body | ConvertFrom-Json).Items.Name -notcontains 'LocalState'; ++$i) { Start-Sleep -Seconds 1 }
    Stop-Kabus
    Start-Sleep -Seconds 2
  }
  $r = Invoke-Wdp POST (LocalState '') @('-F', "args.txt=@$file")
  if ($r.Code -ne 200) { throw "Writing args.txt failed: HTTP $($r.Code) $($r.Body)" }
  Write-Host "kabus22: engine arguments: $($EngineArgs -join ' ')"
}
if (-not $NoLaunch) {
  Stop-Kabus
  Start-Kabus
  Write-Host 'kabus22: started'
}
} finally {
  foreach ($temporary in $config, $jar, $body) { if ($temporary) { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue } }
}
