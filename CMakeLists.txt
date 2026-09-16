cmake_minimum_required(VERSION 3.20)
project(ballsim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 単一構成のジェネレータ（Makefile / Ninja）で -DCMAKE_BUILD_TYPE の指定を忘れると
# 最適化なしでビルドされ、数倍遅くなる。既定を Release にしておく。
# Visual Studio は複数構成なので --config Release で指定する。
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# WIN32 を付けるとウィンドウアプリになり、起動時にコンソールが開かない。
# macOS / Linux では無視される。
add_executable(ballsim WIN32
    src/main.cpp
    src/Simulation.cpp
    src/Simulation.h
    src/Font.h
)

# --------------------------------------------------------------------------
# SDL2 の探し方は 2 通り。
#
#  A) vcpkg を使う場合（何も指定しなくてよい）
#       vcpkg install sdl2:x64-windows
#       cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#
#  B) vcpkg を使わない場合
#       https://github.com/libsdl-org/SDL/releases から
#       SDL2-devel-<version>-VC.zip を落として展開し、その場所を渡す。
#       cmake -B build -S . -DSDL2_ROOT=C:/SDL2-2.32.10
# --------------------------------------------------------------------------

set(SDL2_ROOT "" CACHE PATH "SDL2-devel-VC を展開したフォルダ（vcpkg を使う場合は不要）")

if(SDL2_ROOT)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_sdl_libdir "${SDL2_ROOT}/lib/x64")
    else()
        set(_sdl_libdir "${SDL2_ROOT}/lib/x86")
    endif()

    if(NOT EXISTS "${SDL2_ROOT}/include/SDL.h")
        message(FATAL_ERROR "SDL2_ROOT が正しくありません: ${SDL2_ROOT}/include/SDL.h が見つかりません")
    endif()

    target_include_directories(ballsim PRIVATE "${SDL2_ROOT}/include")
    target_link_libraries(ballsim PRIVATE
        "${_sdl_libdir}/SDL2main.lib"
        "${_sdl_libdir}/SDL2.lib")

    # SDL2.dll を exe の隣に置く
    add_custom_command(TARGET ballsim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_sdl_libdir}/SDL2.dll" $<TARGET_FILE_DIR:ballsim>)
else()
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        find_package(SDL2 REQUIRED)
    endif()

    if(TARGET SDL2::SDL2main)
        target_link_libraries(ballsim PRIVATE SDL2::SDL2main)
    endif()

    if(TARGET SDL2::SDL2)
        target_link_libraries(ballsim PRIVATE SDL2::SDL2)
        add_custom_command(TARGET ballsim POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:ballsim>)
    else()
        target_include_directories(ballsim PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ballsim PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

if(MSVC)
    # ソースは UTF-8。/utf-8 が無いと日本語コメントで警告が出る
    target_compile_options(ballsim PRIVATE /W4 /utf-8)
    # freopen を使うため。安全版に置き換えると MinGW と書き分けが要る
    target_compile_definitions(ballsim PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT ballsim)
else()
    target_compile_options(ballsim PRIVATE -Wall -Wextra)
endif()
