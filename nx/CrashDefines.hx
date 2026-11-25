package nx;

#if macro
import haxe.macro.Compiler;
import haxe.macro.Context;

/**
 * Adds the Haxe-level defines the NX crash handler (cpp/nx/NXCrash.cpp) uses.
 *
 * Called from `extraParams.hxml`, so it runs for every build that links the
 * library.
 */
class CrashDefines {
	static inline var DISABLE = "HXCPP_NX_NO_CRASH_HANDLER";

	static final DEFINES = [
		"HXCPP_CHECK_POINTER",
		"HXCPP_STACK_TRACE",
		"HXCPP_STACK_LINE"
	];

	//////////////////////////////////

	public static function init():Void {
		if (Context.defined(DISABLE)) {
			return;
		}

		for (define in DEFINES) {
			if (!Context.defined(define)) {
				Compiler.define(define);
			}
		}
	}
}
#end
