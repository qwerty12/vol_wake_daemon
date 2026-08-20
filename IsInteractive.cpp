/*
 *   adb pull /system/framework/framework.jar .
 *   jadx framework.jar
 *   grep -R "TRANSACTION_isInteractive" out/sources/android/os/IPowerManager*.java
 */

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

static bool connectPowerService()
{
    const sp<IServiceManager> sm = defaultServiceManager();
    if (!sm)
        return false;

    const sp<IBinder> binder = sm->getService(String16("power"));
    if (!binder)
        return false;

    power_manager = interface_cast<IPowerManager>(binder);
    if (!power_manager)
        return false;

    binder->linkToDeath(gDeathNotifier);

    return true;
}

int IsInteractive()
{
    Parcel reply;

    if (!power_manager && !connectPowerService())
        return -1;

    if (!power_manager->isInteractive(&reply))
        return -1;

    int32_t status = reply.readInt32();
    if (status != OK)
        return -1;

    return reply.readBool() ? 1 : 0;
}
