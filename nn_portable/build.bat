@echo off
:: Try MSVC first, fallback to GCC/MinGW
where cl.exe >nul 2>nul
if %errorlevel% equ 0 (
    cl /nologo /O2 /W3 main.c nn.c /Fe:nn_demo.exe
    goto :done
)
where gcc.exe >nul 2>nul
if %errorlevel% equ 0 (
    gcc -Wall -O2 main.c nn.c -o nn_demo.exe -lm
    goto :done
)
echo No C compiler found. Install MSVC or MinGW-GCC.
:done
echo Done.
