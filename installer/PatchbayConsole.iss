; The Patchbay Console installer (Inno Setup 6). Built by tools\build-installer.ps1, which passes
; AppVersion, Source (the Release artefacts folder), Icon and VCRedist (vc_redist.x64.exe of the
; toolset that built the exe) with its version.

#ifndef AppVersion
  #error Run tools\build-installer.ps1 (it passes the version and the build folders)
#endif
#define AppName "Patchbay Console"
#define AppExe "Patchbay Console.exe"

[Setup]
; (AppId never changes: it's how an update finds the copy already installed)
AppId={{A5EEC64C-3FD6-4D57-9FE8-768530A5E3CF}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Patchbay
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile={#Icon}
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=..\build\installer
OutputBaseFilename=PatchbayConsole-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
LicenseFile=..\LICENSE

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Source}\*"; Excludes: "*.pdb"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\docs\CREDITS.md"; DestDir: "{app}\licences"; Flags: ignoreversion
Source: "..\third_party\rnnoise\COPYING"; DestDir: "{app}\licences"; DestName: "rnnoise.txt"; Flags: ignoreversion
Source: "..\third_party\openvr\LICENSE"; DestDir: "{app}\licences"; DestName: "openvr.txt"; Flags: ignoreversion
Source: "{#VCRedist}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Check: VCRedistNeeded
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser
Filename: "https://vb-audio.com/Cable/"; Description: "Get VB-CABLE (vb-audio.com), the virtual cable Patchbay Console feeds your mic through"; Flags: shellexec nowait postinstall skipifsilent runasoriginaluser; Check: not HasVbCable

[Code]
// The runtime the exe was built against (or newer) already installed?
function VCRedistNeeded: Boolean;
var
  Major, Minor, Bld: Cardinal;
begin
  Result := True;
  if RegQueryDWordValue (HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Major', Major)
     and RegQueryDWordValue (HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Minor', Minor)
     and RegQueryDWordValue (HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Bld', Bld) then
    Result := (Major < {#VCMajor})
              or ((Major = {#VCMajor}) and (Minor < {#VCMinor}))
              or ((Major = {#VCMajor}) and (Minor = {#VCMinor}) and (Bld < {#VCBld}));
end;

// Any VB-Audio playback device (VB-CABLE, its A/B cables, VoiceMeeter) present?
function HasVbCable: Boolean;
var
  Keys: TArrayOfString;
  I: Integer;
  Name: String;
begin
  Result := False;
  if RegGetSubkeyNames (HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render', Keys) then
    for I := 0 to GetArrayLength (Keys) - 1 do
      if RegQueryStringValue (HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\' + Keys[I] + '\Properties',
                              '{b3f8fa53-0004-438e-9003-51a46e139bfc},6', Name)
         and (Pos ('VB-Audio', Name) > 0) then
      begin
        Result := True;
        Exit;
      end;
end;

// It keeps running in the tray when its window is closed, so it's asked to quit first.
procedure QuitRunningCopy (const Exe: String);
var
  Code: Integer;
begin
  if not FileExists (Exe) then
    Exit;
  ExecAsOriginalUser (Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep (3000);   // (time to close its devices; CloseApplications catches a copy still running)
end;

function PrepareToInstall (var NeedsRestart: Boolean): String;
begin
  QuitRunningCopy (ExpandConstant ('{app}\{#AppExe}'));
  Result := '';
end;

function InitializeUninstall: Boolean;
begin
  QuitRunningCopy (ExpandConstant ('{app}\{#AppExe}'));
  Result := True;
end;

// "Start with Windows" (set from the app) is removed with the exe it points at (one elsewhere is left
// alone). Settings in %APPDATA%\PatchbayConsole are kept.
procedure CurUninstallStepChanged (CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if (CurUninstallStep = usPostUninstall)
     and RegQueryStringValue (HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#AppName}', Command)
     and (Pos (Lowercase (ExpandConstant ('{app}\{#AppExe}')), Lowercase (Command)) > 0) then
    RegDeleteValue (HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#AppName}');
end;
