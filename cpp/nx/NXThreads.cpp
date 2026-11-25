/**
 * NXThreads - Switch fixes for the hxcpp/libstdc++ thread runtime.
 *
 * DevKitA64's libsysbase implements pthread_detach() as a jump through a
 * platform hook, "__syscall_thread_detach". libnx installs the hooks for
 * create/join/self/exit but not that one, so pthread_detach() returns ENOSYS
 * (88).
 *
 * libstdc++ turns that into a std::system_error from std::thread::detach().
 * libhxcpp's Thread.create() always detaches, and while that exception
 * unwinds it destroys the still-joinable std::thread, whose destructor calls
 * std::terminate() - the "<none>" abort with no active exception.
 *
 * Supplying the hook makes detach succeed without doing anything, which is
 * exactly what hxcpp 4.3.x did on Switch: it never detached, and the OS
 * thread simply ran to completion.
 */

#include "NXThreads.h"

#include <pthread.h>

/* Referenced by the static anchor in NXThreads.h, so this archive member is
   always linked. */
extern "C" void nx_threads_link_anchor(void)
{
}

extern "C" int __syscall_thread_detach(pthread_t thread)
{
	(void) thread;
	return 0;
}
