/**
 * NXSocketRuntime - libnx socket lifecycle owned by HxNXRuntime.
 *
 * This file implements the socket lifecycle owned by HxNXRuntime, which keeps the socket
 * available for the whole hxcpp run and closes it on every exit path that goes
 * through exit() (normal return, Haxe exception caught by the crash handler, etc.).
 */

#include "NXSocketRuntime.h"

#include <switch.h>
#include <sys/iosupport.h>
#include <stdlib.h>

namespace
{
    // True only when this module called socketInitializeDefault() itself.
    bool g_ownsSocket = false;
    bool g_atexitRegistered = false;

    void nxSocketExitThunk(void)
    {
        nx_socket_runtime_exit();
    }
}

extern "C" void nx_socket_runtime_init(void)
{
    if (!g_atexitRegistered)
    {
        // Registered before main() so it also runs when hxcpp ends through an
        // uncaught Haxe exception: the wrapper catches it, returns, and the C
        // runtime calls exit().
        if (atexit(nxSocketExitThunk) == 0)
            g_atexitRegistered = true;
    }

    if (g_ownsSocket)
        return;

    // Somebody else already owns the bsd/soc device (a custom wrapper, a
    // library, ...). Never initialize it twice.
    if (FindDevice("soc:") != -1)
        return;

    if (R_SUCCEEDED(socketInitializeDefault()))
        g_ownsSocket = true;
}

extern "C" void nx_socket_runtime_exit(void)
{
    if (!g_ownsSocket)
        return;

    g_ownsSocket = false;

    // A legacy wrapper or another code may have called socketExit() already; in that case the
    // device is gone and there is nothing left to close.
    if (FindDevice("soc:") != -1)
        socketExit();
}
