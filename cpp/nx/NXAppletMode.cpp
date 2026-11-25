/*
 * NXAppletMode - refuse to start in applet mode.
 *
 * See NXAppletMode.h. The notice is drawn with NXScreen, the same renderer
 * the crash handler uses, so there is no text console and no SDL involved.
 */

#include "NXAppletMode.h"

#include <stdio.h>
#include <stdlib.h>
#include <switch.h>

#include "NXScreen.h"

#define NX_APPLET_RULE "================================================"

/* Long enough to read the notice, bounded so it can never hang. */
#define NX_APPLET_HOLD_MS 16
#define NX_APPLET_HOLD_ITERATIONS 3750
#define NX_APPLET_HOLD_SECONDS ((NX_APPLET_HOLD_ITERATIONS * NX_APPLET_HOLD_MS) / 1000)

/* The display is normally ready here, but retry a little in case the window
   manager is still setting up the default window. */
#define NX_APPLET_SCREEN_ATTEMPTS 5
#define NX_APPLET_SCREEN_RETRY_MS 50

/* Started before main() by NXSocketRuntime.cpp; weak so HxNXRuntime still
   links without it. */
extern "C" void nx_socket_runtime_exit (void) __attribute__((weak));

static void (*s_shutdownHook) (void) = NULL;

/*
 * Same teardown order as the crash handler (NXCrash.cpp): run the
 * application's own shutdown (OpenAL, romfs, nxlink), then the socket
 * service, and leave with _Exit(0) so the loader returns to hbmenu.
 */
static void nxAppletModeExit (void) NX_NORETURN;

static void nxAppletModeExit (void)
{
	if (s_shutdownHook != NULL)
	{
		s_shutdownHook ();
		s_shutdownHook = NULL;
	}

	if (nx_socket_runtime_exit != NULL)
	{
		nx_socket_runtime_exit ();
	}

	_Exit (0);
}

void nxAppletModeSetShutdownHook (void (*hook) (void))
{
	s_shutdownHook = hook;
}

bool nxAppletModeDetected (void)
{
	return appletGetAppletType () == AppletType_LibraryApplet;
}

static void nxAppletModeBuildNotice (const char *program)
{
	nxScreenClearLines ();
	nxScreenAddLine (NX_APPLET_RULE, NX_RULE);
	nxScreenAddLine ("HxNXRuntime -- Applet Mode", NX_TITLE);
	nxScreenAddLine (NX_APPLET_RULE, NX_RULE);
	nxScreenAddLine ("", NX_FG);

	nxScreenAddLinef (NX_FG, "Program: %s", (program != NULL && program[0] != '\0') ? program : "Haxe program");

	nxScreenAddLine ("", NX_FG);
	nxScreenAddLine ("This program cannot start in applet mode.", NX_ACCENT);
	nxScreenAddLine ("", NX_FG);
	nxScreenAddLine ("Applet mode (the one the Album uses) gives", NX_FG);
	nxScreenAddLine ("the process less memory. This program needs", NX_FG);
	nxScreenAddLine ("the full application mode, otherwise it would", NX_FG);
	nxScreenAddLine ("run out of memory and the console would", NX_FG);
	nxScreenAddLine ("close it.", NX_FG);
	nxScreenAddLine ("", NX_FG);
	nxScreenAddLine ("Start the homebrew menu from a game's title", NX_FG);
	nxScreenAddLine ("instead, then launch this program from there.", NX_FG);
	nxScreenAddLine ("", NX_FG);
	nxScreenAddLine (NX_APPLET_RULE, NX_RULE);
	nxScreenAddLine ("Press PLUS to return to hbmenu.", NX_FG);
	nxScreenAddLinef (NX_DIM, "Closes by itself after %d seconds.", NX_APPLET_HOLD_SECONDS);
}

/*
 * Same steps as padConfigureInput(1, HidNpadStyleSet_NpadStandard), but
 * checking the results: padConfigureInput() calls diagAbortWithResult() on
 * failure, which is the fatal-error dialog this guard exists to avoid.
 *
 * Without this, a library applet has no npad id/style configured and
 * padUpdate() never reports a button, so PLUS would be ignored. The crash
 * handler does not need it because SDL has already configured hid by then.
 */
static void nxAppletModeConfigureInput (void)
{
	Result rc = hidSetSupportedNpadStyleSet (HidNpadStyleSet_NpadStandard);

	if (R_FAILED (rc))
	{
		return;
	}

	const HidNpadIdType ids[] = { HidNpadIdType_No1, HidNpadIdType_Handheld };

	rc = hidSetSupportedNpadIdType (ids, sizeof (ids) / sizeof (ids[0]));

	if (R_FAILED (rc))
	{
		return;
	}

	/* padConfigureInput() activates npad last. It aborts internally on
	   failure, but by this point hid is initialised and the applet has its
	   resource, so this is the normal path every applet example uses. */
	hidInitializeNpad ();
}

void nxAppletModeGuard (const char *program)
{
	if (!nxAppletModeDetected ())
	{
		return;
	}

	nxAppletModeBuildNotice (program);

	bool screenReady = false;

	for (int attempt = 0; attempt < NX_APPLET_SCREEN_ATTEMPTS && !screenReady; attempt++)
	{
		if (attempt > 0)
		{
			svcSleepThread ((u64) NX_APPLET_SCREEN_RETRY_MS * 1000000ULL);
		}

		screenReady = nxScreenInit ();
	}

	if (!screenReady)
	{
		/* Nothing to show, but the program still must not start in applet
		   mode. Leave cleanly so the loader returns to hbmenu. */
		nxAppletModeExit ();
	}

	nxScreenPresent ();

	/* Skip padConfigureInput(): it calls diagAbortWithResult() on failure,
	   which is the console fatal-error dialog this guard avoids. */
	Result hidResult = hidInitialize ();
	bool padReady = R_SUCCEEDED (hidResult);

	PadState pad;

	if (padReady)
	{
		nxAppletModeConfigureInput ();
		padInitializeDefault (&pad);
	}

	for (int i = 0; i < NX_APPLET_HOLD_ITERATIONS; i++)
	{
		if (padReady)
		{
			padUpdate (&pad);

			if ((padGetButtonsDown (&pad) & HidNpadButton_Plus) != 0)
			{
				break;
			}
		}

		svcSleepThread ((u64) NX_APPLET_HOLD_MS * 1000000ULL);
	}

	if (padReady)
	{
		hidExit ();
	}

	nxScreenClose ();

	nxAppletModeExit ();
}
