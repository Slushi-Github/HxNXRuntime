<h1 align="center">HxNXRuntime</h1>
<h2 align="center">A Nintendo Switch target for libhxcpp</h2>

So, forking hxcpp every time it's a lot of work for me... Why not find a way to compile the Switch with HXCPP without modifying HXCPP?

This is HxNXRuntime, a Nintendo Switch runtime support for [libhxcpp](https://github.com/openfl/libhxcpp), injected into the build **without forking hxcpp**.

HxNXRuntime adds the NX toolchain, the NX runtime sources and the socket lifecycle and more, on top of an unmodified libhxcpp. libhxcpp stays clean, so it can be updated independently!

> **Requires libhxcpp.** Stock hxcpp is rejected at compile time with a clear error.

> **Status:** ultra-experimental, tested on real hardware (Nintendo Switch V2, HorizonOS 22.5.0, Atmosphere 1.11.2) with Haxe 4.3.7 and libhxcpp 6.0.0.

> **About the future of hxcpp-nx:** I'm planning to keep HxNXRuntime as a separate library for the moment, and to make libhxcpp more feature-complete. The current version is a proof-of-concept, this is a very different way for me of integrating the Switch support to HXCPP and Haxe, but I think it's the future and the best way to support the creating homebrew with Haxe on the Switch.

## What it does

- Installs the hxcpp toolchain for devkitA64 / libnx.
- Replaces the host-specific runtime sources with NX implementations:
  - `File`, `Sys`, `Process` and `Socket`
  - `Date`
  - SSL/mbedTLS, loading the console's root certificates
- Owns the libnx socket lifecycle:
  - starts the default socket before Haxe runs, and
  - closes it with `socketExit()` on normal exit, on `exit()`, and after an uncaught Haxe exception.
- Adds the official `nx_perf_*` performance API (`apm:am` + `applet`).
- Offers an optional `HXCPP_HX_NX_SYS_EXTRA` mode (disabled by default) with an in-memory environment and a working current directory.
- Implements a native crash handler, with optional crash reports to SD card.
- Refuses to start in applet mode when the project opts in, with a graphical notice and a clean return to hbmenu.
- Keeps libhxcpp untouched: every NX change lives in this library.

## Requirements

- Haxe 4.3.x
- devkitPro with devkitA64 and libnx (`DEVKITPRO` set)
- [libhxcpp](https://github.com/openfl/libhxcpp) (not stock hxcpp)
- [hx_libnx](https://github.com/Slushi-Github/hx_libnx) for the Switch bindings
- [HaxeNXCompiler](https://github.com/Slushi-Github/HaxeNXCompiler) or Lime (Coming soon)

The first macro in `extraParams.hxml` checks `DEVKITPRO`, the libnx headers and library, and the devkitA64 tools before compiling. If something is missing it stops with a single list of what was not found.

## Installation

```bash
# libhxcpp, the runtime HxNXRuntime extends
haxelib git libhxcpp https://github.com/openfl/libhxcpp

# Make "-lib hxcpp" resolve to libhxcpp
haxelib run libhxcpp setup
```

```bash
# Nintendo Switch bindings
haxelib git hx_libnx https://github.com/Slushi-Github/hx_libnx
```

```bash
# HxNXRuntime
haxelib git hx-nx-runtime https://github.com/Slushi-Github/HxNXRuntime
```

For a local checkout:

```bash
haxelib dev hx-nx-runtime /path/to/HxNXRuntime
```

## Usage

### With HaxeNXCompiler 4.0.0+

Nothing to add. The compiler injects `-lib hx-nx-runtime` automatically, so declaring it in `HxNXProject.xml` is optional (it is not duplicated).

### With Lime-NX:

it's a TODO. It uses the old hxcpp-nx workflow.

### With plain hxcpp / hxml

Just add the library:

```hxml
-cp source
-main Main
-lib hxcpp
-lib hx_libnx
-lib hx-nx-runtime
-cpp output
```

# What it does in detail

HxNXRuntime's `extraParams.hxml` adds the NX configuration:

```hxml
-D nx
-D toolchain=nx
-D BINDIR=Switch
--macro nx.CrashDefines.init()
--macro nx.Build.init()
```

`nx.CrashDefines.init()` only sets the crash handler defines when the build did
not ask for `HXCPP_NX_NO_CRASH_HANDLER`. `nx.Build.init()` writes
`toolchain/nx-toolchain.xml` next to the generated `Build.xml`, and the
toolchain overrides the runtime file groups.

## Socket lifecycle

The runtime starts `socketInitializeDefault()` from a static constructor, before `main()` and therefore before any Haxe code. It only does so when the socket is not already running, and only calls `socketExit()` if it was the one that started it.

`socketExit()` is registered with `atexit()`, so it runs when:

- `main()` returns normally,
- the crash handler catches an uncaught Haxe exception and returns,
- the program calls `exit()`.

You do not need to call `socketInitializeDefault()` or `socketExit()` from Haxe.

## Threads

Two Switch-specific fixes are applied to the hxcpp/libstdc++ thread runtime:

- `xml/nx-files.xml` swaps the semaphore implementation inside hxcpp's `runtime` group. libhxcpp would otherwise pick `CountingSemaphore.unsupported.cpp` on Switch, whose `acquire()`/`release()` throw `"Unsupported platform"`; the override keeps hxcpp's thread file list and uses the POSIX one instead (newlib/libnx provide `sem_init`/`sem_wait`/`sem_post` through `libsysbase`).
- `cpp/nx/NXThreads.cpp` supplies `__syscall_thread_detach`. DevKitA64's `libsysbase` implements `pthread_detach()` as a jump through that hook but does not install it, so `pthread_detach()` returns `ENOSYS`; libstdc++ turns that into a `std::system_error` from `std::thread::detach()`, and hxcpp always detaches. The no-op hook makes detach succeed, matching hxcpp 4.3.x behaviour on Switch.

`NXThreads.h` is force-included by the toolchain and its static anchor keeps the archive member in the link, because the `libsysbase` reference is weak.

## Optional Sys extensions (`HXCPP_HX_NX_SYS_EXTRA`)

`HXCPP_HX_NX_SYS_EXTRA` is **disabled by default** and must be enabled per project. It changes how `Sys` and `FileSystem` behave, and a project that relied on the previous behaviour should keep the define off.

```xml
<haxedef name="HXCPP_HX_NX_SYS_EXTRA" />
```

Without the define, `Sys` keeps the conservative NX behaviour. With the define:

- `Sys.getEnv` / `Sys.putEnv` use a thread-safe in-memory map, since HorizonOS does not expose a process environment. `getEnv` returns `null` for missing variables, `putEnv(name, null)` removes one, and values are UTF-8 and may contain `=`.
- `Sys.getCwd` starts at the real process directory (`sdmc:/switch/` when launched from hbmenu) and always ends with `/`.
- `Sys.setCwd` accepts absolute (`sdmc:/...`) and relative paths, normalizes `.`, `..` and backslashes, checks that the target exists and then changes the real newlib/fsdev cwd, so relative `File`/`FileSystem` calls follow it. If the directory does not exist, the cwd is left unchanged.
- `FileSystem.fullPath` resolves relative paths against the current directory and returns `null` if the path does not exist.
- `Sys.programPath()` returns `argv[0]`, for example `sdmc:/switch/app.nro`.

## Performance API

These functions are for changing the performance mode of the console, and restoring the original configuration.

```haxe
// 0 = restore/default, 1 = max performance, 2 = fast load, 3 = power saver
untyped __global__.nx_perf_apply(1);

// Restore the configuration captured before the first change
untyped __global__.nx_perf_restore();
```

| Profile | Value | Meaning |
|---|---:|---|
| `Default` | `0` | Restore the original configuration |
| `Performance` | `1` | Highest official clocks for the current operation mode |
| `FastLoad` | `2` | CPU 1785 MHz / minimum GPU, for loading screens |
| `PowerSaver` | `3` | Lowest official clocks for the current operation mode |

The implementation only uses interfaces Nintendo exposes to regular applications:

- `apm:am` (`apmInitialize`, `apmSetPerformanceConfiguration`)
- `applet` (`appletGetOperationMode`, `appletSetCpuBoostMode`)

Useful helpers:

```haxe
untyped __global__.nx_perf_is_available();
untyped __global__.nx_perf_get_operation_mode();   // 0 = handheld, 1 = docked
untyped __global__.nx_perf_refresh();              // re-apply after dock/undock
untyped __global__.nx_perf_shutdown();
```

To keep the API available but never touch the hardware, build with:

```xml
<haxedef name="HXCPP_NX_NO_OVERCLOCK" />
```

or in plain hxml:

```hxml
-D HXCPP_NX_NO_OVERCLOCK
```

The `nx_perf_*` symbols still exist, but become stubs.

## Crash handler

Now HxNXRuntime has a crash handler, under the file `cpp/nx/NXCrash.*`. It
writes a report file under `sdmc:/switch/HxNXRuntime/nativeCrashes` or can be
saved to another location with `nxCrashInstall()`.

The crash handler uses another way to render the report on the screen, it not uses the normal libnx text console, so it uses the framebuffer to display the report using the same font as the libnx text console

This is useful, because not always the Haxe code can be catched by a normal crash handler, like the one in OpenFL? And in cases like using the applet mode, it can crash the whole system, with this handler it will just show the report on the screen, and then you can press the PLUS button (Or wait 24 seconds) to return to the homebrew menu.

The three defines that make the report useful live by default, with no manual
step:

| Stage | Where | What it does |
| --- | --- | --- |
| Haxe | `nx/CrashDefines.hx`, called from `extraParams.hxml` | Defines `HXCPP_CHECK_POINTER`, `HXCPP_STACK_TRACE` and `HXCPP_STACK_LINE` unless they are already set, so generated code carries function names and file:line |
| C++ | `xml/nx-toolchain.xml` | Forces the matching `-DHXCPP_*` macros |

`-D HXCPP_NX_NO_CRASH_HANDLER` turns both off: the macro adds nothing and the
toolchain skips the C++ flags, while `NXCrash.cpp`'s functions stay available
and simply do nothing.

At runtime, the application can sets the names shown on the
crash screen and the folder that receives the logs and what addons are used.

```cpp
nxCrashSetProgram("My Program");                   // default: "Haxe program"
nxCrashAddAddon("HaxeNXCompiler");                 // repeatable, omitted when empty
nxCrashInstall("sdmc:/switch/Game/nativeCrashes"); // folder, created if missing
```

Every crash gets it's own file, `crash_YYYYMMDD_HHMMSS.log` (with `_2`, `_3`...
if two land in the same second), so reports are never mixed or overwritten.
With `nxCrashInstall(NULL)` nothing is written automatically; the crash screen
offers to save it under `sdmc:/switch/HxNXRuntime/nativeCrashes/` with the MINUS button.

## Applet-mode guard

HxNXRuntime also has an applet-mode guard, under `cpp/nx/NXAppletMode.*`. It exists because homebrew launched from the
Album runs as a library applet, which has less memory than a full application.
A large program can run out of memory there and take the whole console down, so
the guard refuses to start instead of letting that happen.

The guard is meant to run at the very start of `main()`, before something like SDL, OpenGL, nxlink, or any Haxe code exist. It asks `appletGetAppletType()`:

| Result | What happens |
| --- | --- |
| `AppletType_Application` | Returns immediately; the program starts as usual |
| `AppletType_LibraryApplet` | Draws a notice and blocks |

When it blocks, it uses the same framebuffer renderer as the crash handler
(`NXScreen`), so there is no text console behind it and nothing is sent to
nxlink. It shows the program name, explains that the program must be started
from a full application, and waits for the PLUS button (or about 60 seconds)
before exiting.

The guard is opt-in, because not every program needs the extra memory. With
Lime-Switch, define `LIME_SWITCH_NO_APPLET` in the project:

```xml
<haxedef name="LIME_SWITCH_NO_APPLET" />
```

Then, `Main.cpp` then calls `nxAppletModeGuard("AppName")` before initialising
anything. The define only decides whether the call is emitted; the function
itself checks the applet type and lets a normal application pass.

The implementation lives in HxNXRuntime, so any project that can call the
function gets the same behavior. `NXScreen.*` is shared with `NXCrash.*` and
contains the framebuffer, the libnx font and the block-linear conversion, so
the screen code is not duplicated.

## Project layout

The project layout for details:

| Path | Purpose |
|---|---|
| `extraParams.hxml` | Selects the NX toolchain and runs `nx.Build.init()` |
| `nx/Build.hx` | Writes `toolchain/nx-toolchain.xml` into the output directory |
| `xml/nx-toolchain.xml` | devkitA64 toolchain, libnx includes, forced headers, runtime groups |
| `xml/nx-files.xml` | Replaces host runtime sources with the NX versions |
| `xml/nx-files.xml` | Also selects hxcpp's POSIX semaphore implementation on Switch |
| `cpp/nx/NXSocketRuntime.*` | Socket lifecycle and link anchor |
| `cpp/nx/NXThreads.*` | `pthread_detach` hook and link anchor |
| `cpp/nx/NXPerformance.*` | `nx_perf_*` implementation and stubs |
| `cpp/nx/NXCrash.*` | Native Crash report handler |
| `cpp/nx/NXScreen.*` | Framebuffer text renderer shared by the crash and applet screens |
| `cpp/nx/NXAppletMode.*` | Applet-mode guard (`nxAppletModeGuard`) |
| `cpp/std/NX*.cpp` | NX implementations of File, Sys, Process and Socket |
| `cpp/std_stubs/*.h` | Stubs for missing DevkitA64/libnx headers |
| `cpp/NXDate.cpp` | NX Date implementation |
| `cpp/ssl/NXSSL*` | SSL/mbedTLS integration and root certificates |
| `cpp/ssl/NXExtraCACerts.h` | Extra root certificates |

## Troubleshooting

### Error: `HxNXRuntime only works with libhxcpp`

`-lib hxcpp` is resolving to stock hxcpp. Install libhxcpp and run it's setup:

```bash
haxelib install libhxcpp
haxelib run libhxcpp setup
```

### Error: `Socket already initialized`

This happens if both HxNXRuntime and the legacy compiler wrapper call `socketInitializeDefault()`. Use the modern HaxeNXCompiler layout, or keep the legacy layout without HxNXRuntime.

### The compiler cannot find `hx-nx-runtime`

HaxeNXCompiler 4.0.0+ checks the required libraries before compiling and prints the install commands. For a local checkout, use

```
haxelib dev hx-nx-runtime /path/to/HxNXRuntime
``` 
or:

```
haxelib git hx-nx-runtime https://github.com/Slushi-Github/HxNXRuntime
```

## License

HxNXRuntime is released under the [MIT License](LICENSE.md).
