/*
 * NXScreen - text framebuffer shared by HxNXRuntime's early screens.
 *
 * NXCrash draws its report here and NXAppletMode draws the applet-mode
 * notice. Both need a screen before SDL, EGL, the text console or any Haxe
 * code exist, so this draws straight into a 1280x720 RGB565 framebuffer using
 * libnx's built-in 16x16 console font. Nothing else has to be initialised.
 *
 * This is internal to HxNXRuntime; it is not part of the public NXCrash API.
 */

#ifndef NX_SCREEN_H
#define NX_SCREEN_H

#include <switch.h>

/*
 * Optional diagnostics hook. NXCrash points it at its log marker so the
 * screen steps end up in the crash log; NXAppletMode leaves it NULL and the
 * screen stays silent.
 *
 * Kept outside the extern "C" block: in C++ a function type declared with C
 * linkage is a distinct type, and the hook is implemented by C++ code.
 */
typedef void (*NXScreenLogFn) (const char *message);

#ifdef __cplusplus
extern "C"
{
#endif

#define NX_SCREEN_WIDTH 1280
#define NX_SCREEN_HEIGHT 720
#define NX_SCREEN_CELL 16
#define NX_SCREEN_COLS (NX_SCREEN_WIDTH / NX_SCREEN_CELL)
#define NX_SCREEN_ROWS (NX_SCREEN_HEIGHT / NX_SCREEN_CELL)
#define NX_SCREEN_LINE_MAX 128
#define NX_SCREEN_LINES_MAX 48

/* Palette (RGB565). */
extern const u16 NX_BG;
extern const u16 NX_FG;
extern const u16 NX_DIM;
extern const u16 NX_TITLE;
extern const u16 NX_RULE;
extern const u16 NX_ACCENT;
extern const u16 NX_WARN;
extern const u16 NX_OK;
extern const u16 NX_STACK;

/* Installs the diagnostics hook described above. */
void nxScreenSetLogFn (NXScreenLogFn fn);

/*
 * Claims the default window and prepares the linear framebuffer. Returns
 * false when the display is not ready; the caller decides what to do.
 */
bool nxScreenInit (void);

/* Releases the framebuffer. Safe to call when nxScreenInit() failed. */
void nxScreenClose (void);

bool nxScreenIsReady (void);

/*
 * The line buffer: add lines, then nxScreenPresent() draws them centered.
 * Lines added once the buffer is full are ignored.
 */
void nxScreenClearLines (void);
void nxScreenAddLine (const char *text, u16 color);
void nxScreenAddLinef (u16 color, const char *fmt, ...);
int nxScreenLineCount (void);

/* Fills the framebuffer, converts it to block-linear and queues it. */
bool nxScreenPresent (void);

/*
 * Drawing primitives used by nxScreenPresent(). They draw into the linear
 * framebuffer, so they are only valid during a present.
 */
void nxScreenClear (u16 color);
void nxScreenDrawGlyph (int col, int row, int ch, u16 color);
void nxScreenDrawText (int col, int row, const char *text, u16 color);
void nxScreenDrawTextCentered (int row, const char *text, u16 color);

#ifdef __cplusplus
}
#endif

#endif /* NX_SCREEN_H */
