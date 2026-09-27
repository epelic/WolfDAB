Unicode True
Name "WolfDAB PlutoSDR"
OutFile "${__FILEDIR__}\..\releases\WolfDAB-PlutoSDR-Setup-x64.exe"
InstallDir "$PROGRAMFILES64\WolfDAB PlutoSDR"
InstallDirRegKey HKLM "Software\WolfDAB-PlutoSDR" "InstallDir"
RequestExecutionLevel admin
BrandingText "WolfDAB PlutoSDR"
VIProductVersion "1.0.17.0"
VIAddVersionKey /LANG=1033 "ProductName" "WolfDAB PlutoSDR"
VIAddVersionKey /LANG=1033 "ProductVersion" "1.0.17"
VIAddVersionKey /LANG=1033 "FileVersion" "1.0.17"
VIAddVersionKey /LANG=1033 "CompanyName" "Freewaves.it"
VIAddVersionKey /LANG=1033 "FileDescription" "WolfDAB PlutoSDR Installer"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright Freewaves.it - Emanuele Pelicioli"

!include "MUI2.nsh"
!define MUI_ICON "${__FILEDIR__}\..\assets\wolfdab.ico"
!define MUI_UNICON "${__FILEDIR__}\..\assets\wolfdab.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\WolfDAB-PlutoSDR.exe"

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

LangString DESC_SHORTCUT ${LANG_ITALIAN} "Avvia WolfDAB PlutoSDR"
LangString DESC_SHORTCUT ${LANG_ENGLISH} "Launch WolfDAB PlutoSDR"
LangString DESC_SHORTCUT ${LANG_GERMAN} "WolfDAB PlutoSDR starten"
LangString DESC_SHORTCUT ${LANG_FRENCH} "Lancer WolfDAB PlutoSDR"
LangString PLUTO_DAB_DISABLED ${LANG_ITALIAN} "Il PlutoSDR è stato rilevato, ma la banda DAB Band III non è abilitata. Abilitare l'estensione di sintonia AD9364 prima di trasmettere."
LangString PLUTO_DAB_DISABLED ${LANG_ENGLISH} "The PlutoSDR was detected, but DAB Band III is not enabled. Enable the AD9364 tuning-range extension before transmitting."
LangString PLUTO_DAB_DISABLED ${LANG_GERMAN} "Der PlutoSDR wurde erkannt, aber DAB Band III ist nicht aktiviert. Aktivieren Sie vor dem Senden die AD9364-Frequenzbereichserweiterung."
LangString PLUTO_DAB_DISABLED ${LANG_FRENCH} "Le PlutoSDR a été détecté, mais la bande DAB III n'est pas activée. Activez l'extension de plage AD9364 avant d'émettre."
LangString PLUTO_NOT_FOUND ${LANG_ITALIAN} "PlutoSDR non rilevato durante l'installazione. Collegarlo alla porta USB dati prima di usare il trasmettitore."
LangString PLUTO_NOT_FOUND ${LANG_ENGLISH} "PlutoSDR was not detected during installation. Connect it through the USB data port before using the transmitter."
LangString PLUTO_NOT_FOUND ${LANG_GERMAN} "PlutoSDR wurde während der Installation nicht erkannt. Schließen Sie ihn vor der Verwendung des Senders am USB-Datenanschluss an."
LangString PLUTO_NOT_FOUND ${LANG_FRENCH} "Le PlutoSDR n'a pas été détecté pendant l'installation. Connectez-le au port USB de données avant d'utiliser l'émetteur."

Section "WolfDAB PlutoSDR" SEC_MAIN
  SetOutPath "$INSTDIR"
  File "${__FILEDIR__}\..\outputs\WolfDAB-PlutoSDR\WolfDAB-PlutoSDR.exe"
  File "${__FILEDIR__}\..\outputs\WolfDAB-PlutoSDR\dabtx.exe"
  File "${__FILEDIR__}\..\outputs\WolfDAB-PlutoSDR\ffmpeg.exe"
  File "${__FILEDIR__}\..\outputs\WolfDAB-PlutoSDR\*.dll"
  File "${__FILEDIR__}\..\README.md"
  File "${__FILEDIR__}\..\LICENSE"
  File "${__FILEDIR__}\..\THIRD_PARTY.md"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\WolfDAB-PlutoSDR" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB-PlutoSDR" "DisplayName" "WolfDAB PlutoSDR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB-PlutoSDR" "DisplayIcon" "$INSTDIR\WolfDAB-PlutoSDR.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB-PlutoSDR" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB-PlutoSDR" "Publisher" "Freewaves.it"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB-PlutoSDR" "DisplayVersion" "1.0.17"
  CreateDirectory "$SMPROGRAMS\WolfDAB PlutoSDR"
  CreateShortcut "$SMPROGRAMS\WolfDAB PlutoSDR\WolfDAB PlutoSDR.lnk" "$INSTDIR\WolfDAB-PlutoSDR.exe" "" "$INSTDIR\WolfDAB-PlutoSDR.exe" 0
  CreateShortcut "$DESKTOP\WolfDAB PlutoSDR.lnk" "$INSTDIR\WolfDAB-PlutoSDR.exe" "" "$INSTDIR\WolfDAB-PlutoSDR.exe" 0
  ExecWait '"$INSTDIR\dabtx.exe" --check-pluto-dab' $1
  StrCmp $1 0 pluto_check_done
  StrCmp $1 2 pluto_dab_disabled pluto_not_found
pluto_dab_disabled:
  MessageBox MB_OK|MB_ICONEXCLAMATION $(PLUTO_DAB_DISABLED)
  Goto pluto_check_done
pluto_not_found:
  MessageBox MB_OK|MB_ICONINFORMATION $(PLUTO_NOT_FOUND)
pluto_check_done:
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\WolfDAB PlutoSDR.lnk"
  RMDir /r "$SMPROGRAMS\WolfDAB PlutoSDR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WolfDAB-PlutoSDR"
  DeleteRegKey HKLM "Software\WolfDAB-PlutoSDR"
  RMDir /r "$INSTDIR"
SectionEnd
