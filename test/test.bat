@echo off
rem Engine unit test
setlocal
gcc test\test_engine.c src\othello.c -o test_engine.exe -O2 -Wall -Wextra -std=c2x -Isrc
if %errorlevel% neq 0 exit /b 1
.\test_engine.exe
