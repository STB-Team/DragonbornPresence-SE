#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "DragonbornPresence.h"

namespace {
    void InitializeLogging() {
        auto path = SKSE::log::log_directory();
        if (!path) return;
        *path /= "DragonbornPresence.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log  = std::make_shared<spdlog::logger>("global", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogging();
    SKSE::log::info("DragonbornPresence 2.0.0");

    DragonbornPresence::SetLocale();
    DragonbornPresence::RegisterGameEventHandlers();
    return true;
}
