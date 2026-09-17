@echo off
setlocal

if "%~1"=="" (
    echo Usage: run_openfhe_vector_hls.bat path-to-openfhe-vector.bin
    exit /b 2
)

set "CKKS_CT_MULT_VECTOR_FILE=%~f1"
if not exist "%CKKS_CT_MULT_VECTOR_FILE%" (
    echo OpenFHE ciphertext vector not found: %CKKS_CT_MULT_VECTOR_FILE%
    exit /b 2
)

set "VITIS_RUN=E:\AMDDesignTools\2025.2\Vitis\bin\vitis-run.bat"
if not exist "%VITIS_RUN%" (
    echo Vitis runner not found: %VITIS_RUN%
    exit /b 2
)

call "%VITIS_RUN%" --mode hls --tcl run_openfhe_vector_hls.tcl
exit /b %ERRORLEVEL%
