#include <hxcpp.h>
#include <hx/OS.h>

#if defined(HX_NX)
#include <switch/arm/counter.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

extern int _hxcpp_argc;
extern char **_hxcpp_argv;

// Redefine functions that will be overridden or custom-implemented for Switch,
#define _hx_std_get_env        static _hx_nx_drop_get_env
#define _hx_std_put_env        static _hx_nx_drop_put_env
#define _hx_std_get_cwd        static _hx_nx_drop_get_cwd
#define _hx_std_set_cwd        static _hx_nx_drop_set_cwd
#define _hx_std_sys_string     static _hx_nx_drop_sys_string
#define _hx_std_sys_command    static _hx_nx_drop_sys_command
#define _hx_std_sys_cpu_time   static _hx_nx_drop_sys_cpu_time
#define _hx_std_file_full_path static _hx_nx_drop_file_full_path
#define _hx_std_sys_exe_path   static _hx_nx_drop_sys_exe_path
#define _hx_std_sys_getch      static _hx_nx_drop_sys_getch
#define _hx_std_sys_get_pid    static _hx_nx_drop_sys_get_pid

// Ensure POSIX/BSD symbols and termios declarations are available when Sys.cpp
// is compiled under DevKitA64/libnx, and avoid #error in the dropped sys_string.
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <sys/wait.h>
#include <machine/termios.h>

extern "C" int unsetenv(const char *);
extern "C" int setenv(const char *, const char *, int);
extern "C" void tzset(void);
extern "C" char *realpath(const char *, char *);
extern "C" ssize_t readlink(const char *, char *, size_t);
extern "C" int fileno(FILE *);
extern "C" int tcgetattr(int, struct termios *);
extern "C" int tcsetattr(int, int, const struct termios *);
extern "C" void cfmakeraw(struct termios *);
extern "C" int chdir(const char *);

#ifndef WEXITSTATUS
#define WEXITSTATUS(x) (((x) >> 8) & 0xff)
#endif
#ifndef WTERMSIG
#define WTERMSIG(x) ((x) & 0x7f)
#endif

#define HX_NX_TEMP_SYSTRING_GUARD
#ifndef NEKO_LINUX
#define NEKO_LINUX
#define HX_NX_UNDEF_NEKO_LINUX
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// Include the original Sys.cpp without modifying it
#include "Sys.cpp"

#pragma GCC diagnostic pop

#if defined(HX_NX_UNDEF_NEKO_LINUX)
#undef NEKO_LINUX
#undef HX_NX_UNDEF_NEKO_LINUX
#endif
#undef HX_NX_TEMP_SYSTRING_GUARD

// Undefine the macros so we can implement the real symbols
#undef _hx_std_get_env
#undef _hx_std_put_env
#undef _hx_std_get_cwd
#undef _hx_std_set_cwd
#undef _hx_std_sys_string
#undef _hx_std_sys_command
#undef _hx_std_sys_cpu_time
#undef _hx_std_file_full_path
#undef _hx_std_sys_exe_path
#undef _hx_std_sys_getch
#undef _hx_std_sys_get_pid

// Internal helpers for Switch
namespace
{
   #if defined(HXCPP_HX_NX_SYS_EXTRA)
   std::mutex &nxEnvMutex()
   {
      static std::mutex *sMutex = new std::mutex();
      return *sMutex;
   }

   std::unordered_map<std::string, std::string> &nxEnvMap()
   {
      static std::unordered_map<std::string, std::string> *sMap = new std::unordered_map<std::string, std::string>();
      return *sMap;
   }

   std::mutex &nxCwdMutex()
   {
      static std::mutex *sMutex = new std::mutex();
      return *sMutex;
   }

