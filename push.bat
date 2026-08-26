@echo off
setlocal
pushd "%~dp0"
if not exist vol_wake_daemon (call make.bat || goto :end)
adb shell "killall vol_wake_daemon 2>/dev/null"
adb push vol_wake_daemon /data/local/tmp/ || goto :end
adb shell "chmod 755 /data/local/tmp/vol_wake_daemon && exec /data/local/tmp/vol_wake_daemon"
:end
popd