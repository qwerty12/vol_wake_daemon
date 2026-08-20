#include <stdlib.h>
#include <sys/cdefs.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IResultReceiver.h>
#include <binder/IShellCallback.h>
#include <utils/String16.h>
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>

#include "BinderGlue.h"

using namespace android;

static sp<IBinder> input = nullptr;
static Vector<String16> args;

class InputDeathRecipient : public IBinder::DeathRecipient
{
    public:
        virtual void binderDied(const wp<IBinder> &who) override
        {
            input = nullptr;
        }
};
const static sp<InputDeathRecipient> gDeathNotifier(new InputDeathRecipient);

void OnBinderReadReady()
{
    IPCThreadState::self()->handlePolledCommands();
}

int SetupBinderOrCrash()
{
    int binder_fd = -1;
    ProcessState::self()->setThreadPoolMaxThreadCount(0);
    IPCThreadState::self()->disableBackgroundScheduling(true);
    const int err = IPCThreadState::self()->setupPolling(&binder_fd);
    if (err) {
        fprintf(stderr, "Error setting up binder polling: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }

    if (binder_fd < 0) {
        fprintf(stderr, "Invalid binder FD: %d\n", binder_fd);
        exit(EXIT_FAILURE);
    }

    return binder_fd;
}

static bool connectInputService()
{
    const sp<IServiceManager> sm = defaultServiceManager();
    if (!sm)
        return false;

    input = sm->getService(String16("input"));
    if (!input)
        return false;

    input->linkToDeath(gDeathNotifier);

    return true;
}

void wakeUpScreen()
{
    if (args.empty())
    {
        args.add(String16("keyevent"));
        args.add(String16("KEYCODE_WAKEUP"));
    }

    if (__predict_false(!input && !connectInputService()))
        return;

    IBinder::shellCommand(input, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, args, nullptr, nullptr);
}
