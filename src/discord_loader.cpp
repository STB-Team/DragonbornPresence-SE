#include <Windows.h>
#include <delayimp.h>
#include <TlHelp32.h>

#include "discord_loader.h"

#include <cwchar>
#include <cwctype>
#include <format>
#include <string>
#include <string_view>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

void SetFailureReason(std::string* target, std::string_view reason) noexcept
{
    if (!target) return;
    try {
        target->assign(reason);
    } catch (...) {
    }
}

struct WindowsErrorDetails {
    std::string_view name;
    std::string_view explanation;
};

[[nodiscard]] constexpr WindowsErrorDetails DescribeWindowsError(
    DWORD error) noexcept
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
        return {"ERROR_FILE_NOT_FOUND", "the requested file or registry key is absent"};
    case ERROR_PATH_NOT_FOUND:
        return {"ERROR_PATH_NOT_FOUND", "part of the configured path does not exist"};
    case ERROR_ACCESS_DENIED:
        return {"ERROR_ACCESS_DENIED", "Windows denied access to the resource"};
    case ERROR_INVALID_HANDLE:
        return {"ERROR_INVALID_HANDLE", "Windows returned an invalid resource handle"};
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return {"ERROR_OUTOFMEMORY", "Windows could not allocate enough memory"};
    case ERROR_INVALID_DATA:
        return {"ERROR_INVALID_DATA", "Windows returned malformed data"};
    case ERROR_MOD_NOT_FOUND:
        return {"ERROR_MOD_NOT_FOUND", "the DLL or one of its dependencies is missing"};
    case ERROR_PROC_NOT_FOUND:
        return {"ERROR_PROC_NOT_FOUND", "the DLL does not export a required function"};
    case ERROR_BAD_EXE_FORMAT:
        return {"ERROR_BAD_EXE_FORMAT", "the DLL is damaged or has the wrong architecture"};
    case ERROR_DLL_INIT_FAILED:
        return {"ERROR_DLL_INIT_FAILED", "the DLL initialization routine failed"};
    default:
        return {"UNKNOWN_WINDOWS_ERROR", "Windows reported an unexpected system error"};
    }
}

void SetWindowsFailureReason(
    std::string* target,
    std::string_view operation,
    DWORD error) noexcept
{
    if (!target) return;
    const auto details = DescribeWindowsError(error);
    try {
        *target = std::format(
            "{} failed: Windows error {} ({}) — {}. The Discord integration was "
            "disabled; Skyrim can continue normally.",
            operation,
            error,
            details.name,
            details.explanation);
    } catch (...) {
        SetFailureReason(target, operation);
    }
}

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

[[nodiscard]] bool BuildPluginSiblingPath(
    const wchar_t* fileName,
    wchar_t (&path)[MAX_PATH]) noexcept
{
    const DWORD length = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(&__ImageBase),
        path,
        MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;

    wchar_t* separator = std::wcsrchr(path, L'\\');
    if (!separator) return false;
    const auto remaining =
        MAX_PATH - static_cast<std::size_t>(separator - path) - 1;
    return wcscpy_s(separator + 1, remaining, fileName) == 0;
}

}  // namespace

namespace DragonbornPresence::detail {

bool IsDiscordRunning(std::string* failureReason) noexcept
{
    constexpr wchar_t kDiscordProtocolCommand[] =
        L"discord\\shell\\open\\command";

    try {
        DWORD byteCount = 0;
        const LSTATUS sizeStatus = RegGetValueW(
            HKEY_CLASSES_ROOT,
            kDiscordProtocolCommand,
            nullptr,
            RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
            nullptr,
            nullptr,
            &byteCount);
        if (sizeStatus != ERROR_SUCCESS) {
            SetWindowsFailureReason(
                failureReason,
                "Reading the discord:// protocol registration",
                static_cast<DWORD>(sizeStatus));
            return false;
        }
        if (byteCount < sizeof(wchar_t)) {
            SetFailureReason(
                failureReason,
                "The discord:// protocol registration is empty or malformed. "
                "The plugin will not repair, install, or launch Discord.");
            return false;
        }

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
        if (readStatus != ERROR_SUCCESS) {
            SetWindowsFailureReason(
                failureReason,
                "Reading the discord:// executable registration",
                static_cast<DWORD>(readStatus));
            return false;
        }

        const auto* executablePath = ExtractExecutablePath(command.data());
        if (!executablePath || !IsExistingFile(executablePath)) {
            SetFailureReason(
                failureReason,
                "The executable registered for discord:// does not exist. "
                "Discord Desktop may have been moved or uninstalled. The plugin "
                "will not repair or reinstall it.");
            return false;
        }

        const auto* executableName = std::wcsrchr(executablePath, L'\\');
        executableName = executableName ? executableName + 1 : executablePath;

        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            SetWindowsFailureReason(
                failureReason,
                "Creating the Windows process list",
                GetLastError());
            return false;
        }

        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);
        if (!Process32FirstW(snapshot, &process)) {
            const DWORD error = GetLastError();
            CloseHandle(snapshot);
            SetWindowsFailureReason(
                failureReason,
                "Reading the Windows process list",
                error);
            return false;
        }

        bool found = false;
        do {
            if (_wcsicmp(process.szExeFile, executableName) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &process));
        CloseHandle(snapshot);

        if (!found) {
            SetFailureReason(
                failureReason,
                "Discord Desktop is installed but its registered process is not "
                "running. The plugin was disabled and will not start Discord.");
        }
        return found;
    } catch (const std::exception& error) {
        try {
            if (failureReason) {
                *failureReason = std::format(
                    "Discord process inspection raised a C++ exception: {}. "
                    "The plugin was disabled.",
                    error.what());
            }
        } catch (...) {
            SetFailureReason(failureReason, "Discord process inspection failed.");
        }
        return false;
    } catch (...) {
        SetFailureReason(
            failureReason,
            "Discord process inspection raised an unknown exception. The plugin "
            "was disabled.");
        return false;
    }
}

bool LoadDiscordSdk(std::string* failureReason) noexcept
{
    if (GetModuleHandleW(L"discord_game_sdk.dll")) return true;

    wchar_t path[MAX_PATH]{};
    if (!BuildPluginSiblingPath(L"discord_game_sdk.dll", path)) {
        SetFailureReason(
            failureReason,
            "The path to discord_game_sdk.dll could not be built from the plugin "
            "directory. The integration was disabled before calling the SDK.");
        return false;
    }
    if (!IsExistingFile(path)) {
        SetFailureReason(
            failureReason,
            "discord_game_sdk.dll is missing next to DragonbornPresence.dll. "
            "Reinstall the mod; the plugin will not download or install anything.");
        return false;
    }

    if (!LoadLibraryExW(
            path,
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
        SetWindowsFailureReason(
            failureReason,
            "Loading the bundled discord_game_sdk.dll",
            GetLastError());
        return false;
    }
    return true;
}

}  // namespace DragonbornPresence::detail

static FARPROC WINAPI DiscordDelayHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (dliNotify != dliNotePreLoadLibrary || !pdli || !pdli->szDll ||
        _stricmp(pdli->szDll, "discord_game_sdk.dll") != 0) {
        return nullptr;
    }

    if (const HMODULE loaded = GetModuleHandleW(L"discord_game_sdk.dll")) {
        return reinterpret_cast<FARPROC>(loaded);
    }

    wchar_t path[MAX_PATH]{};
    if (!BuildPluginSiblingPath(L"discord_game_sdk.dll", path)) return nullptr;
    return reinterpret_cast<FARPROC>(LoadLibraryExW(
        path,
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = DiscordDelayHook;
