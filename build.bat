@echo off
rem Reversi (black and white) build script: MinGW-w64 + raylib static link
setlocal
set RAYLIB=%USERPROFILE%\MingW_Dev_Lib

gcc src\main.c src\othello.c src\ai.c src\audio.c src\widgets.c src\scene_menu.c src\scene_game.c ^
    -o blackandwhite.exe ^
    -O3 -ffast-math -Wall -Wextra -std=c2x -m64 ^
    -I"%RAYLIB%\include" -Isrc ^
    -L"%RAYLIB%\lib" -s ^
    -lraylib -lopengl32 -lgdi32 -lwinmm -lm ^
    -mwindows

if %errorlevel% equ 0 (
    echo Build OK: blackandwhite.exe
) else (
    echo Build FAILED
    exit /b 1
)
