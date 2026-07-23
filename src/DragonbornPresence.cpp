#include "DragonbornPresence.h"
#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbGameDataSource.h"
#include "DragonbornPresence/adapters/discord/DiscordPresenceClient.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/SkseLogger.h"
#include "DragonbornPresence/application/PresenceCoordinator.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbRuntimeAdapter.h"

#include <exception>

namespace DragonbornPresence
{
    namespace
    {
        /// Selects and owns concrete infrastructure adapters for the plugin lifetime.
        ///
        /// The runtime adapter is declared last so its scheduler thread is stopped and
        /// joined before the coordinator and the coordinator's dependencies are destroyed.
        adapters::config::JsonConfigProvider g_configProvider;
        adapters::SkyrimTrueBeliever::StbGameDataSource g_gameDataSource;
        adapters::discord::DiscordPresenceClient g_presenceClient;
        adapters::SkyrimTrueBeliever::SkseLogger g_logger;

        application::PresenceCoordinator g_presenceCoordinator(
            g_configProvider,
            g_gameDataSource,
            g_presenceClient,
            g_logger);

        adapters::SkyrimTrueBeliever::StbRuntimeAdapter g_runtimeAdapter(
            g_presenceCoordinator,
            g_logger);

    } // namespace

    /// Loads configuration without allowing errors to escape into SKSE.
    void LoadConfig() noexcept
    {
        try
        {
            g_presenceCoordinator.LoadConfig();
        }
        catch (const std::exception &error)
        {
            g_presenceCoordinator.HandleException("configuration loading", error.what());
        }
        catch (...)
        {
            g_presenceCoordinator.HandleUnknownException("configuration loading");
        }
    }

    /// Initializes integrations without allowing errors to escape into SKSE.
    void RegisterGameEventHandlers() noexcept
    {
        try
        {
            g_runtimeAdapter.RegisterGameEventHandlers();
        }
        catch (const std::exception &error)
        {
            g_runtimeAdapter.HandleException(
                "game event registration",
                error.what());
        }
        catch (...)
        {
            g_runtimeAdapter.HandleUnknownException("game event registration");
        }
    }

    /// Publishes the first state without allowing errors to escape into SKSE.
    void OnGameLoaded() noexcept
    {
        try
        {
            g_runtimeAdapter.OnGameLoaded();
        }
        catch (const std::exception &error)
        {
            g_runtimeAdapter.HandleException("game-load handler", error.what());
        }
        catch (...)
        {
            g_runtimeAdapter.HandleUnknownException("game-load handler");
        }
    }

} // namespace DragonbornPresence
