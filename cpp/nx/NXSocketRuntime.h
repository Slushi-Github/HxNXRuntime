/**
 * NXSocketRuntime - libnx socket lifecycle owned by HxNXRuntime.
 
 * The HaxeNXCompiler wrapper used to call socketInitializeDefault() at the
 * start of main() and socketExit() before returning. HxNXRuntime now owns that
 * lifecycle, so the generated wrapper does not need to know about sockets:
 *
 *   - The socket is started before main() runs (and therefore before any Haxe
 *     code), but only when libnx did not start it already. socketInitialize()
 *     is not refcounted and returns LibnxError_AlreadyInitialized otherwise.
 *   - socketExit() is registered with atexit() during that same early phase,
 *     so it runs when main() returns normally, when the wrapper returns after
 *     catching an uncaught Haxe exception, or when exit() is called.
 *
 * This header is force-included in every generated .cpp by the NX toolchain.
 * The tiny anchor below keeps the object that implements the lifecycle in the
 * link: static archives only pull a member when one of its symbols is needed,
 * and nothing in a regular Haxe app references these functions by hand.
 *
 * It deliberately uses plain C types and includes nothing, so it is safe to
 * inject everywhere.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starts the default libnx socket unless somebody already started it.
// Safe to call more than once.
void nx_socket_runtime_init(void);

// Closes the socket only when nx_socket_runtime_init() started it.
// Safe to call more than once.
void nx_socket_runtime_exit(void);

#ifdef __cplusplus
}

namespace nx_socket_runtime
{
    // One instance per translation unit (internal linkage). Its constructor
    // both forces the archive member into the link and makes sure the socket
    // is up before main()/hxcpp runs. nx_socket_runtime_init() is idempotent,
    // so the repeated calls are harmless.
    struct LinkAnchor
    {
        LinkAnchor() { ::nx_socket_runtime_init(); }
    };

    static LinkAnchor g_linkAnchor;
}
#endif
