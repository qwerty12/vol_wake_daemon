#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include "binder_ndk_compat.h"

#include "is_interactive.h"

/*
 * VERIFY for your target build:
 *   adb pull /system/framework/framework.jar .
 *   jadx framework.jar
 *   grep -R "TRANSACTION_isInteractive" out/sources/android/os/IPowerManager*.java
 */
#define TRANSACTION_isInteractive (FIRST_CALL_TRANSACTION + 14)

#define IPOWERMANAGER_DESCRIPTOR "android.os.IPowerManager"

static AIBinder *g_power_manager = NULL;
static AIBinder_Class *g_clazz = NULL;
static AIBinder_DeathRecipient *g_death_recipient = NULL;

static void* stub_onCreate(void *args) { return args; }
static void  stub_onDestroy(__unused void *userData) {}
static binder_status_t stub_onTransact(__unused AIBinder *binder, __unused transaction_code_t code, __unused const AParcel *in, __unused AParcel *out)
{
    return STATUS_UNKNOWN_TRANSACTION;
}

static void service_died(__unused void *cookie)
{
    if (g_power_manager) {
        AIBinder_decStrong(g_power_manager);
        g_power_manager = NULL;
    }
}

bool ConnectPowerService(void)
{
    if (!g_clazz) {
        g_clazz = AIBinder_Class_define(IPOWERMANAGER_DESCRIPTOR, stub_onCreate, stub_onDestroy, stub_onTransact);
        if (__predict_false(!g_clazz))
            return false;
    }

    AIBinder *binder = AServiceManager_getService("power");
    if (__predict_false(!binder))
        return false;

    if (__predict_false(!AIBinder_associateClass(binder, g_clazz))) {
        AIBinder_decStrong(binder);
        return false;
    }

    g_power_manager = binder;

    if (!g_death_recipient)
        g_death_recipient = AIBinder_DeathRecipient_new(service_died);

    AIBinder_linkToDeath(g_power_manager, g_death_recipient, NULL);

    return true;
}

int IsInteractive(void)
{
    if (__predict_false(!g_power_manager && !ConnectPowerService()))
        return -1;

    AParcel *in = NULL;
    AParcel *out = NULL;

    binder_status_t status = AIBinder_prepareTransaction(g_power_manager, &in);
    if (__predict_false(status != STATUS_OK))
        return -1;

    status = AIBinder_transact(g_power_manager, TRANSACTION_isInteractive, &in, &out, 0);
    if (__predict_false(status != STATUS_OK))
        return -1;

    int32_t exceptionCode = 0;
    AParcel_readInt32(out, &exceptionCode);
    if (__predict_false(exceptionCode != 0)) {
        AParcel_delete(out);
        return -1;
    }

    bool interactive = false;
    AParcel_readBool(out, &interactive);
    AParcel_delete(out);

    return interactive ? 1 : 0;
}
