#include <Windows.h>
#include <delayimp.h>
#include <TlHelp32.h>

#include "discord_loader.h"

#include <cwchar>
#include <cwctype>
#include <string>

namespace {

[[nodiscard]] wchar_t* ExtractExecutablePath(wchar_t* command) noexcept
{
    while (*command != L'\0' && std::iswspace(*command)) ++command;
    if (*command == L'\0') return nullptr;

    if (*command == L'"') {
        ++command;
        auto* closingQuote = std::wcschr(command, L'"');
        if (!closingQuote) return nullptr;
        *closingQuote = L'\0';
        return command;
    }

    auto* separator = command;
    while (*separator != L'\0' && !std::iswspace(*separator)) ++separator;
    *separator = L'\0';
    return command;
}

[[nodiscard]] bool IsExistingFile(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}  // namespace

namespace DragonbornPresence::detail {

bool IsDiscordRunning() noexcept
{
    constexpr wchar_t kDiscordProtocolCommand[] =
        L"discord\\shell\\open\\command";

    DWORD byteCount = 0;
    const LSTATUS sizeStatus = RegGetValueW(
        HKEY_CLASSES_ROOT,
        kDiscordProtocolCommand,
        nullptr,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
        nullptr,
        nullptr,
        &byteCount);
    if (sizeStatus != ERROR_SUCCESS || byteCount < sizeof(wchar_t)) return false;

    try {
        std::wstring command(byteCount / sizeof(wchar_t), L'\0');
        DWORD valueType = 0;
        const LSTATUS readStatus = RegGetValueW(
            HKEY_CLASSES_ROOT,
            kDiscordProtocolCommand,
            nullptr,
            RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
            &valueType,
            command.data(),
            &byteCount);
        if (readStatus != ERROR_SUCCESS) return false;

        const auto* executablePath = ExtractExecutablePath(command.data());
        if (!executablePath || !IsExistingFile(executablePath)) return false;

        const auto* executableName = std::wcsrchr(executablePath, L'\\');
        executableName = executableName ? executableName + 1 : executablePath;

        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);
        bool found = false;
        if (Process32FirstW(snapshot, &process)) {
            do {
                if (_wcsicmp(process.szExeFile, executableName) == 0) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &process));
        }
        CloseHandle(snapshot);
        return found;
    } catch (...) {
        return false;
    }
}

}  // namespace DragonbornPresence::detail

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
