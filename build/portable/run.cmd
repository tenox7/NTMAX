@echo off
cd /d "%~dp0"
bin\windows-x86_64\gxemul.exe -e 3max -X -j osloader -d ntmax-disk.img -d c4: %*
