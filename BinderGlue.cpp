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

bool ConnectInputService(void)
{
    const sp<IServiceManager> sm = defaultServiceManager();
    if (__predict_false(!sm))
        return false;

    input = sm->getService(String16("input"));
    if (__predict_false(!input))
        return false;

    (void)input->linkToDeath(gDeathNotifier);

    if (args.empty()) {
        args.add(String16("keyevent"));
        args.add(String16("KEYCODE_WAKEUP"));
    }

    return true;
}

void WakeUpScreen(void)
{
    if (__predict_false(!input && !ConnectInputService()))
        return;

    (void)IBinder::shellCommand(input, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, args, nullptr, nullptr);
}
