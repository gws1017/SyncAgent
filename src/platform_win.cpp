#include "platform.h"
#include <windows.h>
#include <shlobj.h>
#include <string>

static std::wstring SaveFilePath() {
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\idlegame";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\save.txt";
    }
    return L"save.txt";
}

FILE* OpenSaveFileForRead() {
    FILE* f = nullptr;
    _wfopen_s(&f, SaveFilePath().c_str(), L"r");
    return f;
}

FILE* OpenSaveFileForWrite() {
    FILE* f = nullptr;
    _wfopen_s(&f, SaveFilePath().c_str(), L"w");
    return f;
}

void BackupSaveFile() {
    std::wstring path = SaveFilePath();
    std::wstring bak  = path + L".bak";
    CopyFileW(path.c_str(), bak.c_str(), FALSE); // 원본이 없거나 실패해도 그냥 무시
}
