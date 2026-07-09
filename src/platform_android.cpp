#include "platform.h"
#include <cstdio>
#include <string>

static std::string g_dataPath;

void PlatformInit(const char* dataPath) {
    g_dataPath = dataPath ? dataPath : ".";
}

static std::string SaveFilePath() {
    return g_dataPath + "/save.txt";
}

FILE* OpenSaveFileForRead() {
    return fopen(SaveFilePath().c_str(), "r");
}

FILE* OpenSaveFileForWrite() {
    return fopen(SaveFilePath().c_str(), "w");
}

void BackupSaveFile() {
    std::string src = SaveFilePath();
    std::string bak = src + ".bak";
    FILE* fsrc = fopen(src.c_str(), "rb");
    if (!fsrc) return;
    FILE* fdst = fopen(bak.c_str(), "wb");
    if (!fdst) { fclose(fsrc); return; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0)
        fwrite(buf, 1, n, fdst);
    fclose(fsrc);
    fclose(fdst);
}
