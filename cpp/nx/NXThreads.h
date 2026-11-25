/**
 * NXThreads - Switch fixes for the hxcpp/libstdc++ thread runtime.
 *
 * DevKitA64's libsysbase implements pthread_detach() as a jump through a
 * platform hook, "__syscall_thread_detach". libnx installs the hooks for
 * create/join/self/exit but not that one, so pthread_detach() returns ENOSYS
 * (88). NXThreads.cpp supplies that hook.
 *
 * This header is force-included in every generated .cpp by the NX toolchain.
 * The anchor keeps the object that implements the hook in the link: static
 * archives only pull a member when one of its symbols is needed, and the
 * reference from libsysbase is weak, so it never pulls it by itself.
 *
 * It deliberately uses plain C types and includes nothing, so it is safe to
 * inject everywhere.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Forces NXThreads.cpp into the link; it has no other effect. */
void nx_threads_link_anchor(void);

#ifdef __cplusplus
}

namespace nx_threads
{
	struct LinkAnchor
	{
		LinkAnchor() { ::nx_threads_link_anchor(); }
	};

	static LinkAnchor g_linkAnchor;
}
#endif
