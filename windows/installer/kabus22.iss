; Kabus22 Windows setup (docs/WINDOWS.md), built by windows/package-windows.ps1 from the `cmake --install` folder:
;   ISCC /DAppVersion=0.4.18 /DAppNumericVersion=0.4.18 /DStage=<install folder> /DOutputDir=<dir> windows\installer\kabus22.iss
; Per-user install without administrator rights. The game data is never bundled: setup extracts it from the player's own
; ISO, setup .exe or installed game folder with prepare-data.ps1 into {app}\game, or leaves that for the Start menu entry
; "Import Kabus 22 game data". Saves and settings live in the engine's data root %LOCALAPPDATA%\Kabus22 (native/platform.cpp
; user_data_root); setup refuses to install there and the uninstaller never touches it.
#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or newer is required (ExecAndLogOutput)
#endif
#ifndef AppVersion
  #error Define AppVersion, AppNumericVersion, Stage and OutputDir (windows/package-windows.ps1)
#endif

[Setup]
AppId={{DC94AFFE-1A07-4D24-9C44-B243AA85D6BD}
AppName=Kabus 22
AppVersion={#AppVersion}
AppVerName=Kabus 22 {#AppVersion}
AppPublisher=Kabus22 native port
AppPublisherURL=https://github.com/sergen213/kabus22-linux
AppSupportURL=https://github.com/sergen213/kabus22-linux/issues
VersionInfoVersion={#AppNumericVersion}
PrivilegesRequired=lowest
DefaultDirName={autopf}\Kabus22
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=Kabus22-{#AppVersion}-Windows-Setup-x64
UninstallDisplayIcon={app}\kabus22.exe
UninstallDisplayName=Kabus 22 (native engine)
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\prepare-data.ps1"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\Kabus 22"; Filename: "{app}\Kabus22.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\kabus22.exe"
Name: "{autoprograms}\Import Kabus 22 game data"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\import-data.ps1"""; WorkingDir: "{app}"; IconFilename: "{app}\kabus22.exe"
Name: "{autodesktop}\Kabus 22"; Filename: "{app}\Kabus22.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\kabus22.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Kabus22.cmd"; WorkingDir: "{app}"; Description: "Start Kabus 22"; Flags: postinstall nowait skipifsilent shellexec; Check: GameDataReady

[UninstallDelete]
; Extracted from the player's own copy by setup or import-data.ps1, so not in the uninstall log.
Type: filesandordirs; Name: "{app}\game"
Type: filesandordirs; Name: "{app}\game.new"

[Code]
var
  SourcePage: TInputOptionWizardPage;
  MediaPage: TInputFileWizardPage;
  FolderPage: TInputDirWizardPage;
  ProgressPage: TOutputMarqueeProgressWizardPage;
  DataError: String;

function DataRoot(): String;
begin
  Result := ExpandConstant('{localappdata}\Kabus22');
end;

function GameDataReady(): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\game\K22.exe'));
end;

procedure InitializeWizard();
begin
  SourcePage := CreateInputOptionPage(wpSelectTasks, 'Game data', 'Where should Setup take the Kabus 22 game data from?',
    'Kabus 22 is not included. Setup copies the game files (about 2.2 GB) from your own copy of the game; nothing from it is run. ' +
    'Reading an ISO or setup .exe downloads the innoextract 1.9 tool once.', True, False);
  SourcePage.Add('From the ISO image or the original setup .exe (with its setup-1.bin)');
  SourcePage.Add('From an installed Kabus 22 folder or the Kabus 22 disc');
  SourcePage.Add('Later, with "Import Kabus 22 game data" in the Start menu');
  // Unattended (/SILENT, /VERYSILENT) installs leave the game data to the Start menu entry.
  if WizardSilent() then SourcePage.SelectedValueIndex := 2 else SourcePage.SelectedValueIndex := 0;
  MediaPage := CreateInputFilePage(SourcePage.ID, 'Game data', 'Select your Kabus 22 ISO image or the original setup .exe.',
    'A setup .exe needs its setup-1.bin in the same folder.');
  MediaPage.Add('&ISO image or setup .exe:', 'Kabus 22 ISO image or setup (*.iso;*.exe)|*.iso;*.exe|All files|*.*', '.iso');
  FolderPage := CreateInputDirPage(MediaPage.ID, 'Game data', 'Select your installed Kabus 22 folder or the Kabus 22 disc.',
    'An installed game folder holds K22.exe; a disc or opened ISO holds the setup .exe and setup-1.bin.', False, '');
  FolderPage.Add('&Folder or disc:');
  ProgressPage := CreateOutputMarqueeProgressPage('Game data', 'Setup is copying the Kabus 22 game data from your copy.');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = SourcePage.ID then
    if GameDataReady() then
      SourcePage.CheckListBox.ItemCaption[2] := 'Keep the game data already installed'
    else
      SourcePage.CheckListBox.ItemCaption[2] := 'Later, with "Import Kabus 22 game data" in the Start menu';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := ((PageID = MediaPage.ID) and (SourcePage.SelectedValueIndex <> 0)) or
            ((PageID = FolderPage.ID) and (SourcePage.SelectedValueIndex <> 1));
end;

function Inside(const Path, Root: String): Boolean;
var
  P, R: String;
begin
  P := Lowercase(AddBackslash(Path));
  R := Lowercase(AddBackslash(Root));
  Result := Copy(P, 1, Length(R)) = R;
end;

// A PowerShell single-quoted string literal.
function PsQuote(S: String): String;
begin
  StringChangeEx(S, '''', '''''', True);
  Result := '''' + S + '''';
end;

procedure DataLog(const S: String; const Error, FirstLine: Boolean);
begin
  if Copy(S, 1, 15) = 'kabus22-error: ' then
    DataError := Copy(S, 16, MaxInt)
  else if Trim(S) <> '' then
    ProgressPage.SetText('Setup is copying the Kabus 22 game data from your copy.', S);
  ProgressPage.Animate;
end;

// Copies the game data when the Ready page is confirmed, before any file is installed: a failure leaves nothing behind and
// the player stays on the Ready page, free to go back and choose another source. Returns the error, empty on success.
function ImportGameData(): String;
var
  Source, Game, Staging, Command: String;
  AppExisted: Boolean;
  ResultCode: Integer;
begin
  Result := '';
  if SourcePage.SelectedValueIndex = 2 then Exit;
  if SourcePage.SelectedValueIndex = 0 then Source := MediaPage.Values[0] else Source := RemoveBackslash(FolderPage.Values[0]);
  // A drive root without its backslash ("D:") would mean that drive's current directory.
  if (Length(Source) = 2) and (Source[2] = ':') then Source := Source + '\.';
  Game := ExpandConstant('{app}\game');
  Staging := Game + '.new';
  AppExisted := DirExists(ExpandConstant('{app}'));
  if DirExists(Staging) then DelTree(Staging, True, True, True);
  ExtractTemporaryFile('prepare-data.ps1');
  Command := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& { try { & ' + PsQuote(ExpandConstant('{tmp}\prepare-data.ps1')) +
    ' -Source ' + PsQuote(Source) + ' -Output ' + PsQuote(Staging) +
    ' } catch { Write-Output (''kabus22-error: '' + $_.Exception.Message); exit 1 } }"';
  DataError := '';
  ProgressPage.Show;
  try
    if not ExecAndLogOutput(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Command, ExpandConstant('{tmp}'),
        SW_HIDE, ewWaitUntilTerminated, ResultCode, @DataLog) then begin
      DataError := SysErrorMessage(ResultCode);
      ResultCode := -1;
    end;
  finally
    ProgressPage.Hide;
  end;
  if (ResultCode = 0) and FileExists(Staging + '\K22.exe') then begin
    if DirExists(Game) and not DelTree(Game, True, True, True) then
      Result := 'The previous game data in ' + Game + ' could not be removed.'
    else if not RenameFile(Staging, Game) then
      Result := 'The game data could not be moved to ' + Game + '.';
  end else begin
    if DataError = '' then DataError := 'prepare-data.ps1 exited with code ' + IntToStr(ResultCode) + '.';
    Result := 'The game data could not be copied from ' + Source + ':' + #13#10#13#10 + DataError + #13#10#13#10 +
      'Go back to choose another source, or choose "Later" to install without game data.';
  end;
  if Result <> '' then begin
    DelTree(Staging, True, True, True);
    if not AppExisted then RemoveDir(ExpandConstant('{app}'));
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Failure: String;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and Inside(WizardDirValue(), DataRoot()) then begin
    MsgBox('Kabus 22 keeps your saves and settings in ' + DataRoot() + '. Choose a different installation folder.', mbError, MB_OK);
    Result := False;
  end else if (CurPageID = MediaPage.ID) and not FileExists(MediaPage.Values[0]) then begin
    MsgBox('Select an existing ISO image or setup .exe.', mbError, MB_OK);
    Result := False;
  end else if (CurPageID = FolderPage.ID) and not DirExists(FolderPage.Values[0]) then begin
    MsgBox('Select an existing folder or disc.', mbError, MB_OK);
    Result := False;
  end else if CurPageID = wpReady then begin
    Failure := ImportGameData();
    if Failure <> '' then begin
      SuppressibleMsgBox(Failure, mbError, MB_OK, IDOK);
      Result := False;
    end;
  end;
end;
