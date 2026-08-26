@echo off
setlocal
pushd "%~dp0"
set "EXTRA_DEFINES="
if exist "extra_ndk\lib64\libbinder_ndk.so" set "EXTRA_DEFINES=-DHAVE_LOCAL_LIBBINDER_NDK"
call "C:\android-ndk-r27d\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.cmd" -s -pie -Wall -Wextra -O3 -mcpu=cortex-a55 -mtune=cortex-a77 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -fomit-frame-pointer -momit-leaf-frame-pointer -Wl,--gc-sections -DANDROID -DNDEBUG %EXTRA_DEFINES% -isystem ./extra_ndk/include -L./extra_ndk/lib64 -lbinder_ndk -Wl,--as-needed -Wl,--exclude-libs,ALL -o vol_wake_daemon vol_wake_daemon.c binder_glue.c is_interactive.c binder_ndk_compat.c
set "ERRORLEVEL1=%ERRORLEVEL%"
popd
exit /b %ERRORLEVEL1%