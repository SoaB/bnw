
SET RAYLIB=%USERPROFILE%\MingW_Dev_Lib

gcc src/*.c -o game.exe -O3 -ffast-math -Wall -std=c2x -Wextra -m64 -I"%RAYLIB%\include" -L"%RAYLIB%\lib" -s -lraylibdll -lole32 -lwinmm -lgdi32 -luser32 -lcomdlg32 -lopengl32 -lwinmm -lm -mwindows