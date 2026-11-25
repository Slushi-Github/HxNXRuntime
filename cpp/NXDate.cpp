#if defined(HX_NX)
#include <time.h>
#include <string.h>
#include <switch/arm/counter.h>

static const char *hx_nx_dummy_tzname[2] = { "", "" };

// Redirect hx_read_zone and tzname while including Date.cpp so DevKitA64/libnx can supply its own implementation
#define tzname hx_nx_dummy_tzname
#define hx_read_zone hx_nx_drop_read_zone

// Include original Date.cpp without modifying it
#include "Date.cpp"

#undef hx_read_zone
#undef tzname

static void hx_read_zone(hx_zone_state *outState)
{
   memset(outState, 0, sizeof(*outState));
   // DevKitA64/libnx does not expose global tzname/timezone arrays like glibc;
   // keeping zeroed state avoids missing symbol errors and lets __internal_localtime
   // use localtime_r / hx_local_fields cleanly.
   outState->offset = 0;
}
#endif