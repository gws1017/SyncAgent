#include "platform.h"
#include <windows.h>
#include <shlobj.h>

std::wstring GetSaveFilePath() {
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\idlegame";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\save.txt";
    }
    return L"save.txt";
}
