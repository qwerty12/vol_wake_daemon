@echo off
setlocal
pushd "%~dp0"
call "C:\android-ndk-r27d\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang++.cmd" -s -pie -Wall -O3 -fno-rtti -fno-exceptions -nostdlib++ -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -Wl,--gc-sections -DDO_NOT_CHECK_MANUAL_BINDER_INTERFACES -DANDROID -DNDEBUG -isystem ./extra_ndk/include -L./extra_ndk/lib64 -lc++ -lutils -lbinder -o vol_wake_daemon vol_wake_daemon.c BinderGlue.cpp IsInteractive.cpp
set "ERRORLEVEL1=%ERRORLEVEL%"
popd
exit /b %ERRORLEVEL1%