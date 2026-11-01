#ifndef PackageDir
  #define PackageDir "..\..\dist\ClipboardHistory-0.2.0-Windows-x64"
#endif
#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif

[Setup]
#ifdef TestAppId
AppId={#TestAppId}
AppName=Clipboard History Installer Test
DefaultDirName={tmp}\{#TestAppId}
#else
AppId={{DAB4076C-D37F-4878-81CC-61EEAB162F70}
AppName=Clipboard History
DefaultDirName={localappdata}\Programs\ClipboardHistory
#endif
AppVersion={#AppVersion}
AppPublisher=ClipboardHistory
UsePreviousAppDir=yes
UsePreviousTasks=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=ClipboardHistory-{#AppVersion}-Windows-x64-Setup
SetupIconFile=..\..\resources\app-icon.ico
UninstallDisplayIcon={app}\ClipboardHistory.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
InfoBeforeFile=upgrade-info.txt

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Excludes: "\data\*,\themes\*,\ClipboardHistory.ini,\custom-theme.json,\qmltooling\*,Qt6Quick3DUtils.dll,Qt6Test.dll,tst_*.exe,*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\custom-theme.json"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

#ifndef TestAppId
[Icons]
Name: "{autoprograms}\Clipboard History"; Filename: "{app}\ClipboardHistory.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Clipboard History"; Filename: "{app}\ClipboardHistory.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ClipboardHistory.exe"; Description: "Launch Clipboard History"; Flags: nowait postinstall skipifsilent unchecked
#endif
