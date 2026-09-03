@echo off
setlocal
pushd "%~dp0"
set "EXTRA_DEFINES="
if exist "extra_ndk\lib64\libbinder_ndk.so" set "EXTRA_DEFINES=-DHAVE_LOCAL_LIBBINDER_NDK"
call "C:\android-ndk-r27d\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.cmd" -s -pie -Wall -Wextra -Wformat=2 -Werror=format-security -O3 -mcpu=cortex-a55 -mtune=cortex-a55 -flto -fvisibility=hidden -fdata-sections -ffunction-sections -fomit-frame-pointer -momit-leaf-frame-pointer -fno-plt -DANDROID -DNDEBUG %EXTRA_DEFINES% -isystem ./extra_ndk/include -L./extra_ndk/lib64 -lbinder_ndk -Wl,--as-needed -Wl,-z,relro -Wl,-z,now -Wl,-z,pack-relative-relocs -Wl,--exclude-libs,ALL -Wl,-O2 -Wl,--gc-sections -Wl,--icf=all -Wl,--build-id=none -o vol_wake_daemon vol_wake_daemon.c binder_glue.c is_interactive.c binder_ndk_compat.c
set "ERRORLEVEL1=%ERRORLEVEL%"
popd
exit /b %ERRORLEVEL1%