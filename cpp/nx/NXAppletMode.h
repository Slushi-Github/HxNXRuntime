/*
 * NXAppletMode - refuse to start in applet mode.
 *
 * When a homebrew is launched from the Album it runs as a library applet,
 * which has less memory than a full application. Large games run out of
 * memory there and the console kills them (or worse, the fatal-error dialog
 * appears). This guard runs at the very start of main(), before anything is
 * initialised, and when applet mode is detected it shows a notice and waits
 * for PLUS before returning to hbmenu with a clean exit.
 *
 */

#ifndef NX_APPLET_MODE_H
#define NX_APPLET_MODE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * True when the process runs as a library applet (launched from the Album).
 * "Application" is the normal mode; unknown/not-initialised values are not
 * treated as applet mode so a detection failure never blocks a normal start.
 */
bool nxAppletModeDetected (void);

/*
 * If applet mode is detected: draws the notice, waits for PLUS (or a bounded
 * timeout) and exits the process cleanly, so the loader goes back to hbmenu.
 * Returns immediately when the process is a normal application.
 *
 * `program` is the name shown on the notice, normally the NRO file name.
 */
void nxAppletModeGuard (const char *program);

/*
 * Registers the application's own shutdown (the same hook
 * nxCrashSetShutdownHook() takes), so the guard closes OpenAL, romfs and the
 * nxlink socket before returning to hbmenu, exactly like the crash handler.
 * Pass NULL to clear it.
 */
void nxAppletModeSetShutdownHook (void (*hook) (void));

#ifdef __cplusplus
}
#endif

#endif /* NX_APPLET_MODE_H */
