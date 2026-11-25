/**
 * @file NXPerformance.cpp
 * @brief Official Nintendo Switch performance control (apm:am + applet).
 *
 * See NXPerformance.h. This file is always part of the NX build; the
 * HXCPP_NX_NO_OVERCLOCK check lives here and swaps the real implementation for
 * stubs, so user code can keep calling the same symbols in both builds.
 */

#include "NXPerformance.h"

#ifdef HXCPP_NX_NO_OVERCLOCK

// Disabled at build time: the symbols must still exist so Haxe code links,
// but nothing here touches apm, applet or the clocks.
int nx_perf_is_available(void) { return 0; }
int nx_perf_get_operation_mode(void) { return -1; }
int nx_perf_get_performance_mode(void) { return -1; }
unsigned int nx_perf_get_current_configuration(void) { return 0; }
unsigned int nx_perf_get_configuration(int mode) { (void)mode; return 0; }
int nx_perf_set_configuration(int mode, unsigned int configuration) { (void)mode; (void)configuration; return -1; }
int nx_perf_set_cpu_boost(int mode) { (void)mode; return -1; }
int nx_perf_apply(int profile) { (void)profile; return -1; }
int nx_perf_refresh(void) { return 0; }
int nx_perf_applied_profile(void) { return NxPerfProfile_Default; }
void nx_perf_restore(void) {}
void nx_perf_shutdown(void) {}

#else // HXCPP_NX_NO_OVERCLOCK

#include <switch.h>
#include <stdlib.h>

// Official PerformanceConfiguration values used by the profiles. See
// https://switchbrew.org/wiki/PTM_services#PerformanceConfiguration.
#define NX_PERF_DOCKED_NORMAL      0x00010001u // 1020 / 768.0 / 1600.0
#define NX_PERF_DOCKED_BOOST       0x00010001u
#define NX_PERF_HANDHELD_NORMAL    0x00020004u // 1020 / 384.0 / 1331.2
#define NX_PERF_HANDHELD_BOOST     0x92220008u // 1020 / 460.8 / 1331.2
#define NX_PERF_FASTLOAD_DOCKED    0x92220009u // 1785 /  76.8 / 1600.0
#define NX_PERF_FASTLOAD_HANDHELD  0x9222000Au // 1785 /  76.8 / 1331.2
#define NX_PERF_SAVER_DOCKED       0x00020000u // 1020 / 230.4 / 1600.0 (docked only)
#define NX_PERF_SAVER_HANDHELD     0x00020005u // 1020 / 307.2 / 1065.6

static bool sInitialized = false;
static bool sOwned = false;
static bool sSaved = false;
static bool sUsedCpuBoost = false;
static bool sAtexitRegistered = false;

static u32 sSavedNormal = 0;
static u32 sSavedBoost = 0;
static int sAppliedProfile = NxPerfProfile_Default;
static int sAppliedOperationMode = -1;

/** 
 * Initializes apm only when nobody else did (appletInitialize usually does). 
*/
static Result nxPerfEnsureService(void)
{
    if (sInitialized)
        return 0;

    if (!serviceIsActive(apmGetServiceSession()))
    {
        Result rc = apmInitialize();
        if (R_FAILED(rc))
            return rc;
        sOwned = true;
    }

    sInitialized = true;
    return 0;
}

/** 
 * Captures both performance modes exactly once, before the first change. 
 */
static void nxPerfCaptureState(void)
{
    if (sSaved)
        return;

    u32 normal = 0;
    u32 boost = 0;

    if (R_FAILED(apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &normal)))
        normal = 0;
    if (R_FAILED(apmGetPerformanceConfiguration(ApmPerformanceMode_Boost, &boost)))
        boost = 0;

    sSavedNormal = normal;
    sSavedBoost = boost;
    sSaved = true;

    // Safety net: if the app returns from main without calling restore(), the
    // clocks still go back to normal before libnx tears the services down.
    if (!sAtexitRegistered && atexit(nx_perf_restore) == 0)
        sAtexitRegistered = true;
}

int nx_perf_is_available(void)
{
    return R_SUCCEEDED(nxPerfEnsureService()) ? 1 : 0;
}

int nx_perf_get_operation_mode(void)
{
    return (int)appletGetOperationMode();
}

int nx_perf_get_performance_mode(void)
{
    if (R_FAILED(nxPerfEnsureService()))
        return -1;

    ApmPerformanceMode mode = ApmPerformanceMode_Invalid;
    if (R_FAILED(apmGetPerformanceMode(&mode)))
        return -1;

    return (int)mode;
}

u32 nx_perf_get_current_configuration(void)
{
    u32 configuration = 0;

    // applet exposes the active configuration (7.0.0+). Fall back to apm when
    // the applet command is not available on the running firmware.
    if (R_SUCCEEDED(appletGetCurrentPerformanceConfiguration(&configuration)))
        return configuration;

    if (R_FAILED(nxPerfEnsureService()))
        return 0;

    if (R_FAILED(apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &configuration)))
        return 0;

    return configuration;
}

