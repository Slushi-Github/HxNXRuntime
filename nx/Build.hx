package nx;

#if macro
import haxe.io.Path;
import haxe.macro.Context;
import sys.FileSystem;
import sys.io.File;

/**
 * Writes this library's HXCPP toolchain into the build directory.
 *
 * The library is selected with -D toolchain=nx (see `extraParams.hxml`), so
 * hxcpp looks for "toolchain/nx-toolchain.xml" next to the generated
 * Build.xml.
 */
class Build {
	public static function init():Void {
		requireLibhxcpp();

		final outDir = findOutputDir();
		if (outDir == null) {
			Context.fatalError("HxNXRuntime: no -cpp output directory found, the NX toolchain cannot be written", Context.currentPos());
		}

		final source = findToolchainSource();
		if (source == null) {
			Context.fatalError("HxNXRuntime: cannot locate xml/nx-toolchain.xml", Context.currentPos());
		}

		final dest = Path.join([outDir, "toolchain", "nx-toolchain.xml"]);
		mkdirs(Path.directory(dest));
		File.saveContent(dest, File.getContent(source));
	}

	//////////////////////////////////

	/**
	 * HxNXRuntime is only supported on libhxcpp, the OpenFL continuation of hxcpp,
	 * not on stock hxcpp. libhxcpp adds the "libhxcpp" define from its own
	 * extraParams.hxml, so its presence is a reliable marker.
	 *
	 * The check is deferred because the -lib order can load HxNXRuntime before
	 * hxcpp, in which case the define does not exist yet while this macro runs.
	 */
	static function requireLibhxcpp():Void {
		if (Context.defined("libhxcpp"))
			return;

		final pos = Context.currentPos();

		Context.onAfterTyping(function(_) {
			if (Context.defined("libhxcpp"))
				return;

			final version = Context.definedValue("hxcpp");
			final found = version == null ? "stock hxcpp" : 'stock hxcpp $version';

			Context.fatalError(
				'HxNXRuntime only works with libhxcpp, but $found was detected.\n' +
				'Install libhxcpp and make "-lib hxcpp" resolve to it:\n' +
				'  haxelib install libhxcpp\n' +
				'  haxelib run libhxcpp setup\n' +
				'For a git checkout: haxelib git libhxcpp https://github.com/openfl/libhxcpp',
				pos
			);
		});
	}

	/** 
	 * Reads the target directory from the Haxe command line (expanded from .hxml too). 
	 */
	static function findOutputDir():Null<String> {
		var args = Sys.args();
		var dir:Null<String> = null;
		var i = 0;
		while (i < args.length) {
			var arg = args[i];
			if (arg == "-cpp" || arg == "--cpp") {
				if (i + 1 < args.length) {
					dir = args[i + 1];
					break;
				}
			}
			if (StringTools.startsWith(arg, "-cpp=")) {
				dir = arg.substr("-cpp=".length);
				break;
			}
			i++;
		}
		if (dir == null || dir == "")
			return null;
		if (!Path.isAbsolute(dir))
			dir = Path.join([Sys.getCwd(), dir]);
		return Path.normalize(dir);
	}

	/** 
	 * Path of the XML fragment shipped with this library. 
	 */
	static function findToolchainSource():Null<String> {
		try {
			var own = Context.resolvePath("nx/Build.hx");
			if (own != null) {
				var libDir = Path.directory(Path.directory(own));
				var candidate = Path.join([libDir, "xml", "nx-toolchain.xml"]);
				if (FileSystem.exists(candidate))
					return candidate;
			}
		} catch (e:Dynamic) {}
		return null;
	}

	/** 
	 * Creates a directory recursively. 
	 */
	static function mkdirs(dir:String):Void {
		if (dir == null || dir == "" || dir == "." || FileSystem.exists(dir))
			return;
		var parent = Path.directory(dir);
		if (parent != null && parent != "" && parent != dir)
			mkdirs(parent);
		FileSystem.createDirectory(dir);
	}
}
#end
