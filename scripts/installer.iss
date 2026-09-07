; Optional Inno Setup 6 build: iscc scripts/installer.iss
; Build portable binaries first. Installer has not been validated unless STATUS says so.
[Setup]
AppId=CantoDeck
AppName=CantoDeck
AppVersion=0.1.0
DefaultDirName={localappdata}\Programs\CantoDeck
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=CantoDeck-0.1.0-setup-unsigned
UninstallDisplayIcon={app}\CantoDeck.exe
LicenseFile=..\LICENSE
[Files]
Source: "..\build\CantoDeck_artefacts\Release\CantoDeck.exe"; DestDir: "{app}"
Source: "..\README.vi.md"; DestDir: "{app}"
Source: "..\LICENSE"; DestDir: "{app}"
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"
[Icons]
Name: "{userprograms}\CantoDeck"; Filename: "{app}\CantoDeck.exe"
