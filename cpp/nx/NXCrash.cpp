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

#define NX_CRASH_VERSION "1.1.0"
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
#define NX_CRASH_THREAD_NAME_MAX 24
#define NX_CRASH_THREAD_SLOTS 16
#define NX_CRASH_PREV_MAX 10
#define NX_CRASH_PREV_READ 65536

/*
 * How long the crash screen waits for a button before exiting on its own. 
 */
#define NX_CRASH_HOLD_MS 16
#define NX_CRASH_HOLD_ITERATIONS 1500
#define NX_CRASH_HOLD_SECONDS ((NX_CRASH_HOLD_ITERATIONS * NX_CRASH_HOLD_MS) / 1000)

/**
 * Folder where a file is written automatically on crash. Empty means nothing
 * is written automatically and MINUS writes to NX_CRASH_DEFAULT_DIR. 
 */
static char s_dir[NX_CRASH_DIR_MAX] = "";

/** 
 * File of the current report. Empty until it is written. 
 */
static char s_path[NX_CRASH_PATH_MAX] = "";

/**
 * Message shown on the crash screen when MINUS could not write the report,
 * so the button never looks like it did nothing.
 */
static char s_saveError[NX_CRASH_LINE_MAX] = "";

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
 * Thread that is reporting the crash. A fault in Horizon never reaches us, but
 * abort(), std::terminate() and nxCrashReport() can run on any worker thread,
 * and then the report has to say which one it was.
 */
static Thread *s_mainThread = NULL;
static u64 s_mainThreadId = 0;

static u64 s_reportThreadId = 0;
static bool s_reportThreadIsMain = true;
static void *s_reportThreadTls = NULL;
static void *s_reportThreadSp = NULL;
static void *s_reportThreadStack = NULL;
static size_t s_reportThreadStackSize = 0;
static const char *s_reportThreadName = NULL;

/** Optional per-thread labels set with nxCrashSetThreadName(), ring buffer. */
static struct
{
	u64 id;
	char name[NX_CRASH_THREAD_NAME_MAX];
} s_threadNames[NX_CRASH_THREAD_SLOTS];

static int s_threadNameNext = 0;

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

/* Current stack pointer, to tell how deep the reporting thread was. */
static void *nxCrashReadSp (void)
{
	void *sp;
	__asm__ volatile ("mov %0, sp" : "=r" (sp));
	return sp;
}

static u64 nxCrashCurrentThreadId (void)
{
	u64 id = 0;
	Handle handle = threadGetCurHandle ();

	if (handle != 0 && R_SUCCEEDED (svcGetThreadId (&id, handle)))
	{
		return id;
	}

	return 0;
}

static const char *nxCrashThreadNameLookup (u64 id)
{
	if (id == 0)
	{
		return NULL;
	}

	for (int i = 0; i < NX_CRASH_THREAD_SLOTS; i++)
	{
		if (s_threadNames[i].id == id)
		{
			return s_threadNames[i].name;
		}
	}

	return NULL;
}

/**
 * Records which thread is reporting, its TLS, its stack pointer and, when
 * libnx knows the thread, the stack region it was given. Called from the
 * crashing thread, so only TLS reads and one syscall are used.
 */
static void nxCrashCaptureThread (void)
{
	Thread *self = threadGetSelf ();
	u64 id = 0;

	s_reportThreadTls = armGetTls ();
	s_reportThreadSp = nxCrashReadSp ();
	s_reportThreadStack = NULL;
	s_reportThreadStackSize = 0;

	if (self != NULL)
	{
		s_reportThreadStack = self->stack_mem;
		s_reportThreadStackSize = self->stack_sz;

		if (self->handle != 0 && R_SUCCEEDED (svcGetThreadId (&id, self->handle)))
		{
			s_reportThreadId = id;
		}
	}
	else
	{
		s_reportThreadId = nxCrashCurrentThreadId ();
	}

	s_reportThreadIsMain = (self != NULL && self == s_mainThread)
		|| (s_reportThreadId != 0 && s_reportThreadId == s_mainThreadId);
	s_reportThreadName = nxCrashThreadNameLookup (s_reportThreadId);
}

/**
 * "main", "worker", plus the label and id when known. Kept short enough for
 * the 80 column crash screen.
 */
static void nxCrashThreadLabel (char *out, size_t size)
{
	const char *kind = s_reportThreadIsMain ? "main" : "worker";

	if (s_reportThreadName != NULL)
	{
		if (s_reportThreadId != 0)
		{
			snprintf (out, size, "%s \"%s\" (id 0x%llx)", kind, s_reportThreadName, (unsigned long long) s_reportThreadId);
		}
		else
		{
			snprintf (out, size, "%s \"%s\"", kind, s_reportThreadName);
		}
	}
	else if (s_reportThreadId != 0)
	{
		snprintf (out, size, "%s (id 0x%llx)", kind, (unsigned long long) s_reportThreadId);
	}
	else
	{
		snprintf (out, size, "%s", kind);
	}
}

/**
 * Writes the reporting thread block to the log: which thread, where its TLS
 * lives, and how much of its stack was in use.
 */
