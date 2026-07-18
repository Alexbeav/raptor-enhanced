@echo off
REM Adds Delta Sector to the Raptor data files in the folder ABOVE this one
REM (i.e. copy this folder into your Raptor folder, then double-click).
cd /d "%~dp0"
python install_delta_sector.py "%~dp0.."
echo.
pause
