@echo off
setlocal

set "TOOL_ROOT=E:\AMDDesignTools\2025.2"
set "VITIS_RUN=%TOOL_ROOT%\Vitis\bin\vitis-run.bat"
set "VIVADO=%TOOL_ROOT%\Vivado\bin\vivado.bat"

if not exist "%VITIS_RUN%" (
  echo ERROR: not found: %VITIS_RUN%
  exit /b 2
)
if not exist "%VIVADO%" (
  echo ERROR: not found: %VIVADO%
  exit /b 2
)

call "%VITIS_RUN%" --mode hls --tcl run_hls.tcl
if errorlevel 1 exit /b %errorlevel%

call "%VITIS_RUN%" --mode hls --tcl run_cosim.tcl
if errorlevel 1 exit /b %errorlevel%

call "%VIVADO%" -mode batch -nojournal -nolog -source run_vivado_ooc.tcl
exit /b %errorlevel%
