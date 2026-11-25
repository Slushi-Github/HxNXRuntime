#include <hxcpp.h>
#include <hx/OS.h>

#if defined(HX_NX)

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

// Declare POSIX functions if newlib's feature guards hid them in C++ mode
extern "C" FILE *fdopen(int, const char *);
extern "C" int fileno(FILE *);

static inline int hx_nx_fseeko(FILE *stream, off_t offset, int whence)
{
   return fseek(stream, (long)offset, whence);
}

static inline off_t hx_nx_ftello(FILE *stream)
{
   return (off_t)ftell(stream);
}

#define fseeko hx_nx_fseeko
#define ftello hx_nx_ftello

// Include original File.cpp without modifying it
#include "File.cpp"

#undef fseeko
#undef ftello

#endif // defined(HX_NX)