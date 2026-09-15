Unicode True
Name "WolfDAB"
OutFile "${__FILEDIR__}\..\..\..\releases\WolfDAB-Setup-x64.exe"
InstallDir "$PROGRAMFILES64\WolfDAB"
InstallDirRegKey HKLM "Software\WolfDAB" "InstallDir"
RequestExecutionLevel admin
BrandingText "WolfDAB"
VIProductVersion "1.0.22.0"
VIAddVersionKey /LANG=1033 "ProductName" "WolfDAB"
VIAddVersionKey /LANG=1033 "ProductVersion" "1.0.22"
VIAddVersionKey /LANG=1033 "FileVersion" "1.0.22"
VIAddVersionKey /LANG=1033 "CompanyName" "Freewaves.it"
VIAddVersionKey /LANG=1033 "FileDescription" "WolfDAB Installer"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright Freewaves.it - Emanuele Pelicioli"

!include "MUI2.nsh"
!define MUI_ICON "${__FILEDIR__}\..\assets\wolfdab.ico"
!define MUI_UNICON "${__FILEDIR__}\..\assets\wolfdab.ico"
!define MUI_ABORTWARNING

LicenseLangString WolfDABLicense 1040 "${__FILEDIR__}\EULA_it.txt"
LicenseLangString WolfDABLicense 1033 "${__FILEDIR__}\EULA_en.txt"
LicenseLangString WolfDABLicense 1031 "${__FILEDIR__}\EULA_de.txt"
LicenseLangString WolfDABLicense 1036 "${__FILEDIR__}\EULA_fr.txt"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE $(WolfDABLicense)
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "French"

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

LangString DESC_SHORTCUT ${LANG_ITALIAN} "Avvia WolfDAB"
LangString DESC_SHORTCUT ${LANG_ENGLISH} "Launch WolfDAB"
LangString DESC_SHORTCUT ${LANG_GERMAN} "WolfDAB starten"
LangString DESC_SHORTCUT ${LANG_FRENCH} "Lancer WolfDAB"

Section "WolfDAB" SEC_MAIN
  SetOutPath "$INSTDIR"
  File "${__FILEDIR__}\..\..\..\outputs\WolfDAB-portable\WolfDAB.exe"
  File "${__FILEDIR__}\..\..\..\outputs\WolfDAB-portable\dabtx.exe"
  File "${__FILEDIR__}\..\..\..\outputs\WolfDAB-portable\ffmpeg.exe"
  File "${__FILEDIR__}\..\..\..\outputs\WolfDAB-portable\*.dll"
  File "${__FILEDIR__}\..\README.md"
  File "${__FILEDIR__}\..\LICENSE"
  File "${__FILEDIR__}\..\THIRD_PARTY.md"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\WolfDAB" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB" "DisplayName" "WolfDAB"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB" "DisplayIcon" "$INSTDIR\WolfDAB.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB" "Publisher" "WolfDAB"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB" "DisplayVersion" "1.0.22"
  CreateDirectory "$SMPROGRAMS\WolfDAB"
  CreateShortcut "$SMPROGRAMS\WolfDAB\WolfDAB.lnk" "$INSTDIR\WolfDAB.exe" "" "$INSTDIR\WolfDAB.exe" 0
  CreateShortcut "$DESKTOP\WolfDAB.lnk" "$INSTDIR\WolfDAB.exe" "" "$INSTDIR\WolfDAB.exe" 0
  ExecWait '"$INSTDIR\WolfDAB.exe" --register-required' $0
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\WolfDAB.lnk"
  RMDir /r "$SMPROGRAMS\WolfDAB"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB"
  DeleteRegKey HKLM "Software\WolfDAB"
  RMDir /r "$INSTDIR"
SectionEnd
