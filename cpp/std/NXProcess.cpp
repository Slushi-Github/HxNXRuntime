#include <hxcpp.h>
#include <hx/OS.h>

// On HorizonOS, we don't have a process API, or a least it's not worth it
// So, we just return null for everything

Dynamic _hx_std_process_run( String cmd, Array<String> vargs, int inShowParam ) { return null(); }
int _hx_std_process_stdout_read( Dynamic handle, Array<unsigned char> buf, int pos, int len ) { return 0; }
int _hx_std_process_stderr_read( Dynamic handle, Array<unsigned char> buf, int pos, int len ) { return 0; }
int _hx_std_process_stdin_write( Dynamic handle, Array<unsigned char> buf, int pos, int len ) { return 0; }
void _hx_std_process_stdin_close( Dynamic handle ) { }
#if (HXCPP_API_LEVEL > 420)
Dynamic _hx_std_process_exit( Dynamic handle, bool block ) { return null(); }
#else
int _hx_std_process_exit( Dynamic handle ) { return 0; }
#endif
int _hx_std_process_pid( Dynamic handle ) { return 0; }
void _hx_std_process_close( Dynamic handle ) { }
void _hx_std_process_kill( Dynamic handle ) { }
Dynamic _hx_std_process_communicate( Dynamic handle, Array<unsigned char> input, Float timeout, int maxOutput ) { return null(); }