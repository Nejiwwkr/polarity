; Polarity Windows installer (Inno Setup 6)
;
; Build with:
;   ISCC.exe /DMyAppVersion=1.1.0 /DStageDir=..\dist\Polarity-1.1.0-win64 polarity.iss
;
; The package_release.ps1 script passes both defines automatically.

#ifndef MyAppVersion
  #define MyAppVersion "1.1.0"
#endif

#ifndef StageDir
  #define StageDir "..\dist\Polarity-" + MyAppVersion + "-win64"
#endif

[Setup]
AppId={{0B7E9F42-3A6C-4D18-9F2B-7C5D1E8A4B60}
AppName=Polarity
AppVersion={#MyAppVersion}
AppVerName=Polarity {#MyAppVersion}
AppPublisher=Nejiwwkr
AppPublisherURL=https://github.com/Nejiwwkr/polarity
AppSupportURL=https://github.com/Nejiwwkr/polarity/issues
AppUpdatesURL=https://github.com/Nejiwwkr/polarity/releases
DefaultDirName={autopf}\Polarity
DefaultGroupName=Polarity
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Polarity-{#MyAppVersion}-win64-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={code:GetVst3Dir}\Polarity.vst3\Contents\x86_64-win\Polarity.vst3
UninstallDisplayName=Polarity {#MyAppVersion}
VersionInfoCompany=Nejiwwkr
VersionInfoDescription=Polarity VST3 and Standalone
VersionInfoProductName=Polarity
VersionInfoProductVersion={#MyAppVersion}
VersionInfoVersion={#MyAppVersion}

; Add a Tools\polarity.ico to give the setup its own icon.
; #if FileExists("..\Tools\polarity.ico")
; SetupIconFile=..\Tools\polarity.ico
; #endif

[Components]
Name: "vst3"; Description: "VST3 plug-in (64-bit)"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#StageDir}\VST3\Polarity.vst3\*"; DestDir: "{code:GetVst3Dir}\Polarity.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#StageDir}\Standalone\Polarity.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{code:GetProgramsDir}\Polarity"; Filename: "{app}\Polarity.exe"; Components: standalone
Name: "{code:GetDesktopDir}\Polarity"; Filename: "{app}\Polarity.exe"; Tasks: desktopicon; Components: standalone

[Run]
Filename: "{app}\Polarity.exe"; Description: "Launch Polarity"; Flags: nowait postinstall skipifsilent; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{userappdata}\VST3\Polarity.vst3"; Check: not IsAdminInstall
Type: filesandordirs; Name: "{commoncf64}\VST3\Polarity.vst3"; Check: IsAdminInstall

[Code]
function IsAdminInstall: Boolean;
begin
  Result := IsAdminInstallMode;
end;

function GetVst3Dir(Param: String): String;
begin
  if IsAdminInstall then
    Result := ExpandConstant('{commoncf64}\VST3')
  else
    Result := ExpandConstant('{userappdata}\VST3');
end;

function GetProgramsDir(Param: String): String;
begin
  if IsAdminInstall then
    Result := ExpandConstant('{commonprograms}')
  else
    Result := ExpandConstant('{userprograms}');
end;

function GetDesktopDir(Param: String): String;
begin
  if IsAdminInstall then
    Result := ExpandConstant('{commondesktop}')
  else
    Result := ExpandConstant('{userdesktop}');
end;
