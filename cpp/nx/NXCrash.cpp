/**
 * NXCrash - minimal crash handler for Nintendo Switch homebrew.
 */

#include "NXCrash.h"
#include "NXScreen.h"

#include <stdio.h>
#include <string.h>

#ifdef __SWITCH__

#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <exception>
#include <cxxabi.h>
#include <unwind.h>
#include <sys/stat.h>
#include <switch.h>

#include <hxcpp.h>
#include <hx/Debug.h>
#include <hx/StackContext.h>

#define NX_CRASH_VERSION "1.0.0"
#define NX_CRASH_TITLE "HxNXRuntime -- Crash Handler v" NX_CRASH_VERSION
#define NX_CRASH_RULE "================================================"

#define NX_CRASH_DIR_MAX 480
#define NX_CRASH_PATH_MAX 600
#define NX_CRASH_PHASE_MAX 96
#define NX_CRASH_PROGRAM_MAX 64
#define NX_CRASH_ADDON_MAX 48
#define NX_CRASH_ADDONS 4
#define NX_CRASH_LINES 24
#define NX_CRASH_LINE_MAX 128
#define NX_CRASH_SCREEN_LINES 12
#define NX_CRASH_DEFAULT_DIR "sdmc:/switch/HxNXRuntime/nativeCrashes"

/*
 * How long the crash screen waits for a button before exiting on its own. 
 */
#define NX_CRASH_HOLD_MS 16
#define NX_CRASH_HOLD_ITERATIONS 1500
#define NX_CRASH_HOLD_SECONDS ((NX_CRASH_HOLD_ITERATIONS * NX_CRASH_HOLD_MS) / 1000)

/**
 * Folder where a file is written automatically on crash. Empty means "ask on
 * the crash screen" (MINUS writes to NX_CRASH_DEFAULT_DIR). 
 */
static char s_dir[NX_CRASH_DIR_MAX] = "";

/** 
 * File of the current report. Empty until it is written. 
 */
static char s_path[NX_CRASH_PATH_MAX] = "";

/**
 * Program name shown on the crash screen. Defaults to "Haxe program". 
 */
static char s_program[NX_CRASH_PROGRAM_MAX] = "Haxe program";
static char s_addons[NX_CRASH_ADDONS][NX_CRASH_ADDON_MAX];

/** 
 * Number of active addons. 
 */
static int s_addonCount = 0;

/** 
 * Phase name shown on the crash screen. Defaults to "startup". 
 */
static char s_phase[NX_CRASH_PHASE_MAX] = "startup";

/** 
 * Whether the crash handler has been initialized. 
 */
static bool s_installed = false;

/** 
 * Whether the crash handler is currently reporting a crash. 
 */
static volatile bool s_reporting = false;

/**
 * Whether the crash handler is currently exiting the process. This is set
 * before calling the application's shutdown hook and svcExitProcess(), so
 * the hook can avoid re-entering the crash handler. 
 */
static volatile bool s_exiting = false;

static char s_kind[32] = "";
static char s_reason[NX_CRASH_LINE_MAX] = "";

static char s_haxeLines[NX_CRASH_LINES][NX_CRASH_LINE_MAX];
static int s_haxeCount = 0;
static bool s_haxeAvailable = false;

static void *s_nativeFrames[NX_CRASH_LINES];
static int s_nativeCount = 0;

/** 
 * The application's own shutdown (OpenAL, romfs, nxlink socket...), so a crash
 * cleans up the same things a clean exit does. 
 */
static void (*s_shutdownHook) (void) = NULL;

extern "C" void nx_socket_runtime_exit (void) __attribute__((weak));

struct NXCrashFrames
{
	void **data;
	int count;
	int max;
};

static _Unwind_Reason_Code nxCrashTraceFrame (struct _Unwind_Context *ctx, void *arg)
{
	NXCrashFrames *frames = (NXCrashFrames *) arg;
	uintptr_t ip = _Unwind_GetIP (ctx);

	if (ip != 0 && frames->count < frames->max)
	{
		frames->data[frames->count] = (void *) ip;
		frames->count++;
	}

	return frames->count >= frames->max ? _URC_END_OF_STACK : _URC_NO_REASON;
}

