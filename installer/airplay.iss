; airplay.iss - Inno Setup script for the AirPlay receiver.
;
; Built by scripts/make-installer.ps1, which reads the version out of app/CMakeLists.txt and
; passes it in as MyAppVersion; SourceDir is the portable folder make-portable.ps1 produced.
; Do not run ISCC on this file by hand without both defines - there are no defaults on purpose,
; so a stale version can never be baked into an installer.
;
; Per-user by design (PrivilegesRequired=lowest): no UAC prompt, installs under
; %LOCALAPPDATA%\Programs\AirPlay. The receiver needs no machine-wide anything - the one
; system-level thing it wants, a firewall rule, needs admin and is left to Windows' own
; first-run prompt, which the user has to answer with "private networks" ticked.

#ifndef MyAppVersion
  #error MyAppVersion is not defined - build through scripts/make-installer.ps1
#endif
#ifndef SourceDir
  #error SourceDir is not defined - build through scripts/make-installer.ps1
#endif

#define MyAppName "AirPlay Alicisi"
#define MyAppExeName "airplay-gui.exe"
#define MyAppPublisher "Lng999"
#define MyAppUrl "https://github.com/Lng999/airplay"

[Setup]
; Never change AppId: it is what makes an install an upgrade rather than a second copy.
AppId={{7C2F4A61-9D3E-4B58-A0C7-2E1B6F8D5A94}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppUrl}
AppSupportURL={#MyAppUrl}/issues
AppUpdatesURL={#MyAppUrl}/releases

DefaultDirName={autopf}\AirPlay
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; The payload is ~232 MB of GStreamer runtime; lzma2/max gets it to about a third of that.
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

OutputDir={#OutputDir}
OutputBaseFilename=AirPlay-Setup-{#MyAppVersion}
SetupIconFile={#SourceDir}\..\..\app\res\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
WizardStyle=modern
LicenseFile={#SourceDir}\LICENSE

; An update arrives while the app is running (it launched us). Shut it down rather than
; leaving files locked; RestartApplications=no because our own [Run] entry starts it again.
CloseApplications=force
RestartApplications=no

[Languages]
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "startup"; Description: "Windows açılışında başlat"; GroupDescription: "Ek seçenekler"; Flags: unchecked

[Files]
; Everything make-portable.ps1 laid out: the two exes plus the whole ucrt64 runtime tree.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; "Windows açılışında başlat" has exactly one mechanism: the HKCU Run value. The app's own
; checkbox (app/src/ui/autostart.cpp) reads and writes this very value - same key, same value
; name "airplay", same "<exe>" -minimized form - so installer and app can never disagree, and
; never both launch us at logon. -minimized: coming up at logon should land in the
; notification area. uninsdeletevalue: uninstalling must not leave a Run entry behind that
; points at a deleted exe.
;
; Check: not WizardSilent - a silent run is the update path (the app runs setup /SILENT without
; /TASKS), and Inno restores the previous install's task selection there (UsePreviousTasks=yes,
; the default). Without the check, a user who ticked the task once and later cleared the box
; inside the app would get the Run value back on every auto-update. A silent run leaves the
; value exactly as the app last set it. The v0.3.0 Startup shortcut is migrated by the app
; itself (autostart.cpp), not here.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "airplay"; ValueData: """{app}\{#MyAppExeName}"" -minimized"; Flags: uninsdeletevalue; Tasks: startup; Check: not WizardSilent

[Run]
; No skipifsilent: a silent run is the update path, and the app it replaced has to come back.
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall

[InstallDelete]
; No Startup shortcut any more - the startup task writes the Run value (see [Registry]).
; Unreleased builds of this script put "airplay.lnk" there; drop it. The v0.3.0 installer's
; "AirPlay Alicisi.lnk" is deliberately NOT deleted here: an upgrade is usually silent, the
; Run value is not written then, and deleting it would silently lose the user's choice. The
; app migrates it to the Run value on first start (migrateLegacyStartupShortcut, autostart.cpp).
Type: files; Name: "{userstartup}\airplay.lnk"

[UninstallDelete]
; The GStreamer plugin cache the app builds on first run. Settings and logs under
; %APPDATA%\airplay and %LOCALAPPDATA%\airplay are left alone - a reinstall should find them.
Type: files; Name: "{localappdata}\airplay\gst-registry.bin"
