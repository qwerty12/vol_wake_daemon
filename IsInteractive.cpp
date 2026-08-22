/*
 *   adb pull /system/framework/framework.jar .
 *   jadx framework.jar
 *   grep -R "TRANSACTION_isInteractive" out/sources/android/os/IPowerManager*.java
 */

#include <sys/cdefs.h>
#include <utils/String16.h>
#include <binder/Parcel.h>
#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <binder/IServiceManager.h>

#include "IsInteractive.h"

using namespace android;

class IPowerManager: public IInterface
{
    public:
        DECLARE_META_INTERFACE(PowerManager);

        enum {
            ISINTERACTIVE = IBinder::FIRST_CALL_TRANSACTION + 14,
        };

        virtual bool isInteractive(Parcel *reply) = 0;
};

class BpPowerManager: public BpInterface<IPowerManager>
{
    public:
        explicit BpPowerManager(const sp<IBinder> &impl)
            : BpInterface<IPowerManager>(impl)
        {
        }

        virtual bool isInteractive(Parcel *reply) override
        {
            Parcel data;
            data.writeInterfaceToken(IPowerManager::getInterfaceDescriptor());
            return remote()->transact(ISINTERACTIVE, data, reply) == OK;
        }
};
IMPLEMENT_META_INTERFACE(PowerManager, "android.os.IPowerManager");

static sp<IPowerManager> power_manager = nullptr;

class PowerManagerDeathRecipient : public IBinder::DeathRecipient
{
    public:
        virtual void binderDied(const wp<IBinder> &who) override
        {
            power_manager = nullptr;
        }
};
const static sp<PowerManagerDeathRecipient> gDeathNotifier(new PowerManagerDeathRecipient);

bool ConnectPowerService(void)
{
    const sp<IServiceManager> sm = defaultServiceManager();
    if (__predict_false(!sm))
        return false;

    const sp<IBinder> binder = sm->getService(String16("power"));
    if (__predict_false(!binder))
        return false;

    power_manager = interface_cast<IPowerManager>(binder);
    if (__predict_false(!power_manager))
        return false;

    (void)binder->linkToDeath(gDeathNotifier);

    return true;
}

int IsInteractive(void)
{
    Parcel reply;

    if (__predict_false(!power_manager && !ConnectPowerService()))
        return -1;

    if (__predict_false(!power_manager->isInteractive(&reply)))
        return -1;

    const int32_t status = reply.readInt32();
    if (__predict_false(status != OK))
        return -1;

    return reply.readBool() ? 1 : 0;
}