/* Fills s_nativeFrames. The frames are absolute addresses: use
   "addr2line -e App.elf <addr - module base>" to symbolise them. */
static void nxCrashCaptureNative (void)
{
	NXCrashFrames frames;
	frames.data = s_nativeFrames;
	frames.count = 0;
	frames.max = NX_CRASH_LINES;

	_Unwind_Backtrace (nxCrashTraceFrame, &frames);
	s_nativeCount = frames.count;
}

/**
 * The Haxe stack is the useful one: it names the real program code. hxcpp only has it
 * when compiled with HXCPP_STACK_TRACE (nx/CrashDefines.hx adds it from
 * extraParams.hxml) and only after beginCatch(true), which its critical error
 * path already calls. 
 */
static void nxCrashCaptureHaxe (void)
{
	s_haxeCount = 0;
	s_haxeAvailable = false;

#ifdef HXCPP_STACK_TRACE
	hx::StackContext *ctx = hx::StackContext::getCurrent ();

	if (ctx == NULL)
	{
		return;
	}

	Array<String> lines = Array_obj<String>::__new (0);
	ctx->getCurrentExceptionStackAsStrings (lines);

	for (int i = 0; i < lines->length && s_haxeCount < NX_CRASH_LINES; i++)
	{
		snprintf (s_haxeLines[s_haxeCount], NX_CRASH_LINE_MAX, "%s", lines[i].utf8_str ());
		s_haxeCount++;
	}

	s_haxeAvailable = s_haxeCount > 0;
#endif
}

/**
 * Writes the report to the given path. 
 */
static void nxCrashWriteLog (const char *path)
{
	FILE *out = fopen (path, "w");

	if (out == NULL)
	{
		return;
	}

	fprintf (out, "%s\n", NX_CRASH_TITLE);
	fprintf (out, "%s\n\n", NX_CRASH_RULE);

	fprintf (out, "Program: %s\n", s_program);

	if (s_addonCount > 0)
	{
		fprintf (out, "Extras: ");

		for (int i = 0; i < s_addonCount; i++)
		{
			fprintf (out, "%s%s", i > 0 ? ", " : "", s_addons[i]);
		}

		fprintf (out, "\n");
	}

	fprintf (out, "Phase: %s\n", s_phase);
	fprintf (out, "Kind: %s\n", s_kind);

	if (s_reason[0] != '\0')
	{
		fprintf (out, "Crash by: %s\n", s_reason);
	}

	fprintf (out, "\nHaxe Call Stack:\n");

	if (s_haxeAvailable)
	{
		for (int i = 0; i < s_haxeCount; i++)
		{
			fprintf (out, "  %s\n", s_haxeLines[i]);
		}
	}
	else
	{
		fprintf (out, "  <unavailable>\n");
	}

	fprintf (out, "\nNative Backtrace:\n");

	if (s_nativeCount == 0)
	{
		fprintf (out, "  <none>\n");
	}

	for (int i = 0; i < s_nativeCount; i++)
	{
		fprintf (out, "  #%d %p\n", i, s_nativeFrames[i]);
	}

	fprintf (out, "\n%s\n", NX_CRASH_RULE);
	fprintf (out, "Crash handler status report: \n");

	fflush (out);
	fclose (out);
}

/* 
 * Appends one line to the report file, so the next boot shows how far the
 * screen setup got even if nothing can be drawn. 
 */
static void nxCrashMarker (const char *fmt, ...)
{
	if (s_path[0] == '\0')
	{
		return;
	}

	FILE *out = fopen (s_path, "a");

	if (out == NULL)
	{
		return;
	}

	fprintf (out, "  ");

	va_list args;
	va_start (args, fmt);
	vfprintf (out, fmt, args);
	va_end (args);

	fprintf (out, "\n");
	fflush (out);
	fclose (out);
}

/* NXScreen's diagnostics go to the same crash log as the markers above. */
static void nxCrashScreenLog (const char *message)
{
	nxCrashMarker ("%s", message);
}

/**
 * Creates the crash directory if needed.
 */
