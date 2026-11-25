#ifndef __BSD_VISIBLE
#define __BSD_VISIBLE 1
#endif

#include <hxcpp.h>
#include <hx/OS.h>

#if defined(HX_NX)

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((in_addr_t)-1)
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

// Provide minimal IPv6 structs if omitted/incomplete in devkitA64 headers
// so the dropped IPv6 functions in Socket.cpp parse cleanly.
#ifndef HX_NX_HAS_IN6_ADDR
#define HX_NX_HAS_IN6_ADDR
struct in6_addr
{
   union
   {
      uint8_t  __u6_addr8[16];
      uint16_t __u6_addr16[8];
      uint32_t __u6_addr32[4];
   } __in6_u;
#define s6_addr   __in6_u.__u6_addr8
#define s6_addr16 __in6_u.__u6_addr16
#define s6_addr32 __in6_u.__u6_addr32
};
#endif

#ifndef HX_NX_HAS_SOCKADDR_IN6
#define HX_NX_HAS_SOCKADDR_IN6
struct sockaddr_in6
{
   sa_family_t     sin6_family;
   in_port_t       sin6_port;
   uint32_t        sin6_flowinfo;
   struct in6_addr sin6_addr;
   uint32_t        sin6_scope_id;
};
#endif

#ifndef HOST_NOT_FOUND
#define HOST_NOT_FOUND 1
#endif
#ifndef TRY_AGAIN
#define TRY_AGAIN 2
#endif
#ifndef NO_RECOVERY
#define NO_RECOVERY 3
#endif
#ifndef NO_DATA
#define NO_DATA 4
#endif

// Fallback resolver when gethostbyname / gethostbyname_r are not declared in newlib headers
static inline struct hostent *hx_nx_gethostbyname(const char *name)
{
   (void)name;
   return NULL;
}

static inline int hx_nx_gethostbyname_r(
   const char *name,
   struct hostent *ret,
   char *buf,
   size_t buflen,
   struct hostent **result,
   int *h_errnop)
{
   (void)name;
   (void)ret;
   (void)buf;
   (void)buflen;
   if (result)
      *result = NULL;
   if (h_errnop)
      *h_errnop = HOST_NOT_FOUND;
   return -1;
}

#define gethostbyname   hx_nx_gethostbyname
#define gethostbyname_r hx_nx_gethostbyname_r

// Define functions that are missing in the DevKitA64/libnx headers, and provide empty implementations for those that are not supported on Switch
#define gethostbyaddr_r(...)  0
#define gethostname(buf,len)  (-1)

// Redefine the functions that are imposible to implement on DevKitA64/libnx
// But we need to keep the original implementations, so we implement them as empty functions
// The IPv4 resolver is replaced too: the original one requires gethostbyname_r,
// which DevKitA64/libnx does not provide. Our version uses DevKitA64/libnx's gethostbyname below.
#define _hx_std_host_resolve         static _hx_nx_drop_host_resolve
#define _hx_std_host_resolve_ipv6    static _hx_nx_drop_host_resolve_ipv6
#define _hx_std_socket_connect_ipv6  static _hx_nx_drop_connect_ipv6
#define _hx_std_socket_bind_ipv6     static _hx_nx_drop_bind_ipv6

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// Include original Socket.cpp without modifying it
#include "Socket.cpp"

#pragma GCC diagnostic pop

// Undefine the functions that we redefined, so we can implement them as empty functions
#undef gethostbyname
#undef gethostbyname_r
#undef gethostbyaddr_r
#undef gethostname
#undef _hx_std_host_resolve
#undef _hx_std_host_resolve_ipv6
#undef _hx_std_socket_connect_ipv6
#undef _hx_std_socket_bind_ipv6
#undef s6_addr
#undef s6_addr16
#undef s6_addr32

// IPv4 resolver for HorizonOS. DevKitA64/libnx (sfdnsres) provides getaddrinfo(),
// while the original hxcpp implementation requires gethostbyname_r(),
// which is not available in devkitA64/libnx.
int _hx_std_host_resolve( String host )
{
   if(host == HX_CSTRING("255.255.255.255"))
      return (int)0xFFFFFFFF;

   unsigned int ip;

   hx::EnterGCFreeZone();
   hx::strbuf buf;
   ip = inet_addr(host.utf8_str(&buf));
   if( ip == INADDR_NONE )
   {
      hx::strbuf hostBuf;
      struct addrinfo hints;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;

      struct addrinfo *res = 0;
      int err = getaddrinfo(host.utf8_str(&hostBuf), NULL, &hints, &res);
      if( err != 0 || res == 0 )
      {
         if( res )
            freeaddrinfo(res);
         hx::ExitGCFreeZone();
         return hx::Throw( HX_CSTRING("Unknown host: ") + host);
      }

      struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
      ip = addr->sin_addr.s_addr;
      freeaddrinfo(res);
   }
   hx::ExitGCFreeZone();
   return ip;
}

// Implement empty functions
Array<unsigned char> _hx_std_host_resolve_ipv6( String host, bool )
{
   (void)host;
   return Array_obj<unsigned char>::__new();
}

void _hx_std_socket_connect_ipv6( Dynamic o, Array<unsigned char> host, int port )
{
   (void)o;
   (void)host;
   (void)port;
}

void _hx_std_socket_bind_ipv6( Dynamic o, Array<unsigned char> host, int port )
{
   (void)o;
   (void)host;
   (void)port;
}

#endif // defined(HX_NX)
