@echo off
REM Shalom 연동 SDK - Windows 빌드
REM 개발자 명령 프롬프트에서 실행한다.
setlocal
cd /d "%~dp0"
cmake -S . -B build -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
echo.
echo 완료: %CD%\build\Release\shalom_monitor.exe
echo 사용: build\Release\shalom_monitor.exe ^<로봇주소^> [포트]
endlocal
