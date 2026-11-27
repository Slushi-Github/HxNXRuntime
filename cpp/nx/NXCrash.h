/*
 * NXCrash - minimal crash handler for Nintendo Switch homebrew.
 *
 * Horizon OS never reports crashes to userland as POSIX signals, so a
 * bad memory access ends in the console's fatal-error dialog and no code of
 * ours runs. Everything that *is* catchable in-process (std::terminate,
 * abort(), hxcpp critical errors, errors returned by hxRunLibrary) is handled
 * here: the Haxe call stack and a native backtrace are shown on screen and,
 * when the application declared a folder, written to the SD card. The report
 * also names the thread that reported (main or worker, its id, TLS, stack
 * pointer and stack region), because those failures can come from any thread.
 * The process then ends with svcExitProcess(), which hbmenu reports as a normal
 * exit instead of the crash dialog.
 *
 * The text console and the EGL/GLES window cannot own the display at the same
 * time, so showing the report shuts SDL's video subsystem down first.
 */

#ifndef NX_CRASH_H
#define NX_CRASH_H

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Installs the handlers. crashDir is the folder that receives one file per
 * crash as soon as one happens, e.g. "sdmc:/switch/Mode/nativeCrashes"; it is
 * created if missing (parents included). Pass NULL to write nothing
 * automatically and let the user save the report from the crash screen with
 * MINUS, which writes it to
 * "sdmc:/switch/HxNXRuntime/nativeCrashes/crash_<date>_<time>.log".
 * Safe to call more than once.
 */
void nxCrashInstall (const char *crashDir);

/* 
 * Program Name shown as "Program:" on the crash screen. Defaults to "Haxe program". 
 */
void nxCrashSetProgram (const char *program);

/** 
 * Extra tag shown as "Extras: a, b, c". Call once per entry (e.g. the target
 * handler name). The line is omitted when nothing was added. 
 */
void nxCrashAddAddon (const char *addon);

/** 
 * Records the step that is running, so a later report says where it was. 
 */
void nxCrashSetPhase (const char *phase);

/**
 * Labels the calling thread. The report always says whether the crash came
 * from the main thread or a worker, with its id, TLS, stack pointer and stack
 * region; this adds a readable name (e.g. "asset-loader") for the threads the
 * application creates. Optional, safe from any thread, and a no-op when the
 * thread id cannot be read.
 */
void nxCrashSetThreadName (const char *name);

/* 
 * Builds the report, shows it on screen and exits the process cleanly. Does
 * not return. 
 */
void nxCrashReport (const char *reason);

/** 
 * Marks the run as finished normally. Logs are one-per-crash, so a clean run
 * writes nothing. 
 */
void nxCrashFinish (void);

/**
 * Registers the application's own shutdown, the same code its normal exit path
 * runs. It is called right before the
 * process hands control back to the loader, so a crash leaves no more state
 * behind than a clean exit would. Pass NULL to clear it. 
 */
void nxCrashSetShutdownHook (void (*hook) (void));

#ifdef __cplusplus
}
#endif

#endif
