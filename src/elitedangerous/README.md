# エリート・デンジャラス 日本語化 dinput8.dll のソース

Steam 版 Elite Dangerous（Odyssey 64 ビット版）の文字表示を日本語にする `dinput8.dll` と、その組み立てに使う道具です。

- `src/` … dinput8.dll 本体（C）。Visual Studio 2022 の x64 ツールで `src\build.bat` を実行すると `build\dinput8.dll` ができます（`build.bat test` で試験用）。
- `tools/` … Python の道具。`merge.py` → `make_text.py text/ja/merged.json` → `make_glyphs.py build/chars.txt` → `src\build.bat` の順に使います。`ed.py` は試験用の起動・操作の道具です。

`src/inflate.c` は Mark Adler 氏の puff（zlib の contrib）を元に書き直したもので、このファイルだけは zlib ライセンスです（冒頭に表示があります）。

必要なもの: Python 3（fontTools）、Noto Sans JP、訳の表（このリポジトリには入っていません）。
ゲーム本体は各自で用意してください。
