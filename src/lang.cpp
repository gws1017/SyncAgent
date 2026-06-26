#include "lang.h"

Lang g_lang = Lang::KO;

const char* T(const char* ko, const char* en) {
    return (g_lang == Lang::KO) ? ko : en;
}

const wchar_t* TW(const wchar_t* ko, const wchar_t* en) {
    return (g_lang == Lang::KO) ? ko : en;
}