   // Normalize path separators and resolve '.' and '..' while preserving
   // device prefixes (such as "sdmc:/" or "romfs:/") when present.
   std::string nxNormalizePath(const std::string &raw)
   {
      std::string s = raw;
      for (size_t i = 0; i < s.size(); ++i)
      {
         if (s[i] == '\\')
            s[i] = '/';
      }

      std::string prefix;
      size_t schemePos = s.find(":/");
      if (schemePos != std::string::npos)
      {
         prefix = s.substr(0, schemePos + 2); // e.g. "sdmc:/"
         s = s.substr(schemePos + 2);
      }
      else if (!s.empty() && s[0] == '/')
      {
         prefix = "/";
         s = s.substr(1);
      }

      std::vector<std::string> parts;
      size_t start = 0;
      while (start <= s.size())
      {
         size_t slash = s.find('/', start);
         std::string token = (slash == std::string::npos)
            ? s.substr(start)
            : s.substr(start, slash - start);

         if (token.empty() || token == ".")
         {
            // Ignore empty segments and current directory markers
         }
         else if (token == "..")
         {
            if (!parts.empty())
               parts.pop_back();
         }
         else
         {
            parts.push_back(token);
         }

         if (slash == std::string::npos)
            break;
         start = slash + 1;
      }

      std::string out = prefix;
      for (size_t i = 0; i < parts.size(); ++i)
      {
         if (i > 0)
            out += "/";
         out += parts[i];
      }
      return out;
   }

   // Ensure directory paths keep a trailing slash, preserving device roots like "sdmc:/"
   std::string nxEnsureTrailingSlash(std::string path)
   {
      if (path.empty())
         return path;
      if (path.back() != '/')
         path += "/";
      return path;
   }

   // Returns the real process cwd. libnx starts it at the NRO directory via
   // __libnx_init_cwd(), and newlib may report it relative to the default
   // device ("/switch"), so normalize it to "sdmc:/switch" when needed.
   std::string nxPosixCwd()
   {
      char buf[PATH_MAX];
      if (getcwd(buf, sizeof(buf)) == nullptr)
         return std::string();

      std::string cwd(buf);
      if (!cwd.empty() && cwd[0] == '/' && cwd.find(":/") == std::string::npos)
         cwd = "sdmc:" + cwd;
      return cwd;
   }

   std::string &nxMutableCwd()
   {
      static std::string *sCwd = []() {
         auto *p = new std::string();
         std::string real = nxPosixCwd();
         if (!real.empty())
            *p = real;
         else if (_hx_std_sys_exists(HX_CSTRING("sdmc:/")))
            *p = "sdmc:/";
         else
            *p = "/";
         return p;
      }();
      return *sCwd;
   }
   #endif // defined(HXCPP_HX_NX_SYS_EXTRA)
}

String _hx_std_get_env( String v )
{
   #if defined(HXCPP_HX_NX_SYS_EXTRA)
   if (v == null() || v.length == 0)
      return String();

   hx::strbuf buf;
   const char *keyPtr = v.utf8_str(&buf);
   std::string key(keyPtr ? keyPtr : "");
   if (key.empty())
      return String();

   std::string val;
   bool found = false;
   {
      std::lock_guard<std::mutex> lock(nxEnvMutex());
      auto it = nxEnvMap().find(key);
      if (it != nxEnvMap().end())
      {
         val = it->second;
         found = true;
      }
   }

   if (!found)
      return String();

   return String::create(val.c_str(), (int)val.size());
   #else
   (void)v;
   return String();
   #endif
}

void _hx_std_put_env( String e, String v )
{
   #if defined(HXCPP_HX_NX_SYS_EXTRA)
   if (e == null() || e.length == 0)
      return;

   hx::strbuf kbuf;
   const char *keyPtr = e.utf8_str(&kbuf);
   std::string key(keyPtr ? keyPtr : "");
   if (key.empty())
      return;

   std::lock_guard<std::mutex> lock(nxEnvMutex());
   if (v == null())
   {
      nxEnvMap().erase(key);
   }
   else
   {
      hx::strbuf vbuf;
      const char *valPtr = v.utf8_str(&vbuf);
      nxEnvMap()[key] = valPtr ? std::string(valPtr) : std::string();
   }
   #else
   (void)e;
   (void)v;
   #endif
}

String _hx_std_get_cwd()
{
   #if defined(HXCPP_HX_NX_SYS_EXTRA)
   std::string cwd;
   {
      std::lock_guard<std::mutex> lock(nxCwdMutex());
      cwd = nxMutableCwd();
   }

   if (cwd.empty())
      return String();

   cwd = nxEnsureTrailingSlash(cwd);
   return String::create(cwd.c_str(), (int)cwd.size());
   #else
   if ( _hx_std_sys_exists(HX_CSTRING("sdmc:/")) )
      return HX_CSTRING("sdmc:/");
   return String();
   #endif
}

