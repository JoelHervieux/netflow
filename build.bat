@echo off
set PATH=C:\msys64\mingw64\bin;%PATH%
if not exist build mkdir build
cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release .. && mingw32-make -j4
if %ERRORLEVEL% EQU 0 (
    copy /Y "C:\msys64\mingw64\bin\libssh2-1.dll" . >nul 2>&1
    copy /Y "C:\msys64\mingw64\bin\libcrypto-3-x64.dll" . >nul 2>&1
    copy /Y "C:\msys64\mingw64\bin\zlib1.dll" . >nul 2>&1
    echo.
    echo Build OK: build\netflow.exe
) else (
    echo.
    echo BUILD FAILED
)
cd ..
pause