static bool nxCrashEnsureDir (const char *dir)
{
	if (dir == NULL || dir[0] == '\0')
	{
		return false;
	}

	if (mkdir (dir, 0777) == 0)
	{
		fsdevCommitDevice ("sdmc");
		return true;
	}

	struct stat info;

	return stat (dir, &info) == 0 && S_ISDIR (info.st_mode);
}

/**
 * Creates the crash file path. 
 * One file per crash: crash_YYYYMMDD_HHMMSS.log, with _2, _3... if two crashes
 * land in the same second. 
 */
static bool nxCrashBuildPath (const char *dir, char *out, size_t outSize)
{
	if (!nxCrashEnsureDir (dir))
	{
		return false;
	}

	time_t now = time (NULL);
	struct tm parts;

	if (localtime_r (&now, &parts) == NULL)
	{
		snprintf (out, outSize, "%s/crash.log", dir);
		return true;
	}

	char base[NX_CRASH_PATH_MAX - 16];
	snprintf (base, sizeof base, "%s/crash_%04d%02d%02d_%02d%02d%02d",
		dir, parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
		parts.tm_hour, parts.tm_min, parts.tm_sec);

	snprintf (out, outSize, "%s.log", base);

	struct stat info;

	for (int i = 2; i < 100 && stat (out, &info) == 0; i++)
	{
		snprintf (out, outSize, "%s_%d.log", base, i);
	}

	return true;
}

/**
 * Captures the Haxe stack trace and the native backtrace.
 */
static void nxCrashBuildReport (const char *kind, const char *reason)
{
	snprintf (s_kind, sizeof s_kind, "%s", kind != NULL ? kind : "fatal");
	snprintf (s_reason, sizeof s_reason, "%s", reason != NULL ? reason : "");

	nxCrashCaptureHaxe ();
	nxCrashCaptureNative ();
}

/**
 * Writes the report to the configured folder and remembers the file name.
 * Called before showing anything, so a failure later still leaves the log.
 */
static void nxCrashSaveReport (void)
{
	if (s_dir[0] == '\0')
	{
		return;
	}

	char path[NX_CRASH_PATH_MAX];

	if (!nxCrashBuildPath (s_dir, path, sizeof path))
	{
		return;
	}

	nxCrashWriteLog (path);
	snprintf (s_path, sizeof s_path, "%s", path);
	fsdevCommitDevice ("sdmc");
}

/**
 * The Haxe stack is what names the game code; the native one is only a
 * fallback for crashes that happen before/outside Haxe. 
 */
