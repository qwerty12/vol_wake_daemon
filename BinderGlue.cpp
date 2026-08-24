#include <sys/cdefs.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>

#include "BinderGlue.h"

using namespace android;

void OnBinderReadReady(void)
{
    IPCThreadState::self()->handlePolledCommands();
}

int SetupBinder(void)
{
    int binder_fd = -1;

    ProcessState::self()->setThreadPoolMaxThreadCount(0);
    IPCThreadState::self()->disableBackgroundScheduling(true);
    const int err = IPCThreadState::self()->setupPolling(&binder_fd);
    if (__predict_false(err != 0))
        return err;

    return binder_fd;
}
