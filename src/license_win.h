#pragma once
#ifdef _WIN32
#include <windows.h>
#include <string>

namespace wolfdab_license {
std::wstring installation_code();
bool is_registered();
bool show_registration(HWND owner, int language);
}
#endif
