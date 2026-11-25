#pragma once

// Include the correct termios header
#if defined(__NX__) || defined(HX_NX) || defined(__SWITCH__)
#include <machine/termios.h>

#ifndef HX_NX_STRUCT_TERMIOS_DEFINED
#define HX_NX_STRUCT_TERMIOS_DEFINED
struct termios
{
   unsigned int c_iflag;
   unsigned int c_oflag;
   unsigned int c_cflag;
   unsigned int c_lflag;
   unsigned char c_cc[32];
   unsigned int c_ispeed;
   unsigned int c_ospeed;
};
#endif
#endif