static void nxCrashWriteThread (FILE *out)
{
	char label[NX_CRASH_LINE_MAX];

	nxCrashThreadLabel (label, sizeof label);
	fprintf (out, "Thread: %s\n", label);

	if (s_reportThreadTls != NULL)
	{
		fprintf (out, "  TLS: %p\n", s_reportThreadTls);
	}

	if (s_reportThreadSp != NULL)
	{
		fprintf (out, "  SP: %p\n", s_reportThreadSp);
	}

	if (s_reportThreadStack != NULL && s_reportThreadStackSize > 0)
	{
		uintptr_t base = (uintptr_t) s_reportThreadStack;
		uintptr_t top = base + s_reportThreadStackSize;
		uintptr_t sp = (uintptr_t) s_reportThreadSp;

		fprintf (out, "  Stack: %p-%p (size 0x%llx", s_reportThreadStack, (void *) top,
			(unsigned long long) s_reportThreadStackSize);

		if (sp >= base && sp <= top)
		{
			fprintf (out, ", used 0x%llx", (unsigned long long) (top - sp));
		}

		fprintf (out, ")\n");
	}
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
 * Writes the report to the given path. Returns false when the file could not
 * be opened, so the caller can report it on the screen instead of silently
 * pretending the log was saved.
 */
static bool nxCrashWriteLog (const char *path)
{
	FILE *out = fopen (path, "w");

	if (out == NULL)
	{
		return false;
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
	nxCrashWriteThread (out);
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

	return true;
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
 * Creates the crash directory and any missing parent, like `mkdir -p`.
 * `mkdir()` alone only creates the last component, so
 * `sdmc:/switch/HxNXRuntime/nativeCrashes` failed whenever
 * `sdmc:/switch/HxNXRuntime` did not exist yet, and MINUS silently did
 * nothing. The drive prefix (`sdmc:`) is left alone.
 */
static bool nxCrashEnsureDir (const char *dir)
{
	if (dir == NULL || dir[0] == '\0')
	{
		return false;
	}

	char path[NX_CRASH_PATH_MAX];
	snprintf (path, sizeof path, "%s", dir);

	for (char *p = path + 1; *p != '\0'; p++)
	{
		if (*p != '/')
		{
			continue;
		}

		*p = '\0';
		mkdir (path, 0777);
		*p = '/';
	}

	if (mkdir (path, 0777) == 0)
	{
		fsdevCommitDevice ("sdmc");
		return true;
	}

	struct stat info;

	return stat (path, &info) == 0 && S_ISDIR (info.st_mode);
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

	nxCrashCaptureThread ();
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

	if (!nxCrashWriteLog (path))
	{
		return;
	}

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

	char threadLabel[NX_CRASH_LINE_MAX];
	nxCrashThreadLabel (threadLabel, sizeof threadLabel);
	nxScreenAddLinef (NX_DIM, "Thread: %s", threadLabel);

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

	if (s_saveError[0] != '\0')
	{
		nxScreenAddLinef (NX_ACCENT, "%s", s_saveError);
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

			/* Only when nothing was written automatically. When the project
			   passes a folder to nxCrashInstall(), the log already exists and
			   MINUS must not create duplicates. */
			if (!saved && (down & HidNpadButton_Minus) != 0)
			{
				char path[NX_CRASH_PATH_MAX] = "";

				s_saveError[0] = '\0';
				nxCrashMarker ("MINUS pressed");

				/* Preferred folder is the documented runtime one; fall back to
				   the project folder when it cannot be created. */
				if (!nxCrashBuildPath (NX_CRASH_DEFAULT_DIR, path, sizeof path)
					&& (s_dir[0] == '\0' || !nxCrashBuildPath (s_dir, path, sizeof path)))
				{
					path[0] = '\0';
				}

				if (path[0] == '\0')
				{
					snprintf (s_saveError, sizeof s_saveError, "Could not create %s", NX_CRASH_DEFAULT_DIR);
					nxCrashMarker ("save failed: no path");
				}
				else if (!nxCrashWriteLog (path))
				{
					snprintf (s_saveError, sizeof s_saveError, "Could not write %s", path);
					nxCrashMarker ("save failed: %s", path);
				}
				else
				{
					snprintf (s_path, sizeof s_path, "%s", path);
					snprintf (savedPath, sizeof savedPath, "%s", path);
					fsdevCommitDevice ("sdmc");
					saved = true;
					nxCrashMarker ("saved to %s", path);
				}

				/* Repaint even on failure, so the press is always visible. */
				nxCrashPresent (saved, savedPath);
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

	/* The auto-install constructor runs on the main thread, so this is the
	   reference used later to tell main from worker threads. */
	if (s_mainThreadId == 0)
	{
		s_mainThread = threadGetSelf ();
		s_mainThreadId = nxCrashCurrentThreadId ();
	}

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
 * Labels the calling thread, so a report from it says which one it was
 * (e.g. "asset-loader"). Slots are reused in a ring: the oldest label is
 * dropped when a new thread has no free one. Safe from any thread.
 */
void nxCrashSetThreadName (const char *name)
{
	if (name == NULL || name[0] == '\0')
	{
		return;
	}

	u64 id = nxCrashCurrentThreadId ();

	if (id == 0)
	{
		return;
	}

	for (int i = 0; i < NX_CRASH_THREAD_SLOTS; i++)
	{
		if (s_threadNames[i].id == id)
		{
			snprintf (s_threadNames[i].name, sizeof s_threadNames[i].name, "%s", name);
			return;
		}
	}

	int slot = s_threadNameNext;
	s_threadNameNext = (slot + 1) % NX_CRASH_THREAD_SLOTS;

	snprintf (s_threadNames[slot].name, sizeof s_threadNames[slot].name, "%s", name);
	s_threadNames[slot].id = id;
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
 * Labels the calling thread, so a report from it says which one it was
 * (e.g. "asset-loader"). Slots are reused in a ring: the oldest label is
 * dropped when a new thread has no free one. Safe from any thread.
 */
void nxCrashSetThreadName (const char *name) { (void) name; }

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