static void nxCrashBuildScreen (bool saved, const char *savedPath)
{
	char line[NX_CRASH_LINE_MAX];

	nxScreenClearLines ();

	nxScreenAddLine (NX_CRASH_RULE, NX_RULE);
	nxScreenAddLine (NX_CRASH_TITLE, NX_TITLE);
	nxScreenAddLine (NX_CRASH_RULE, NX_RULE);
	nxScreenAddLine ("", NX_FG);

	nxScreenAddLinef (NX_FG, "Program: %s", s_program);

	if (s_addonCount > 0)
	{
		int used = snprintf (line, sizeof line, "Extras: ");

		for (int i = 0; i < s_addonCount; i++)
		{
			used += snprintf (line + used, sizeof line - used, "%s%s", i > 0 ? ", " : "", s_addons[i]);
		}

		nxScreenAddLine (line, NX_DIM);
	}

	nxScreenAddLinef (NX_FG, "Phase: %s - Type: %s", s_phase, s_kind);

	if (s_reason[0] != '\0')
	{
		nxScreenAddLinef (NX_ACCENT, "Crash by: %s", s_reason);
	}

	nxScreenAddLine ("", NX_FG);

	if (s_haxeAvailable)
	{
		nxScreenAddLine ("Haxe Call Stack:", NX_DIM);

		int shown = s_haxeCount > NX_CRASH_SCREEN_LINES ? NX_CRASH_SCREEN_LINES : s_haxeCount;

		for (int i = 0; i < shown; i++)
		{
			nxScreenAddLinef (NX_STACK, "  %s", s_haxeLines[i]);
		}

		if (s_haxeCount > shown)
		{
			nxScreenAddLinef (NX_DIM, "  ... %d more lines", s_haxeCount - shown);
		}
	}
	else
	{
		nxScreenAddLine ("Haxe Call Stack: <unavailable>", NX_WARN);
		nxScreenAddLine ("Native Backtrace:", NX_DIM);

		int shown = s_nativeCount > NX_CRASH_SCREEN_LINES ? NX_CRASH_SCREEN_LINES : s_nativeCount;

		for (int i = 0; i < shown; i++)
		{
			nxScreenAddLinef (NX_STACK, "  #%d %p", i, s_nativeFrames[i]);
		}

		if (s_nativeCount == 0)
		{
			nxScreenAddLine ("  <none>", NX_DIM);
		}
	}

	nxScreenAddLine ("", NX_FG);
	nxScreenAddLine (NX_CRASH_RULE, NX_RULE);

	if (saved && savedPath[0] != '\0')
	{
		nxScreenAddLine ("Full log saved to:", NX_OK);

		/* Split it: "sdmc:/switch/.../nativeCrashes/crash_....log" does not
		   fit next to the label in an 80 column screen. */
		nxScreenAddLinef (NX_OK, "  %s", savedPath);
	}
	else
	{
		nxScreenAddLinef (NX_WARN, "Press MINUS to save the log to %s", NX_CRASH_DEFAULT_DIR);
	}

	nxScreenAddLine ("Press PLUS to return to hbmenu.", NX_FG);
	nxScreenAddLinef (NX_DIM, "Closes by itself after %d seconds.", NX_CRASH_HOLD_SECONDS);
}

/*
 * Draws the report built by nxCrashBuildScreen() with the shared NXScreen
 * renderer, so the crash screen and the applet-mode notice use one
 * implementation.
 */
static bool nxCrashPresent (bool saved, const char *savedPath)
{
	nxCrashBuildScreen (saved, savedPath);

	return nxScreenPresent ();
}

extern "C" void SDL_Quit (void) __attribute__((weak));

static void nxCrashShowScreen (void)
{
	static bool shown = false;

	if (shown)
	{
		return;
	}

	shown = true;

	/* Run app shutdown BEFORE SDL_Quit() so
	   SDL-backed audio devices (OpenAL-Soft) can close cleanly first. */
	if (s_shutdownHook != NULL)
	{
		nxCrashMarker ("app shutdown");
		s_shutdownHook ();
		s_shutdownHook = NULL;
	}

	/* GL maybe owns the display until SDL tears the window down. Weak symbol:
	   HxNXRuntime is also used by projects that do not link SDL. */
	if (SDL_Quit != NULL)
	{
		nxCrashMarker ("SDL_Quit()");
		SDL_Quit ();
		nxCrashMarker ("SDL_Quit() done");
	}

	/* Give the display a moment to be released before claiming the default
	   nwindow for the framebuffer. */
	svcSleepThread (100000000ULL);

	if (!nxScreenInit ())
	{
		return;
	}

	char savedPath[NX_CRASH_PATH_MAX] = "";
	bool saved = false;

	if (s_path[0] != '\0')
	{
		snprintf (savedPath, sizeof savedPath, "%s", s_path);
		saved = true;
	}

	nxCrashMarker ("draw step");
	nxCrashPresent (saved, savedPath);

	/** 
	 * SDL_Quit() may have shut hid down. Re-open it before reading the pad,
	 * and skip padConfigureInput(): it calls diagAbortWithResult() on failure,
	 * which is exactly the console fatal-error dialog this handler avoids. 
	 */
	Result hidResult = hidInitialize ();
	bool padReady = R_SUCCEEDED (hidResult);

	nxCrashMarker ("hidInitialize=0x%08x", (unsigned) hidResult);

	PadState pad;

	if (padReady)
	{
		padInitializeDefault (&pad);
	}

	/* Long enough to read, bounded so it can never hang. */
	for (int i = 0; i < NX_CRASH_HOLD_ITERATIONS; i++)
	{
		if (padReady)
		{
			padUpdate (&pad);
			u64 down = padGetButtonsDown (&pad);

			if (!saved && (down & HidNpadButton_Minus) != 0)
			{
				char path[NX_CRASH_PATH_MAX];

				if (nxCrashBuildPath (NX_CRASH_DEFAULT_DIR, path, sizeof path))
				{
					nxCrashWriteLog (path);
					snprintf (s_path, sizeof s_path, "%s", path);
					snprintf (savedPath, sizeof savedPath, "%s", path);
					fsdevCommitDevice ("sdmc");
					saved = true;
					nxCrashPresent (saved, savedPath);
				}
			}

			if ((down & HidNpadButton_Plus) != 0)
			{
				nxCrashMarker ("Exit requested");
				break;
			}
		}

		svcSleepThread ((u64) NX_CRASH_HOLD_MS * 1000000ULL);
	}

	if (padReady)
	{
		hidExit ();
	}

	nxCrashMarker ("screen shown");
}

