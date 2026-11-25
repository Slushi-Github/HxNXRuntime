/*
 * NXScreen - text framebuffer shared by HxNXRuntime's early screens.
 *
 * See NXScreen.h. This is the rendering half of the old NXCrash screen,
 * moved out unchanged so NXAppletMode can reuse it without duplicating the
 * framebuffer, the font code or the block-linear conversion.
 */

#include "NXScreen.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const u16 NX_BG = RGB565_FROM_RGB8 (10, 12, 18);
const u16 NX_FG = RGB565_FROM_RGB8 (228, 230, 236);
const u16 NX_DIM = RGB565_FROM_RGB8 (150, 156, 168);
const u16 NX_TITLE = RGB565_FROM_RGB8 (120, 200, 255);
const u16 NX_RULE = RGB565_FROM_RGB8 (70, 95, 125);
const u16 NX_ACCENT = RGB565_FROM_RGB8 (255, 110, 110);
const u16 NX_WARN = RGB565_FROM_RGB8 (255, 210, 90);
const u16 NX_OK = RGB565_FROM_RGB8 (130, 230, 140);
const u16 NX_STACK = RGB565_FROM_RGB8 (200, 206, 216);

struct NXScreenLine
{
	char text[NX_SCREEN_LINE_MAX];
	u16 color;
};

static NXScreenLine s_lines[NX_SCREEN_LINES_MAX];
static int s_lineCount = 0;

static Framebuffer s_fb;
static u16 *s_fbData = NULL;
static u32 s_fbStride = 0;
static bool s_screenReady = false;

static NXScreenLogFn s_logFn = NULL;

static void nxScreenLog (const char *message)
{
	if (s_logFn != NULL)
	{
		s_logFn (message);
	}
}

static void nxScreenLogf (const char *fmt, ...)
{
	if (s_logFn == NULL)
	{
		return;
	}

	char buffer[192];

	va_list args;
	va_start (args, fmt);
	vsnprintf (buffer, sizeof buffer, fmt, args);
	va_end (args);

	s_logFn (buffer);
}

void nxScreenSetLogFn (NXScreenLogFn fn)
{
	s_logFn = fn;
}

bool nxScreenIsReady (void)
{
	return s_screenReady;
}

void nxScreenClearLines (void)
{
	s_lineCount = 0;
}

void nxScreenAddLine (const char *text, u16 color)
{
	if (s_lineCount >= NX_SCREEN_LINES_MAX)
	{
		return;
	}

	NXScreenLine *line = &s_lines[s_lineCount++];
	snprintf (line->text, sizeof line->text, "%s", text);
	line->color = color;
}

void nxScreenAddLinef (u16 color, const char *fmt, ...)
{
	if (s_lineCount >= NX_SCREEN_LINES_MAX)
	{
		return;
	}

	NXScreenLine *line = &s_lines[s_lineCount++];

	va_list args;
	va_start (args, fmt);
	vsnprintf (line->text, sizeof line->text, fmt, args);
	va_end (args);

	line->color = color;
}

int nxScreenLineCount (void)
{
	return s_lineCount;
}

void nxScreenClear (u16 color)
{
	for (u32 y = 0; y < NX_SCREEN_HEIGHT; y++)
	{
		u16 *row = s_fbData + (size_t) y * s_fbStride;

		for (u32 x = 0; x < NX_SCREEN_WIDTH; x++)
		{
			row[x] = color;
		}
	}
}

/*
 * Glyph source is libnx's built-in 16x16 1bpp console font, so the screen has
 * no external dependency and no text console behind it.
 */
void nxScreenDrawGlyph (int col, int row, int ch, u16 color)
{
	PrintConsole *fontConsole = consoleGetDefault ();
	const u16 *font = (const u16 *) fontConsole->font.gfx;

	int index = ch - fontConsole->font.asciiOffset;

	if (index < 0 || index >= fontConsole->font.numChars)
	{
		return;
	}

	const u16 *glyph = font + 16 * index;

	int x0 = col * NX_SCREEN_CELL;
	int y0 = row * NX_SCREEN_CELL;

	if (x0 < 0 || y0 < 0 || x0 + NX_SCREEN_CELL > NX_SCREEN_WIDTH || y0 + NX_SCREEN_CELL > NX_SCREEN_HEIGHT)
	{
		return;
	}

	for (int y = 0; y < NX_SCREEN_CELL; y++)
	{
		u16 bits = glyph[y];
		u16 *dst = s_fbData + (size_t) (y0 + y) * s_fbStride + x0;

		for (int x = 0; x < NX_SCREEN_CELL; x++)
		{
			if ((bits & (0x8000u >> x)) != 0)
			{
				dst[x] = color;
			}
		}
	}
}

void nxScreenDrawText (int col, int row, const char *text, u16 color)
{
	for (int i = 0; text[i] != '\0' && col + i < NX_SCREEN_COLS; i++)
	{
		nxScreenDrawGlyph (col + i, row, (unsigned char) text[i], color);
	}
}

void nxScreenDrawTextCentered (int row, const char *text, u16 color)
{
	int length = (int) strlen (text);
	int col = (NX_SCREEN_COLS - length) / 2;

	nxScreenDrawText (col > 0 ? col : 0, row, text, color);
}

