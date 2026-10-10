@echo off
setlocal

REM Locate the project from this script, regardless of the current directory.
cd /d "%~dp0.."

REM Use the Visual Studio x64 Native Tools Command Prompt.
where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] MSVC compiler not found.
    echo Open the x64 Native Tools Command Prompt for Visual Studio.
    exit /b 1
)

if not exist build mkdir build
if not exist build (
    echo [ERROR] Cannot create build directory.
    exit /b 1
)

REM Place all objects and executables in the build directory.
pushd build
if errorlevel 1 exit /b 1

REM /std:c11: compile C11 sources.
REM /utf-8: interpret C source and execution strings as UTF-8.
REM /W4 /WX: treat compiler warnings as errors.
REM /MT: use the static MSVC runtime.
REM Compile native Windows C sources; do not compile macOS .m files.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT ^
  /D_CRT_SECURE_NO_WARNINGS ^
  /D_WIN32_WINNT=0x0601 ^
  /I..\src ^
  ..\src\protocol.c ^
  ..\src\net.c ^
  ..\src\content.c ^
  ..\src\files.c ^
  ..\src\image_windows.c ^
  ..\src\platform_windows.c ^
  ..\src\scroll_windows.c ^
  ..\src\caps_windows.c ^
  ..\src\main.c ^
  /Fe:clipboard-bridge.exe ^
  /link ws2_32.lib user32.lib bcrypt.lib ole32.lib windowscodecs.lib gdi32.lib shell32.lib

if errorlevel 1 goto failed

REM File stream roundtrip uses a private test directory; no clipboard access.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT /D_CRT_SECURE_NO_WARNINGS /I..\src ^
  ..\src\protocol.c ..\src\files.c ..\tests\test_files.c ^
  /Fe:test_files.exe /link user32.lib bcrypt.lib
if errorlevel 1 goto failed
test_files.exe
if errorlevel 1 goto failed

REM Native CF_HDROP conversion only; never opens the system clipboard.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT /D_CRT_SECURE_NO_WARNINGS /I..\src ^
  ..\src\protocol.c ..\src\content.c ..\src\image_windows.c ..\tests\test_files_windows.c ^
  /Fe:test_files_windows.exe /link user32.lib bcrypt.lib ole32.lib windowscodecs.lib gdi32.lib shell32.lib
if errorlevel 1 goto failed
test_files_windows.exe
if errorlevel 1 goto failed

REM Build protocol tests.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT ^
  /D_CRT_SECURE_NO_WARNINGS ^
  /I..\src ^
  ..\src\protocol.c ^
  ..\tests\test_protocol.c ^
  /Fe:test_protocol.exe

if errorlevel 1 goto failed

REM Image bounds and compound protocol tests do not access the clipboard.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT /I..\src ^
  ..\src\protocol.c ..\src\content.c ..\tests\test_content.c /Fe:test_content.exe
if errorlevel 1 goto failed
test_content.exe
if errorlevel 1 goto failed
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT /I..\src ^
  ..\tests\test_windows_dib.c /Fe:test_windows_dib.exe
if errorlevel 1 goto failed
test_windows_dib.exe
if errorlevel 1 goto failed

REM Native WIC in-memory roundtrip; does not open the system clipboard.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT /I..\src ^
  ..\src\protocol.c ..\src\content.c ..\tests\test_image_windows.c ^
  /Fe:test_image_windows.exe /link ole32.lib windowscodecs.lib gdi32.lib user32.lib
if errorlevel 1 goto failed
test_image_windows.exe
if errorlevel 1 goto failed

REM Caps hold state and mocked Windows injection tests, no actual keyboard input.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT /I..\src ^
  ..\tests\test_caps_hold.c /Fe:test_caps_hold.exe
if errorlevel 1 goto failed
test_caps_hold.exe
if errorlevel 1 goto failed
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT /I..\src ^
  ..\tests\test_caps_windows.c /Fe:test_caps_windows.exe
if errorlevel 1 goto failed
test_caps_windows.exe
if errorlevel 1 goto failed

REM Build network tests.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT ^
  /D_CRT_SECURE_NO_WARNINGS ^
  /D_WIN32_WINNT=0x0601 ^
  /I..\src ^
  ..\src\net.c ^
  ..\tests\test_net.c ^
  /Fe:test_net.exe ^
  /link ws2_32.lib

if errorlevel 1 goto failed

REM Test line mapping and recovery without changing system settings.
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT ^
  /I..\src ..\tests\test_scroll_lines.c /Fe:test_scroll_lines.exe
if errorlevel 1 goto failed
test_scroll_lines.exe
if errorlevel 1 goto failed

REM Run tests that do not access the real clipboard.
test_protocol.exe
if errorlevel 1 goto failed

test_net.exe
if errorlevel 1 goto failed

popd
echo [OK] Windows build and tests completed.
echo [OK] Program: build\clipboard-bridge.exe
exit /b 0

:failed
popd
echo [ERROR] Build or test failed. Check the output above.
echo [HINT] For LNK1104 on clipboard-bridge.exe, stop the running bridge first.
echo [HINT] Wait for its scroll guard to exit and restore settings before rebuilding.
exit /b 1