static void nxCrashExit (void) NX_NORETURN;

static void nxCrashExit (void)
{
	if (s_exiting)
	{
		nxCrashMarker ("re-entered during exit");
		_Exit (1);
	}

	s_exiting = true;
	nxCrashMarker ("teardown");

	if (nxScreenIsReady ())
	{
		nxCrashMarker ("framebufferClose()");
		nxScreenClose ();
	}

	if (s_shutdownHook != NULL)
	{
		nxCrashMarker ("app shutdown");
		s_shutdownHook ();
		s_shutdownHook = NULL;
	}

	if (nx_socket_runtime_exit != NULL)
	{
		nxCrashMarker ("nx_socket_runtime_exit()");
		nx_socket_runtime_exit ();
	}

	nxCrashMarker ("_Exit(0)");
	fsdevCommitDevice ("sdmc");

	_Exit (0);
}

/** 
 * Returns a string describing the current exception.
 */
static const char *nxCrashExceptionDetail (void)
{
	static char buffer[NX_CRASH_LINE_MAX];
	const std::type_info *type = abi::__cxa_current_exception_type ();

	buffer[0] = '\0';

	if (type != NULL)
	{
		int status = 0;
		char *name = abi::__cxa_demangle (type->name (), NULL, NULL, &status);

		snprintf (buffer, sizeof buffer, "%s", (status == 0 && name != NULL) ? name : type->name ());

		if (name != NULL)
		{
			free (name);
		}
	}

	std::exception_ptr current = std::current_exception ();

	if (current != NULL)
	{
		try
		{
			std::rethrow_exception (current);
		}
		catch (const std::exception &e)
		{
			size_t used = strlen (buffer);
			snprintf (buffer + used, sizeof buffer - used, " %s", e.what ());
		}
		catch (...)
		{
			size_t used = strlen (buffer);
			snprintf (buffer + used, sizeof buffer - used, " <non-standard exception>");
		}
	}

	return buffer;
}

/** 
 * Reports the current exception and exits.
 */
static void nxCrashTerminate (void)
{
	if (s_exiting)
	{
		nxCrashMarker ("re-entered during exit");
		_Exit (1);
	}

	if (s_reporting)
	{
		nxCrashExit ();
	}

	s_reporting = true;
	nxCrashBuildReport ("TERMINATE", nxCrashExceptionDetail ());
	nxCrashSaveReport ();
	nxCrashShowScreen ();
	nxCrashExit ();
}

/** 
 * Returns the name of a signal.
 */
static const char *nxCrashSignalName (int sig)
{
	switch (sig)
	{
		case SIGABRT: return "SIGABRT";
		case SIGSEGV: return "SIGSEGV";
		case SIGBUS: return "SIGBUS";
		case SIGFPE: return "SIGFPE";
		case SIGILL: return "SIGILL";
		case SIGINT: return "SIGINT";
		case SIGTERM: return "SIGTERM";
		default: return "signal";
	}
}

static void nxCrashSignal (int sig)
{
	if (s_exiting)
	{
		nxCrashMarker ("re-entered during exit");
		_Exit (1);
	}

	if (s_reporting)
	{
		nxCrashExit ();
	}

	s_reporting = true;
	nxCrashBuildReport (nxCrashSignalName (sig), "");
	nxCrashSaveReport ();
	nxCrashShowScreen ();
	nxCrashExit ();
}