bool _hx_std_set_cwd( String d )
{
   #if defined(HXCPP_HX_NX_SYS_EXTRA)
   if (d == null() || d.length == 0)
      return false;

   hx::strbuf buf;
   const char *rawPtr = d.utf8_str(&buf);
   std::string input(rawPtr ? rawPtr : "");
   if (input.empty())
      return false;

   // OpenFL's SharedObject.__mkdir switches to the device root with
   // "sdmc:\" (backslash). Normalize separators before deciding whether the
   // path is absolute, otherwise "sdmc:\" is treated as a relative name and
   // the chdir fails, leaving directory creation relative to the wrong cwd.
   for (size_t i = 0; i < input.size(); ++i)
   {
      if (input[i] == '\\')
         input[i] = '/';
   }

   std::string current;
   {
      std::lock_guard<std::mutex> lock(nxCwdMutex());
      current = nxMutableCwd();
   }

   std::string combined;
   if (input.find(":/") != std::string::npos || input[0] == '/')
      combined = input;
   else
      combined = nxEnsureTrailingSlash(current) + input;

   std::string norm = nxNormalizePath(combined);
   if (norm.empty())
      return false;

   norm = nxEnsureTrailingSlash(norm);

   String hxCheck = String::create(norm.c_str(), (int)norm.size());
   if (!_hx_std_sys_exists(hxCheck))
      return false;

   // Change the real process cwd too: File/FileSystem resolve relative paths
   // through newlib and fsdev, so a purely virtual cwd would make them write
   // somewhere else than getCwd()/fullPath() report.
   if (chdir(norm.c_str()) != 0)
      return false;

   {
      std::lock_guard<std::mutex> lock(nxCwdMutex());
      nxMutableCwd() = norm;
   }
   return true;
   #else
   (void)d;
   return false;
   #endif
}

String _hx_std_sys_string()
{
   return HX_CSTRING("HorizonOS");
}

int _hx_std_sys_command( String cmd )
{
   (void)cmd;
   return -1;
}

double _hx_std_sys_cpu_time()
{
   // Return CPU time in seconds to match sys_cpu_time semantics
   return ((double)armTicksToNs(armGetSystemTick()) / 1.0e9);
}

String _hx_std_file_full_path( String path )
{
   #if defined(HXCPP_HX_NX_SYS_EXTRA)
   if (path == null() || path.length == 0)
      return null();

   hx::strbuf buf;
   const char *rawPtr = path.utf8_str(&buf);
   std::string input(rawPtr ? rawPtr : "");
   if (input.empty())
      return null();

   for (size_t i = 0; i < input.size(); ++i)
   {
      if (input[i] == '\\')
         input[i] = '/';
   }

   std::string current;
   {
      std::lock_guard<std::mutex> lock(nxCwdMutex());
      current = nxMutableCwd();
   }

   std::string combined;
   if (input.find(":/") != std::string::npos || input[0] == '/')
      combined = input;
   else
      combined = nxEnsureTrailingSlash(current) + input;

   std::string norm = nxNormalizePath(combined);
   if (norm.empty())
      return null();

   // Preserve trailing slash when requested, without turning device roots like "sdmc:/" into "sdmc://"
   if ((input.back() == '/' || input.back() == '\\') && norm.back() != '/')
      norm += "/";

   String hxPath = String::create(norm.c_str(), (int)norm.size());
   if (!_hx_std_sys_exists(hxPath))
      return null();

   return hxPath;
   #else
   return path;
   #endif
}

String _hx_std_sys_exe_path()
{
   #if defined(HXCPP_HX_NX_SYS_EXTRA)
   // Use _hxcpp_argv[0] set by the wrapper entry point when available
   if (_hxcpp_argc > 0 && _hxcpp_argv && _hxcpp_argv[0] && _hxcpp_argv[0][0] != '\0')
   {
      std::string normalized = nxNormalizePath(_hxcpp_argv[0]);
      if (!normalized.empty())
         return String::create(normalized.c_str(), (int)normalized.size());
   }

   // Optional fallback if set in the in-memory environment map
   String envExe = _hx_std_get_env(HX_CSTRING("NX_EXE_PATH"));
   if (envExe != null() && envExe.length > 0)
      return envExe;

   return HX_CSTRING("");
   #else
   return HX_CSTRING("");
   #endif
}

int _hx_std_sys_getch( bool b )
{
   (void)b;
   return 0;
}

int _hx_std_sys_get_pid()
{
   return 1;
}

#endif // defined(HX_NX)