; Dev by Sufyan - Inno Setup 6 script
;
; Expects the staged portable folder at ..\deploy\ (DevBySufyan.exe plus
; the Qt runtime files copied by windeployqt, app resources and LICENSE).
; The GitHub Actions workflow (.github/workflows/devbysufyan-windows.yml)
; produces that folder and then runs:  iscc installer\DevBySufyan.iss
; The finished installer, DevBySufyan-Setup.exe, is written to the repo
; root (OutputDir below). All paths are relative to this script's folder.

#define AppName "Dev by Sufyan"
#define AppVersion "1.0.1"
#define AppPublisher "Abu Sufyan"
#define AppExeName "DevBySufyan.exe"

[Setup]
AppId={{B7E4A1C2-9D3F-4E8A-A5C6-1F2D3B4A5967}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\DevBySufyan
DefaultGroupName={#AppName}
LicenseFile=..\LICENSE
OutputDir=..\
OutputBaseFilename=DevBySufyan-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
SetupIconFile=..\RedPandaIDE\images\devcpp.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Registry]
; Red Panda checks this key (PORTABLE_CONFIG=registry) to decide it is an
; installed, not portable, copy; without it the app tries to create a
; "config" folder beside the exe and fails under Program Files.
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\RedPanda-C++"; ValueType: string; ValueName: "InstallLocation"; ValueData: "{app}"; Flags: uninsdeletekey
