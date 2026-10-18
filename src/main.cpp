#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <Windows.h>

#include "DragonbornPresence.h"

#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

[[nodiscard]] bool InitializeLogging() noexcept
{
    try {
        const auto logPath = SKSE::log::log_directory().value_or(
            []() {
                wchar_t buffer[MAX_PATH]{};
                const DWORD length = GetModuleFileNameW(
                    reinterpret_cast<HMODULE>(&__ImageBase),
                    buffer,
                    MAX_PATH);
                if (length == 0 || length >= MAX_PATH) {
                    return std::filesystem::current_path();
                }
                return std::filesystem::path(buffer).parent_path();
            }()) /
            "DragonbornPresence.log";

        auto sink =
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        return true;
    } catch (const std::exception& error) {
        OutputDebugStringA(
            "DragonbornPresence: logging initialization failed: ");
        OutputDebugStringA(error.what());
        OutputDebugStringA(" The plugin was not started.\n");
        return false;
    } catch (...) {
        OutputDebugStringA(
            "DragonbornPresence: logging initialization raised an unknown "
            "exception. The plugin was not started.\n");
        return false;
    }
}

void LogEntryPointFailure(const char* details) noexcept
{
    try {
        SKSE::log::critical(
            "DragonbornPresence entry-point failure: {} The plugin was stopped "
            "before registering game work; Skyrim can continue normally.",
            details ? details : "unknown error.");
    } catch (...) {
        OutputDebugStringA(
            "DragonbornPresence: entry-point failure; plugin stopped.\n");
    }
}

void OnSkseMessage(SKSE::MessagingInterface::Message* message) noexcept
{
    if (!message) {
        LogEntryPointFailure("SKSE delivered a null messaging event.");
        return;
    }

    using MI = SKSE::MessagingInterface;
    switch (message->type) {
    case MI::kDataLoaded:
        DragonbornPresence::RegisterGameEventHandlers();
        break;
    case MI::kNewGame:
    case MI::kPostLoadGame:
        DragonbornPresence::OnGameLoaded();
        break;
    default:
        break;
    }
}

}  // namespace

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    try {
        if (!skse) {
            OutputDebugStringA(
                "DragonbornPresence: SKSE supplied a null LoadInterface. "
                "The plugin was not started.\n");
            return false;
        }

        SKSE::Init(skse);
        if (!InitializeLogging()) return false;

        const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
        if (!plugin) {
            LogEntryPointFailure("SKSE PluginDeclaration is unavailable.");
            return false;
        }
        const auto& version = plugin->GetVersion();
        SKSE::log::info(
            "DragonbornPresence {}.{}.{} — plugin loaded.",
            version.major(),
            version.minor(),
            version.patch());

        DragonbornPresence::LoadConfig();

        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            LogEntryPointFailure("SKSE MessagingInterface is unavailable.");
            return false;
        }
        if (!messaging->RegisterListener(OnSkseMessage)) {
            LogEntryPointFailure(
                "SKSE rejected the messaging listener registration.");
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        LogEntryPointFailure(error.what());
        return false;
    } catch (...) {
        LogEntryPointFailure("unknown C++ exception.");
        return false;
    }
}
