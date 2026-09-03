#include <android/binder_status.h>
#include "binder_ndk_compat.h"

#include "binder_glue.h"

void OnBinderReadReady(void)
{
    ABinderProcess_handlePolledCommands();
}

int SetupBinder(void)
{
    int binder_fd = -1;

    ABinderProcess_setThreadPoolMaxThreadCount(0);

    const binder_status_t err = ABinderProcess_setupPolling(&binder_fd);
    if (__predict_false(err != STATUS_OK))
        return err;

    return binder_fd;
}
