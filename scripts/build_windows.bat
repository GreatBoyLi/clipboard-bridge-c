@echo off
setlocal

REM 根据脚本位置进入项目根目录。
cd /d "%~dp0.."

REM 必须在 Visual Studio 的 x64 开发者命令提示符中执行。
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

REM 把目标文件和程序统一放到 build 目录。
pushd build
if errorlevel 1 exit /b 1

REM /std:c11：使用 C11。
REM /utf-8：源文件及执行字符集使用 UTF-8。
REM /W4 /WX：开启严格警告，警告视为错误。
REM /MT：静态链接 MSVC 运行库，减少额外运行库依赖。
REM Windows 使用本地原生接口，不编译 Mac 的 .m 文件。
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT ^
  /D_CRT_SECURE_NO_WARNINGS ^
  /D_WIN32_WINNT=0x0601 ^
  /I..\src ^
  ..\src\protocol.c ^
  ..\src\net.c ^
  ..\src\platform_windows.c ^
  ..\src\main.c ^
  /Fe:clipboard-bridge.exe ^
  /link ws2_32.lib user32.lib bcrypt.lib

if errorlevel 1 goto failed

REM 编译协议测试。
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT ^
  /D_CRT_SECURE_NO_WARNINGS ^
  /I..\src ^
  ..\src\protocol.c ^
  ..\tests\test_protocol.c ^
  /Fe:test_protocol.exe

if errorlevel 1 goto failed

REM 编译网络测试。
cl /nologo /std:c11 /utf-8 /W4 /WX /O2 /MT ^
  /D_CRT_SECURE_NO_WARNINGS ^
  /D_WIN32_WINNT=0x0601 ^
  /I..\src ^
  ..\src\net.c ^
  ..\tests\test_net.c ^
  /Fe:test_net.exe ^
  /link ws2_32.lib

if errorlevel 1 goto failed

REM 编译完成后执行不访问系统剪贴板的测试。
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
exit /b 1
