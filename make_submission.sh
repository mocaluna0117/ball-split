#!/bin/sh
# 提出用フォルダ Submission/A2 の中身を作り直す。
# 動画ファイルなど、自分で足したものには触らない。
set -e

DOC_SRC="${DOC_SRC:-doc/document.md}"
OUT=Submission/A2

mkdir -p "$OUT/fallback"

cp dist/windows/ballsim.exe          "$OUT/"
cp dist/windows-dll/ballsim.exe      "$OUT/fallback/"
cp dist/windows-dll/SDL2.dll         "$OUT/fallback/"
cp shots/start.png                   "$OUT/screen_start.png"
cp shots/finished.png                "$OUT/screen_finished.png"

if [ -f "$DOC_SRC" ]; then
    pandoc "$DOC_SRC" -o "$OUT/document.docx"
    pandoc "$DOC_SRC" -s -o "$OUT/document.html"
else
    echo "説明資料の元ファイルが無いので docx / html は更新しません: $DOC_SRC" >&2
fi

echo
find "$OUT" -type f | sort
