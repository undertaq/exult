@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
cd /d D:\Project\exult2
MSBuild msvcstuff\vs2026\Exult.sln /p:Configuration=Release /p:Platform=x64 /t:Exult /m