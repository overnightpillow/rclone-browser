; Inno Setup script for the Windows installer, driven by release_windows.ps1.
;
; Rewritten from upstream's: the publisher, the URLs and the executable name
; were all kapitainsky's, and the AppId was upstream's own GUID. Installing
; this fork with that AppId would have looked to Windows like an upgrade of
; upstream's installation -- same uninstall entry, same registry key, and an
; uninstall here would remove theirs.
;
; Qt 6 has no 32-bit build, so the two-architecture dance is gone; the arch is
; still a parameter because arm64 is a plausible target.

#define MyAppName "rclone-browser"
#define MyAppPublisher "overnightpillow"
#define MyAppURL "https://github.com/overnightpillow/rclone-browser"
#define MyAppExeName "rclone-browser.exe"

[Setup]
; A fresh GUID for this fork. It must never be changed once released: Windows
; keys the installed application off it, and a new one turns every upgrade
; into a second parallel installation.
AppId={{81F855B2-78C0-485C-9A51-03AC54CF118C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; The [Icons] "quicklaunchicon" entry uses {userappdata} but its [Tasks] entry has a proper IsAdminInstallMode Check.
UsedUserAreasWarning=no
LicenseFile=..\release\{#MyAppDir}\License.txt
PrivilegesRequiredOverridesAllowed=dialog
SetupIconFile=..\src\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
WizardStyle=modern

#if MyAppArch=="arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\release\{#MyAppDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:ProgramOnTheWeb,{#MyAppName}}"; Filename: "{#MyAppURL}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
