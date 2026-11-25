package nx;

#if macro
import haxe.io.Path;
import haxe.macro.Context;
import sys.FileSystem;

/**
 * Checks the minimum needed to compile HxNXRuntime before anything else runs.
 *
 * This is the first `--macro` in `extraParams.hxml`, so it fails in seconds
 * with a clear message instead of letting the C++ compiler fail later on a
 * missing switch.h or libnx.a.
 */
class Preflight {
	public static function init():Void {
		final problems:Array<String> = [];
		final devkitPro = Sys.getEnv("DEVKITPRO");

		if (devkitPro == null || devkitPro == "") {
			problems.push("DEVKITPRO is not set");
		} else {
			final libnx = Path.join([devkitPro, "libnx"]);

			requireFile(problems, Path.join([libnx, "include", "switch.h"]), "libnx headers");
			requireFile(problems, Path.join([libnx, "lib", "libnx.a"]), "libnx library");

			final executable = Sys.systemName() == "Windows" ? ".exe" : "";
			final bin = Path.join([devkitPro, "devkitA64", "bin"]);

			// The tools the nx toolchain invokes (CXX, AS, AR, RANLIB, STRIP).
			for (tool in ["aarch64-none-elf-g++", "aarch64-none-elf-as", "aarch64-none-elf-ar", "aarch64-none-elf-ranlib", "aarch64-none-elf-strip"]) {
				requireFile(problems, Path.join([bin, tool + executable]), "devkitA64 toolchain");
			}
		}

		if (problems.length == 0) {
			return;
		}

		Context.fatalError(
			"HxNXRuntime: cannot compile for Nintendo Switch:\n  - " +
			problems.join("\n  - ") +
			"\nInstall devkitPro with the Switch development packages (switch-dev) and set DEVKITPRO.\n" +
			"https://devkitpro.org/wiki/Getting_Started",
			Context.currentPos()
		);
	}

	static function requireFile(problems:Array<String>, path:String, description:String):Void {
		if (!FileSystem.exists(path)) {
			problems.push('$description not found at $path');
		}
	}
}
#end
