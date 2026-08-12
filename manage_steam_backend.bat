@echo off
setlocal
cd /d "%~dp0"
where pyw.exe >nul 2>nul
if %errorlevel%==0 (
    start "" pyw.exe -3 "%~dp0services\leaderboard_verifier\deployment_manager_gui.py"
) else (
    start "" pythonw.exe "%~dp0services\leaderboard_verifier\deployment_manager_gui.py"
)
endlocal