u32 nx_perf_get_configuration(int mode)
{
    if (R_FAILED(nxPerfEnsureService()))
        return 0;

    u32 configuration = 0;
    if (R_FAILED(apmGetPerformanceConfiguration((ApmPerformanceMode)mode, &configuration)))
        return 0;

    return configuration;
}

int nx_perf_set_configuration(int mode, u32 configuration)
{
    Result rc = nxPerfEnsureService();
    if (R_FAILED(rc))
        return rc;

    nxPerfCaptureState();

    rc = apmSetPerformanceConfiguration((ApmPerformanceMode)mode, configuration);
    if (R_SUCCEEDED(rc))
    {
        // A raw write is not a managed profile; do not let refresh() override it.
        sAppliedProfile = NxPerfProfile_Default;
        sAppliedOperationMode = -1;
    }

    return rc;
}

int nx_perf_set_cpu_boost(int mode)
{
    Result rc = nxPerfEnsureService();
    if (R_FAILED(rc))
        return rc;

    nxPerfCaptureState();

    rc = appletSetCpuBoostMode((ApmCpuBoostMode)mode);
    if (R_SUCCEEDED(rc))
        sUsedCpuBoost = ((ApmCpuBoostMode)mode != ApmCpuBoostMode_Normal);

    return rc;
}

int nx_perf_apply(int profile)
{
    if (profile < NxPerfProfile_Default || profile > NxPerfProfile_PowerSaver)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    if (profile == NxPerfProfile_Default)
    {
        nx_perf_restore();
        return 0;
    }

    Result rc = nxPerfEnsureService();
    if (R_FAILED(rc))
        return rc;

    nxPerfCaptureState();

    bool docked = appletGetOperationMode() == AppletOperationMode_Console;

    u32 normal = 0;
    u32 boost = 0;
    ApmCpuBoostMode boostMode = ApmCpuBoostMode_Normal;

    switch (profile)
    {
    case NxPerfProfile_Performance:
        normal = docked ? NX_PERF_DOCKED_NORMAL : NX_PERF_HANDHELD_NORMAL;
        boost = docked ? NX_PERF_DOCKED_BOOST : NX_PERF_HANDHELD_BOOST;
        break;

    case NxPerfProfile_FastLoad:
        normal = docked ? NX_PERF_DOCKED_NORMAL : NX_PERF_HANDHELD_NORMAL;
        boost = docked ? NX_PERF_FASTLOAD_DOCKED : NX_PERF_FASTLOAD_HANDHELD;
        boostMode = ApmCpuBoostMode_FastLoad;
        break;

    case NxPerfProfile_PowerSaver:
        normal = docked ? NX_PERF_SAVER_DOCKED : NX_PERF_SAVER_HANDHELD;
        boost = normal;
        break;

    default:
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    Result rcNormal = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, normal);
    if (R_FAILED(rcNormal))
        return rcNormal;

    // The boost configuration is used by the system if it ever enters boost
    // mode; not being able to set it is not fatal.
    apmSetPerformanceConfiguration(ApmPerformanceMode_Boost, boost);

    if (boostMode != ApmCpuBoostMode_Normal)
    {
        if (R_SUCCEEDED(appletSetCpuBoostMode(boostMode)))
            sUsedCpuBoost = true;
    }
    else if (sUsedCpuBoost)
    {
        // appletCancelCpuBoostMode is 10.0.0+; fall back to the normal mode.
        if (R_FAILED(appletCancelCpuBoostMode()))
            appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        sUsedCpuBoost = false;
    }

    sAppliedProfile = profile;
    sAppliedOperationMode = (int)appletGetOperationMode();
    return 0;
}

int nx_perf_refresh(void)
{
    if (sAppliedProfile == NxPerfProfile_Default)
        return 0;

    if ((int)appletGetOperationMode() == sAppliedOperationMode)
        return 0;

    return nx_perf_apply(sAppliedProfile);
}

int nx_perf_applied_profile(void)
{
    return sAppliedProfile;
}

void nx_perf_restore(void)
{
    if (!sSaved)
    {
        sAppliedProfile = NxPerfProfile_Default;
        sAppliedOperationMode = -1;
        return;
    }

    if (sUsedCpuBoost)
    {
        if (R_FAILED(appletCancelCpuBoostMode()))
            appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        sUsedCpuBoost = false;
    }

    if (sSavedNormal != 0)
        apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, sSavedNormal);
    if (sSavedBoost != 0)
        apmSetPerformanceConfiguration(ApmPerformanceMode_Boost, sSavedBoost);

    sAppliedProfile = NxPerfProfile_Default;
    sAppliedOperationMode = -1;
}

void nx_perf_shutdown(void)
{
    nx_perf_restore();

    if (sOwned)
    {
        apmExit();
        sOwned = false;
    }

    sInitialized = false;
}

#endif // HXCPP_NX_NO_OVERCLOCK
