; Flying Toasters 2026 - Multi-Monitor Edition - Inno Setup script
; --------------------------------------------------------------------
; This script expects to be invoked from a staging directory that
; already contains:
;   flyingtoasters2026.scr
;   flyingtoasters2026_engine.scr
;   README.md, LICENSE.txt, CREDITS.txt
;   source\ (wrapper.c, wrapper.rc, app.manifest)
; package.ps1 produces such a staging dir automatically.

#define MyAppName        "Flying Toasters 2026 Multi-Monitor"
#define MyAppShortName   "FlyingToasters2026-MultiMonitor"
#define MyAppVersion     "1.0.0"
#define MyAppPublisher   "chaoticgeniu5 - https://jgz.sh"
#define MyAppURL         "https://github.com/chaoticgeniu5/flying-toasters-2026-multimonitor"
#define MyAppExeName     "flyingtoasters2026.scr"
#define MyAppEngineName  "flyingtoasters2026_engine.scr"

[Setup]
AppId={{A4E2B19F-8C7D-4D6E-B8A3-2F5C1A9E7B40}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={commonpf32}\Flying Toasters 2026
DefaultGroupName=Flying Toasters 2026
DisableProgramGroupPage=yes
DisableDirPage=auto
LicenseFile=LICENSE.txt
InfoAfterFile=README.md
OutputBaseFilename={#MyAppShortName}-{#MyAppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion=1.0.0.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Multi-monitor wrapper for the Flying Toasters screensaver
VersionInfoProductName={#MyAppName}
CloseApplications=force
RestartApplications=no
MinVersion=10.0.10240
ShowLanguageDialog=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "setactive";   Description: "Set as the current user's active screensaver"; GroupDescription: "Options:"; Flags: checkedonce
Name: "registersys"; Description: "Register in C:\Windows\System32 (required for the screensaver to appear in the Settings dropdown)"; GroupDescription: "Options:"; Flags: checkedonce

[Files]
Source: "{#MyAppExeName}";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppEngineName}";     DestDir: "{app}"; Flags: ignoreversion
Source: "README.md";              DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "LICENSE.txt";            DestDir: "{app}"; Flags: ignoreversion
Source: "CREDITS.txt";            DestDir: "{app}"; Flags: ignoreversion
Source: "source\*";               DestDir: "{app}\source"; Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#MyAppExeName}";        DestDir: "{sys}"; Flags: ignoreversion; Tasks: registersys
Source: "{#MyAppEngineName}";     DestDir: "{sys}"; Flags: ignoreversion; Tasks: registersys

[Registry]
Root: HKCU; Subkey: "Control Panel\Desktop"; ValueType: string; ValueName: "SCRNSAVE.EXE";    ValueData: "{sys}\{#MyAppExeName}"; Tasks: setactive
Root: HKCU; Subkey: "Control Panel\Desktop"; ValueType: string; ValueName: "ScreenSaveActive"; ValueData: "1";                     Tasks: setactive

[Icons]
Name: "{group}\Configure screensaver";  Filename: "rundll32.exe"; Parameters: "shell32.dll,Control_RunDLL desk.cpl,,1"
Name: "{group}\Test now";               Filename: "{app}\{#MyAppExeName}"; Parameters: "/s"
Name: "{group}\Wrapper source code";    Filename: "{app}\source"
Name: "{group}\README";                 Filename: "{app}\README.md"
Name: "{group}\Project page on GitHub"; Filename: "{#MyAppURL}"
Name: "{group}\Author's blog";          Filename: "https://jgz.sh"
Name: "{group}\Uninstall";              Filename: "{uninstallexe}"

[Run]
Filename: "rundll32.exe"; Parameters: "shell32.dll,Control_RunDLL desk.cpl,,1"; Description: "Open Screen Saver settings now"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: files; Name: "{sys}\{#MyAppExeName}"
Type: files; Name: "{sys}\{#MyAppEngineName}"
Type: filesandordirs; Name: "{app}\source"

[Code]
const
  BACKUP_SUFFIX = '.original_backup';

procedure CurStepChanged(CurStep: TSetupStep);
var
  origScr, backup: String;
begin
  if CurStep = ssInstall then begin
    if IsTaskSelected('registersys') then begin
      origScr := ExpandConstant('{sys}\{#MyAppExeName}');
      backup  := origScr + BACKUP_SUFFIX;
      if FileExists(origScr) and (not FileExists(backup)) then begin
        FileCopy(origScr, backup, False);
        Log('Backed up existing screensaver to ' + backup);
      end;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  origScr, backup, hkcuVal, sysScrPath: String;
begin
  if CurUninstallStep = usPostUninstall then begin
    origScr := ExpandConstant('{sys}\{#MyAppExeName}');
    backup  := origScr + BACKUP_SUFFIX;
    if FileExists(backup) then begin
      FileCopy(backup, origScr, False);
      DeleteFile(backup);
      Log('Restored original screensaver from backup');
    end;

    sysScrPath := ExpandConstant('{sys}\{#MyAppExeName}');
    if RegQueryStringValue(HKCU, 'Control Panel\Desktop', 'SCRNSAVE.EXE', hkcuVal) then begin
      if CompareText(hkcuVal, sysScrPath) = 0 then begin
        if not FileExists(sysScrPath) then begin
          RegDeleteValue(HKCU, 'Control Panel\Desktop', 'SCRNSAVE.EXE');
          RegWriteStringValue(HKCU, 'Control Panel\Desktop', 'ScreenSaveActive', '0');
        end;
      end;
    end;
  end;
end;
