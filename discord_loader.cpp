#include <Windows.h>
#include <delayimp.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static FARPROC WINAPI DiscordDelayHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary &&
        _stricmp(pdli->szDll, "discord_game_sdk.dll") == 0) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
        wchar_t* sep = wcsrchr(path, L'\\');
        if (sep) {
            wcscpy_s(sep + 1, MAX_PATH - static_cast<size_t>(sep - path) - 1,
                     L"discord_game_sdk.dll");
            return reinterpret_cast<FARPROC>(LoadLibraryW(path));
        }
    }
    return nullptr;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = DiscordDelayHook;
