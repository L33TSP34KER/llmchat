cmake_minimum_required(VERSION 3.16)
project(llmchat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(NCURSES REQUIRED ncursesw)
pkg_check_modules(JSON REQUIRED nlohmann_json)

add_executable(llmchat
    src/main.cpp
    src/config.cpp
    src/http_client.cpp
    src/conversation.cpp
    src/llm/client.cpp
    src/llm/provider.cpp
    src/llm/tool_exec.cpp
    src/llm/persistent_shell.cpp
    src/mcp/client.cpp
    src/mcp/manager.cpp
    src/ui/chat_ui.cpp
    src/ui/renderer.cpp
    src/ui/animation.cpp
    src/ui/text_util.cpp
    src/ui/markdown.cpp
)

target_include_directories(llmchat PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${NCURSES_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    src
)

target_link_directories(llmchat PRIVATE
    ${CURL_LIBRARY_DIRS}
    ${NCURSES_LIBRARY_DIRS}
)

target_link_libraries(llmchat PRIVATE
    ${CURL_LIBRARIES}
    ${NCURSES_LIBRARIES}
    pthread
)

target_compile_options(llmchat PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(test_llmchat
    test/test_flow.cpp
    src/config.cpp
    src/http_client.cpp
    src/conversation.cpp
    src/llm/tool_exec.cpp
    src/llm/persistent_shell.cpp
    src/llm/provider.cpp
)

target_include_directories(test_llmchat PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    src
)

target_link_directories(test_llmchat PRIVATE
    ${CURL_LIBRARY_DIRS}
)

target_link_libraries(test_llmchat PRIVATE
    ${CURL_LIBRARIES}
    pthread
)

target_compile_options(test_llmchat PRIVATE -Wall -Wextra -Wno-unused-parameter)
