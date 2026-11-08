cmake_minimum_required(VERSION 3.20)
project(DynamicWindows VERSION 0.1.0 LANGUAGES C RC)

if(NOT MINGW)
    message(FATAL_ERROR "Build with MinGW-w64 GCC; its GDI+ headers expose the flat C API.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(APP_SOURCES src/main.c src/render.c src/platform.c src/media.c src/accessibility.c src/config.c src/settings.c src/headphones.c src/notifications.c src/crab.c)
set(CHECK_SOURCES ${APP_SOURCES} src/island.c tests/test_island.c tests/test_config.c tests/media_publisher.c tests/test_headphones.c tests/test_notifications.c tests/test_crab.c)
set(CHECK_FLAGS -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes)
set(APP_DEFINES UNICODE _UNICODE COBJMACROS WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

add_library(island_core STATIC src/island.c)
target_include_directories(island_core PUBLIC src)
target_compile_options(island_core PRIVATE ${CHECK_FLAGS})

add_executable(dynamicwindows WIN32 ${APP_SOURCES} resources/dynamicwindows.rc)
target_compile_definitions(dynamicwindows PRIVATE ${APP_DEFINES})
target_compile_options(dynamicwindows PRIVATE ${CHECK_FLAGS})
target_link_options(dynamicwindows PRIVATE -municode -static -static-libgcc)
target_link_libraries(dynamicwindows PRIVATE island_core gdiplus ole32 oleaut32 uuid oleacc shell32 shlwapi
                                     comctl32 comdlg32 runtimeobject shcore user32 gdi32 cfgmgr32
                                     winhttp credui sapi)
set_target_properties(dynamicwindows PROPERTIES OUTPUT_NAME DynamicWindows)

include(CTest)
if(BUILD_TESTING)
    add_executable(island_tests tests/test_island.c)
    target_compile_options(island_tests PRIVATE ${CHECK_FLAGS})
    target_link_options(island_tests PRIVATE -static -static-libgcc)
    target_link_libraries(island_tests PRIVATE island_core)
    add_test(NAME core COMMAND island_tests)
    add_executable(config_tests tests/test_config.c src/config.c)
    target_compile_definitions(config_tests PRIVATE ${APP_DEFINES})
    target_compile_options(config_tests PRIVATE ${CHECK_FLAGS})
    target_link_options(config_tests PRIVATE -static -static-libgcc)
    target_link_libraries(config_tests PRIVATE island_core shell32 ole32 uuid)
    add_test(NAME configuration COMMAND config_tests)
    add_executable(headphone_tests tests/test_headphones.c src/headphones.c)
    target_include_directories(headphone_tests PRIVATE src)
    target_compile_definitions(headphone_tests PRIVATE ${APP_DEFINES})
    target_compile_options(headphone_tests PRIVATE ${CHECK_FLAGS})
    target_link_options(headphone_tests PRIVATE -static -static-libgcc)
    target_link_libraries(headphone_tests PRIVATE ole32 uuid cfgmgr32 user32)
    add_test(NAME headphones COMMAND headphone_tests)
    add_executable(notification_tests tests/test_notifications.c src/notifications.c)
    target_include_directories(notification_tests PRIVATE src)
    target_compile_definitions(notification_tests PRIVATE ${APP_DEFINES})
    target_compile_options(notification_tests PRIVATE ${CHECK_FLAGS})
    target_link_options(notification_tests PRIVATE -static -static-libgcc)
    target_link_libraries(notification_tests PRIVATE ole32 runtimeobject user32)
    add_test(NAME notifications COMMAND notification_tests)
    add_executable(crab_tests tests/test_crab.c src/crab.c)
    target_include_directories(crab_tests PRIVATE src)
    target_compile_definitions(crab_tests PRIVATE ${APP_DEFINES})
    target_compile_options(crab_tests PRIVATE ${CHECK_FLAGS})
    target_link_options(crab_tests PRIVATE -static -static-libgcc)
    target_link_libraries(crab_tests PRIVATE ole32 uuid user32 advapi32 winhttp credui sapi
                                             runtimeobject)
    add_test(NAME crab COMMAND crab_tests)
    add_executable(media_publisher tests/media_publisher.c)
    target_compile_definitions(media_publisher PRIVATE ${APP_DEFINES})
    target_compile_options(media_publisher PRIVATE ${CHECK_FLAGS})
    target_link_options(media_publisher PRIVATE -municode -static -static-libgcc)
    target_link_libraries(media_publisher PRIVATE ole32 uuid shell32 runtimeobject)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/smoke")
    add_test(NAME native_smoke COMMAND dynamicwindows --smoke-test "${CMAKE_BINARY_DIR}/smoke")
    set_tests_properties(native_smoke PROPERTIES TIMEOUT 20)
endif()

add_custom_target(typecheck
    COMMAND "${CMAKE_C_COMPILER}" -std=c11 ${CHECK_FLAGS}
            -DUNICODE -D_UNICODE -DCOBJMACROS -DWIN32_LEAN_AND_MEAN -DNOMINMAX
            -D_WIN32_WINNT=0x0A00 -Isrc -fsyntax-only ${CHECK_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM)

find_program(CLANG_FORMAT clang-format REQUIRED)
file(GLOB FORMAT_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c"
     "${CMAKE_SOURCE_DIR}/src/*.h" "${CMAKE_SOURCE_DIR}/tests/*.c")
add_custom_target(lint
    COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${FORMAT_FILES}
    VERBATIM)

install(TARGETS dynamicwindows RUNTIME DESTINATION .)
install(FILES README.md LICENSE resources/defaults.ini DESTINATION .)
