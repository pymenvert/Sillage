; Sillage — single-file Windows installer (Inno Setup 6).
; Built by the release CI: ISCC /DBinDir=<build output> /DVersion=x.y.z sillage.iss
; Produces: sillage-setup-<version>.exe

#ifndef BinDir
  #define BinDir "..\..\build\windows-msvc\engine\Release"
#endif
#ifndef Version
  #define Version "0.1.0"
#endif

[Setup]
AppId={{7E3F2C41-9B7A-4E2D-A9D1-5B1153A9E0C2}
AppName=Sillage
AppVersion={#Version}
AppPublisher=Sillage
DefaultDirName={autopf}\Sillage
DefaultGroupName=Sillage
OutputBaseFilename=sillage-setup-{#Version}
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
PrivilegesRequired=admin
UninstallDisplayName=Sillage

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#BinDir}\sillage-engine.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs
Source: "..\..\examples\demo-project.json"; DestDir: "{commonappdata}\Sillage"; \
    DestName: "project.json"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "install-service.ps1"; DestDir: "{app}\packaging"; Flags: ignoreversion
Source: "..\portable\run-sillage.bat"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Sillage"; Filename: "{app}\run-sillage.bat"; WorkingDir: "{app}"
Name: "{group}\Interface Sillage"; Filename: "http://127.0.0.1:8080"
Name: "{group}\Désinstaller Sillage"; Filename: "{uninstallexe}"

[Tasks]
Name: "service"; Description: "Installer comme service Windows (démarrage automatique 24/7)"; \
    Flags: unchecked

[Run]
; Not runhidden: if the service fails to install or start, the technician must
; see it now rather than discover a dead service on show day. The script exits
; non-zero on failure, which Inno surfaces to the user.
Filename: "powershell.exe"; \
    Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\packaging\install-service.ps1"" -BinaryPath ""{app}\sillage-engine.exe"" -ConfigPath ""{commonappdata}\Sillage\project.json"""; \
    StatusMsg: "Installation du service Windows..."; Tasks: service; \
    Flags: waituntilterminated
Filename: "{app}\run-sillage.bat"; Description: "Lancer Sillage maintenant"; \
    Flags: postinstall nowait skipifsilent unchecked

[UninstallRun]
Filename: "powershell.exe"; \
    Parameters: "-Command ""if (Get-Service Sillage -ErrorAction SilentlyContinue) {{ Stop-Service Sillage; sc.exe delete Sillage }}"""; \
    RunOnceId: "RemoveService"; Flags: runhidden
