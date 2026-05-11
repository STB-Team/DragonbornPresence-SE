cmake_minimum_required(VERSION 3.21)
project(DragonbornPresence VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
    GIT_TAG v3.7.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CommonLibSSE)

add_commonlibsse_plugin(DragonbornPresence
    SOURCES
        main.cpp
        DragonbornPresence.cpp
        AdditionalFunctions.cpp
    NAME "DragonbornPresence"
    VERSION "2.0.0"
    COMPATIBLE_RUNTIMES "1.6.323.0"
)

target_include_directories(DragonbornPresence PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/discord_rpc/include"
)

target_link_libraries(DragonbornPresence PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/discord_rpc/lib/discord-rpc.lib"
)