/* libnx's framebufferBegin()/framebufferEnd() call diagAbortWithResult() when
   the window is not ready, and that is the console's fatal-error dialog. These
   screens must never trigger it, so the dequeue/queue and the block-linear
   conversion are done here with every Result checked. The conversion mirrors
   libnx's framebuffer.c (64-byte GOBs, 16Bx2 sector ordering). */
static void nxScreenConvertGobTo16Bx2 (u8 *outgob, const u8 *ingob, u32 stride)
{
	for (u32 i = 0; i < 32; i++)
	{
		const u32 y = ((i >> 1) & 0x06) | (i & 0x01);
		const u32 x = ((i << 3) & 0x10) | ((i << 1) & 0x20);
		*(u128 *) outgob = *(const u128 *) (ingob + y * stride + x);
		outgob += sizeof (u128);
	}
}

static void nxScreenConvertToBlocklinear (void *outbuf, const void *inbuf, u32 stride, u32 height, u32 block_height_log2)
{
	const u32 block_height_gobs = 1U << block_height_log2;
	const u32 block_height_px = 8U << block_height_log2;
	const u32 width_blocks = stride >> 6;
	const u32 height_blocks = (height + block_height_px - 1) >> (3 + block_height_log2);
	u8 *outgob = (u8 *) outbuf;

	for (u32 block_y = 0; block_y < height_blocks; block_y++)
	{
		for (u32 block_x = 0; block_x < width_blocks; block_x++)
		{
			for (u32 gob_y = 0; gob_y < block_height_gobs; gob_y++)
			{
				const u32 x = block_x * 64;
				const u32 y = block_y * block_height_px + gob_y * 8;

				if (y < height)
				{
					const u8 *ingob = (const u8 *) inbuf + y * stride + x;
					nxScreenConvertGobTo16Bx2 (outgob, ingob, stride);
				}

				outgob += 512;
			}
		}
	}
}

bool nxScreenPresent (void)
{
	if (!s_screenReady)
	{
		return false;
	}

	s32 slot = -1;
	Result rc = nwindowDequeueBuffer (s_fb.win, &slot, NULL);

	if (R_FAILED (rc))
	{
		nxScreenLogf ("dequeue failed: 0x%08x", (unsigned) rc);
		return false;
	}

	s_fbData = (u16 *) s_fb.buf_linear;
	s_fbStride = s_fb.stride / sizeof (u16);

	nxScreenClear (NX_BG);

	int top = (NX_SCREEN_ROWS - s_lineCount) / 2;

	if (top < 0)
	{
		top = 0;
	}

	for (int i = 0; i < s_lineCount; i++)
	{
		nxScreenDrawTextCentered (top + i, s_lines[i].text, s_lines[i].color);
	}

	s_fbData = NULL;

	u8 *dst = (u8 *) s_fb.buf + (size_t) slot * s_fb.fb_size;
	nxScreenConvertToBlocklinear (dst, s_fb.buf_linear, s_fb.stride, s_fb.win->height, 4);
	armDCacheFlush (dst, s_fb.fb_size);

	rc = nwindowQueueBuffer (s_fb.win, slot, NULL);

	if (R_FAILED (rc))
	{
		nxScreenLogf ("queue failed: 0x%08x", (unsigned) rc);
		return false;
	}

	return true;
}

bool nxScreenInit (void)
{
	if (s_screenReady)
	{
		return true;
	}

	NWindow *window = nwindowGetDefault ();

	/* SDL/EGL may still hold buffers on the default window (the crash path
	   runs after SDL_Quit()). Release them so framebufferCreate() can dequeue
	   its own slots; harmless if unused. */
	nxScreenLog ("nwindowReleaseBuffers()");
	nwindowReleaseBuffers (window);

	nxScreenLog ("nwindowSetDimensions()");

	Result rc = nwindowSetDimensions (window, NX_SCREEN_WIDTH, NX_SCREEN_HEIGHT);

	if (R_FAILED (rc))
	{
		nxScreenLogf ("nwindowSetDimensions failed: 0x%08x", (unsigned) rc);
		return false;
	}

	nxScreenLog ("framebufferCreate()");

	rc = framebufferCreate (&s_fb, window, NX_SCREEN_WIDTH, NX_SCREEN_HEIGHT, PIXEL_FORMAT_RGB_565, 2);

	if (R_FAILED (rc))
	{
		nxScreenLogf ("framebufferCreate failed: 0x%08x", (unsigned) rc);
		return false;
	}

	nxScreenLog ("framebufferMakeLinear()");

	rc = framebufferMakeLinear (&s_fb);

	if (R_FAILED (rc))
	{
		nxScreenLogf ("framebufferMakeLinear failed: 0x%08x", (unsigned) rc);
		framebufferClose (&s_fb);
		return false;
	}

	s_screenReady = true;

	return true;
}

void nxScreenClose (void)
{
	if (!s_screenReady)
	{
		return;
	}

	framebufferClose (&s_fb);
	s_screenReady = false;
	s_fbData = NULL;
	s_fbStride = 0;
}
