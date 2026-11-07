; Per-user installer. userdata and user packs survive upgrade and uninstall.
; Build with tools/packaging/windows/make_setup.sh which stages the files
; from the windows package into stage/ then runs ISCC.
#define MyAppName "1 vs 100"
#ifndef MyAppVersion
#define MyAppVersion "0.1.0"
#endif
#define MyPublisher "1vs100"

[Setup]
AppId={{1VS100-PC-PER-USER}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
AppPublisher={#MyPublisher}
DefaultDirName={localappdata}\1vs100
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
OutputBaseFilename=1vs100-{#MyAppVersion}-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; Flags: unchecked

[Dirs]
Name: "{app}\content"; Flags: uninsneveruninstall
Name: "{app}\userdata"; Flags: uninsneveruninstall

[Files]
Source: "stage\onevs100.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\launcher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\rexruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\rexgpu-xenos.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\dxcompiler.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\dxil.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\RUN.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\manifest.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\onevs100.toml"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "stage\content\.keep"; DestDir: "{app}\content"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{autoprograms}\1 vs 100"; Filename: "{app}\launcher.exe"
Name: "{autodesktop}\1 vs 100"; Filename: "{app}\launcher.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\launcher.exe"; Description: "Launch 1 vs 100"; Flags: nowait postinstall skipifsilent
