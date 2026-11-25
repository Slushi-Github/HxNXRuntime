/**
 * NXPerformance - Performance configuration for the Nintendo Switch.
 *
 * Only uses the interfaces Nintendo exposes to regular applications:
 *   - apm:am  (apmInitialize / apmSetPerformanceConfiguration)
 *   - applet  (appletGetOperationMode / appletSetCpuBoostMode)
 *
 * This is the same mechanism an
 * official game uses to pick one of the PerformanceConfiguration values.
 *
 * This header is force-included in every generated .cpp by the NX toolchain,
 * so the functions can be called from Haxe with:
 *
 *     untyped __global__.nx_perf_apply(1);
 *
 * exactly like the built-in __hxcpp_* helpers. It deliberately uses plain C
 * types and includes nothing, so it is safe to inject everywhere.
 *
 * The implementation is always compiled. A build with the Haxe define
 * HXCPP_NX_NO_OVERCLOCK keeps these symbols but replaces them with stubs that
 * never touch the hardware.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// High level profiles handled by nx_perf_apply().
typedef enum {
    NxPerfProfile_Default     = 0, //< Restore the configuration captured before the first change.
    NxPerfProfile_Performance = 1, //< Highest official clocks for the current operation mode.
    NxPerfProfile_FastLoad    = 2, //< CPU 1785 MHz / GPU minimum (loading screens).
    NxPerfProfile_PowerSaver  = 3, //< Lowest official clocks for the current operation mode.
} NxPerfProfile;

// Returns 1 when the apm service is usable, 0 otherwise.
int nx_perf_is_available(void);

// Cached AppletOperationMode (0 = handheld, 1 = console/docked).
int nx_perf_get_operation_mode(void);

// Current ApmPerformanceMode (0 = normal, 1 = boost, -1 = error).
int nx_perf_get_performance_mode(void);

// Current active PerformanceConfiguration, or 0 on failure.
unsigned int nx_perf_get_current_configuration(void);

// PerformanceConfiguration for an ApmPerformanceMode, or 0 on failure.
unsigned int nx_perf_get_configuration(int mode);

// Raw apmSetPerformanceConfiguration(). Captures the original state first.
int nx_perf_set_configuration(int mode, unsigned int configuration);

// Raw appletSetCpuBoostMode(). Captures the original state first.
int nx_perf_set_cpu_boost(int mode);

// Applies one of the NxPerfProfile values for the current operation mode.
int nx_perf_apply(int profile);

// Re-applies the active profile if the operation mode changed (dock/undock).
int nx_perf_refresh(void);

// Profile currently applied through nx_perf_apply(), or NxPerfProfile_Default.
int nx_perf_applied_profile(void);

// Restores the configuration captured before the first change.
void nx_perf_restore(void);

// Restores the configuration and closes apm if this module opened it.
void nx_perf_shutdown(void);

#ifdef __cplusplus
}
#endif
