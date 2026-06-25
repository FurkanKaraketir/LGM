; LGM Windows installer (Inno Setup 6)
; Build: ISCC.exe /DAppVersion=0.2.0 /DStageDir=C:\path\to\staged\files LGM.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\dist\LGM-0.1.0-win64"
#endif
#ifndef RepoRoot
  #define RepoRoot "..\.."
#endif

[Setup]
AppId={{A3B8F2E1-9C4D-4A7B-8E2F-1D5C6B9A0E3F}
AppName=LGM
AppVersion={#AppVersion}
AppVerName=LGM {#AppVersion}
AppPublisher=Furkan Karaketir
AppPublisherURL=https://github.com/FurkanKaraketir/LGM
AppSupportURL=https://github.com/FurkanKaraketir/LGM/issues
AppUpdatesURL=https://github.com/FurkanKaraketir/LGM/releases
DefaultDirName={autopf}\LGM
DefaultGroupName=LGM
LicenseFile={#RepoRoot}\COPYING
InfoBeforeFile=
OutputDir={#RepoRoot}\dist
OutputBaseFilename=LGM-{#AppVersion}-win64-setup
SetupIconFile={#RepoRoot}\src\assets\app_logo.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\LGM.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\LGM"; Filename: "{app}\LGM.exe"
Name: "{group}\{cm:UninstallProgram,LGM}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\LGM"; Filename: "{app}\LGM.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LGM.exe"; Description: "{cm:LaunchProgram,LGM}"; Flags: nowait postinstall skipifsilent

[Messages]
LicenseLabel3=You must accept the GNU General Public License v3.0 to install LGM.
