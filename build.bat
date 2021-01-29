@echo off

if not exist bin\win mkdir bin\win
if not defined DevEnvDir call vcvars64

call cl /Isource\third_party\include source\equirectangular_to_cube.c /Febin\win\gen_cubemap.exe
set /A retcode=%errorlevel%

if exist equirectangular_to_cube.obj del equirectangular_to_cube.obj
if exist vc140.pdb      del vc140.pdb

exit /b %retcode%
