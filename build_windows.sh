#!/bin/sh
# macOS から Windows 用 exe を作るスクリプト（クロスコンパイル）。
#
# 必要なもの:
#   brew install mingw-w64
#   SDL2 の MinGW 用開発ライブラリ
#     https://github.com/libsdl-org/SDL/releases から
#     SDL2-devel-<version>-mingw.tar.gz を落として展開する
#
# 使い方:
#   SDL2_MINGW=<展開先>/SDL2-2.32.10/x86_64-w64-mingw32 ./build_windows.sh
#
# 出力:
#   dist/windows/ballsim.exe        単体で動く版（DLL 不要。こちらを推奨）
#   dist/windows-dll/ballsim.exe    SDL2.dll を隣に置いて動かす版（保険）

set -e

SDL2_MINGW="${SDL2_MINGW:-./third_party/SDL2-mingw/x86_64-w64-mingw32}"
CXX=x86_64-w64-mingw32-g++
STRIP=x86_64-w64-mingw32-strip

if [ ! -d "$SDL2_MINGW/include/SDL2" ]; then
    echo "SDL2 の MinGW 用ライブラリが見つかりません: $SDL2_MINGW" >&2
    echo "SDL2_MINGW に展開先のパスを指定してください。" >&2
    exit 1
fi

CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -I$SDL2_MINGW/include/SDL2"
SRC="src/Simulation.cpp src/main.cpp"

# SDL2 が内部で必要とする Windows のシステムライブラリ一式
SYSLIBS="-lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 \
         -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid"

# ---- 単体で動く版 ----
# SDL2 も C++ ランタイムも exe に取り込むので、配布は exe 1 個で済む。
# -mwindows でウィンドウアプリにする。起動してもコンソールが開かない。
# --headless の出力は、コマンドプロンプトから起動した場合のみそちらに出る。
mkdir -p dist/windows
$CXX $CXXFLAGS $SRC -o dist/windows/ballsim.exe \
    -static -static-libgcc -static-libstdc++ \
    -L"$SDL2_MINGW/lib" -lmingw32 -lSDL2main "$SDL2_MINGW/lib/libSDL2.a" \
    -mwindows -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va \
    $SYSLIBS
$STRIP dist/windows/ballsim.exe

# ---- SDL2.dll 版 ----
# SDL2 だけを動的にしたいので、import ライブラリをフルパスで直接指定する。
# -lSDL2 と書くと -static のせいで静的ライブラリが選ばれてしまう。
mkdir -p dist/windows-dll
$CXX $CXXFLAGS $SRC -o dist/windows-dll/ballsim.exe \
    -static -static-libgcc -static-libstdc++ \
    -L"$SDL2_MINGW/lib" -lmingw32 -lSDL2main "$SDL2_MINGW/lib/libSDL2.dll.a" \
    -mwindows
$STRIP dist/windows-dll/ballsim.exe
cp "$SDL2_MINGW/bin/SDL2.dll" dist/windows-dll/

# 依存 DLL の確認。Windows 標準以外が出てきたら配布に同梱が要る。
for exe in dist/windows/ballsim.exe dist/windows-dll/ballsim.exe; do
    echo
    echo "=== $exe が必要とする DLL（Windows 標準の CRT を除く）==="
    x86_64-w64-mingw32-objdump -p "$exe" | grep "DLL Name" | sort -u \
        | grep -viE "api-ms-win-crt"
done
echo
ls -lh dist/windows dist/windows-dll