/** 
 * Installs the crash handler.
 */
void nxCrashInstall (const char *crashDir)
{
	s_dir[0] = '\0';

	if (crashDir != NULL && crashDir[0] != '\0')
	{
		snprintf (s_dir, sizeof s_dir, "%s", crashDir);
		nxCrashEnsureDir (s_dir);
	}

	if (s_installed)
	{
		return;
	}

	s_installed = true;

	nxScreenSetLogFn (nxCrashScreenLog);

	std::set_terminate (nxCrashTerminate);

	/* 
	 * Faults are listed for completeness: Horizon usually kills the thread
	 * before userland sees them, but abort() and the software signals do
	 * reach these handlers. devkitPro's newlib exposes plain signal(), not
	 * the POSIX sigaction/siginfo API. 
	 */
	const int signals[] = { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGINT, SIGTERM };

	for (size_t i = 0; i < sizeof signals / sizeof signals[0]; i++)
	{
		signal (signals[i], nxCrashSignal);
	}
}

/** 
 * Sets the program name shown on the crash screen.
 */
void nxCrashSetProgram (const char *program)
{
	if (program == NULL || program[0] == '\0')
	{
		return;
	}

	snprintf (s_program, sizeof s_program, "%s", program);
}

/** 
 * Adds an addon to the crash report.
 */
void nxCrashAddAddon (const char *addon)
{
	if (addon == NULL || addon[0] == '\0' || s_addonCount >= NX_CRASH_ADDONS)
	{
		return;
	}

	snprintf (s_addons[s_addonCount], NX_CRASH_ADDON_MAX, "%s", addon);
	s_addonCount++;
}

/** 
 * Sets the phase shown on the crash screen.
 */
void nxCrashSetPhase (const char *phase)
{
	if (phase == NULL)
	{
		return;
	}

	snprintf (s_phase, sizeof s_phase, "%s", phase);
}

/** 
 * Reports a crash with a custom reason.
 */
void nxCrashReport (const char *reason)
{
	if (s_exiting)
	{
		nxCrashMarker ("re-entered during exit");
		_Exit (1);
	}

	if (s_reporting)
	{
		nxCrashExit ();
	}

	s_reporting = true;
	nxCrashBuildReport ("FATAL", reason);
	nxCrashSaveReport ();
	nxCrashShowScreen ();
	nxCrashExit ();
}

void nxCrashFinish (void)
{
	// A clean run leaves no file behind: logs are one-per-crash.
}

void nxCrashSetShutdownHook (void (*hook) (void))
{
	s_shutdownHook = hook;
}

/** 
 * Being an archive member, this file is only linked when something references
 * it. Projects that never call nxCrashInstall() still get the terminate
 * handler through this constructor. 
 */
__attribute__ ((constructor)) static void nxCrashAutoInstall (void)
{
	nxCrashInstall (NULL);
}

#else

/* 
 * Also compiled when the application opts out with -D HXCPP_NX_NO_CRASH_HANDLER: the
 * API stays available so callers do not need #if guards, it just does nothing. 
 */

/** 
 * Installs the crash handler.
 */
void nxCrashInstall (const char *crashDir) { (void) crashDir; }

/** 
 * Sets the program name shown on the crash screen.
 */
void nxCrashSetProgram (const char *program) { (void) program; }

/** 
 * Adds an addon to the crash report.
 */
void nxCrashAddAddon (const char *addon) { (void) addon; }

/** 
 * Sets the phase shown on the crash screen.
 */
void nxCrashSetPhase (const char *phase) { (void) phase; }

/** 
 * Reports a crash with a custom reason.
 */
void nxCrashReport (const char *reason) { (void) reason; }

/** 
 * Finalizes the crash reporting process.
 */
void nxCrashFinish (void) {}

/** 
 * Sets the shutdown hook for the crash handler.
 */
void nxCrashSetShutdownHook (void (*hook) (void)) { (void) hook; }
#endif // !defined(HXCPP_NX_NO_CRASH_HANDLER)