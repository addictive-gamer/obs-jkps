; obs-jkps Windows installer
; Built with Inno Setup (https://jrsoftware.org/isinfo.php)
;
; This script detects an existing OBS Studio installation via the registry
; key that the official OBS Studio installer creates, and copies the
; plugin binary + data files directly into it, following the standard
; OBS plugin folder layout:
;   <OBS install dir>\obs-plugins\64bit\obs-jkps.dll
;   <OBS install dir>\data\obs-plugins\obs-jkps\...
;
; Build artifacts are expected to already exist under:
;   release\RelWithDebInfo\obs-jkps\bin\64bit\obs-jkps.dll
;   release\RelWithDebInfo\obs-jkps\data\...
; (i.e. after running the normal CMake build + `cmake --install`, which is
; exactly what the project's GitHub Actions workflow produces before
; invoking this script.)

#define MyAppName "OBS JKPS"
#define MyAppVersion GetEnv('OBS_JKPS_VERSION')
#if MyAppVersion == ""
  #define MyAppVersion "1.0.0"
#endif
#define MyAppPublisher "addictive-gamer"
#define MyAppURL "https://github.com/addictive-gamer/obs-jkps"
#define ReleaseDir GetEnv('OBS_JKPS_RELEASE_DIR')
#if ReleaseDir == ""
  #define ReleaseDir "..\..\release\RelWithDebInfo\obs-jkps"
#endif

[Setup]
AppId={{6C5E9B6E-6A9E-4C7A-9E37-2B0B0B7E9A11}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
; No dedicated install dir is offered: this is a plugin, it always
; installs into the detected OBS Studio installation.
DefaultDirName={code:GetOBSInstallDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
UsePreviousAppDir=no
OutputDir=..\..\release\installer
OutputBaseFilename=obs-jkps-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\obs-plugins\64bit\obs-jkps.dll

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Files]
Source: "{#ReleaseDir}\bin\64bit\obs-jkps.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#ReleaseDir}\bin\64bit\obs-jkps.pdb"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#ReleaseDir}\data\*"; DestDir: "{app}\data\obs-plugins\obs-jkps"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
var
  OBSPath: string;

function FindOBSInstallDir(): string;
var
  Path: string;
begin
  Result := '';

  { Installed via the official 64-bit OBS Studio installer }
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Path) then begin
    if DirExists(Path) then begin
      Result := Path;
      exit;
    end;
  end;

  { Fallback: look up the Uninstall registry entry OBS Studio registers }
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio',
    'InstallLocation', Path) then begin
    if DirExists(Path) then begin
      Result := Path;
      exit;
    end;
  end;

  { Last resort: common default path }
  Path := ExpandConstant('{commonpf64}\obs-studio');
  if DirExists(Path) then
    Result := Path;
end;

function GetOBSInstallDir(Param: string): string;
begin
  Result := OBSPath;
end;

function InitializeSetup(): Boolean;
begin
  OBSPath := FindOBSInstallDir();

  if OBSPath = '' then begin
    MsgBox('No se encontró una instalación de OBS Studio en este equipo.' + #13#10 +
      'Instala OBS Studio (https://obsproject.com) antes de continuar, o copia manualmente ' +
      'los archivos del plugin en su carpeta de instalación.', mbError, MB_OK);
    Result := False;
  end else begin
    MsgBox('Se instalará el plugin JKPS - Teclas por segundo en la siguiente instalación de OBS Studio detectada:' +
      #13#10 + #13#10 + OBSPath, mbInformation, MB_OK);
    Result := True;
  end;
end;
