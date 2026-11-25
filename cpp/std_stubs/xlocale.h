#pragma once

// Include the correct xlocale header
#if defined(__NX__) || defined(HX_NX) || defined(__SWITCH__)
#include <locale.h>
#endif