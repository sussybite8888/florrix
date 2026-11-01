#!/usr/bin/env node
/**
 * Builds the native desktop programs and packages each platform's as a zip a
 * player can unpack and run:
 *
 *   florr          the client: plays on a server, as the web page does
 *   florr-offline  the offline build: server and client in one program, the
 *                  desktop's counterpart of the offline page. Plays alone,
 *                  with the world kept in the player's own folder
 *                  (offline/native_main.cpp). The whole game's content is
 *                  compiled into it, so it is one file with nothing beside
 *                  it, and it is also written out on its own next to the
 *                  zip. On a Mac it is an app, florr offline.app
 *   data/          the game's content, which the client reads from beside it
 *
 * Platforms:
 *
 *   windows  32-bit x86, cross-compiled with Zig (cpp/cmake/windows-x86-zig.cmake),
 *            so one .exe runs on 32- and 64-bit Windows 7 through 11. SDL2 is
 *            linked in from SDL's own MinGW package, fetched once into
 *            cpp/build-win32/_deps and checked against a pinned hash.
 *            -> cpp/build-win32/florr-windows-x86.zip
 *               cpp/build-win32/florr-offline.exe
 *   mac      this Mac's own architecture, with Homebrew's static SDL2 linked
 *            in (FLIX_STATIC_SDL2) so nothing runs off Homebrew, and built
 *            for the oldest macOS that SDL2 runs on. The offline build is an
 *            app bundle, which Finder opens on a double-click where it would
 *            run a bare program in a Terminal window (see macApp).
 *            -> cpp/build-mac/florr-mac-<arch>.zip
 *               cpp/build-mac/florr offline.app
 *               cpp/build-mac/florr-offline-mac-<arch>.zip  (the app alone)
 *
 * Usage:
 *   node scripts/build-native.js [windows|mac|all]     (default: windows)
 *
 * Env:
 *   FLIX_BUILD    dev | release (default release)
 *
 * Needs cmake and make, Homebrew's sdl2 (the Windows build's font metrics and
 * .exe icon come from host builds of tools/font_table.cpp and
 * tools/app_icon.cpp, which link it), zig for the Windows build, Xcode's
 * command line tools (otool, iconutil, codesign) for the Mac build, and curl,
 * tar and zip.
 */

const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const CPP_DIR = path.join(ROOT, 'cpp');
const flavour = process.env.FLIX_BUILD || 'release';
const jobs = String(os.cpus().length);

/** SDL's MinGW development package, pinned by version and hash. */
const SDL2_VERSION = '2.32.10';
const SDL2_SHA256 = '83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2';

function fail(message) {
    console.error(`\nbuild-native: ${message}\n`);
    process.exit(1);
}

function run(cmd, argv, options = {}) {
    console.log(`> ${cmd} ${argv.join(' ')}`);
    const res = spawnSync(cmd, argv, { cwd: ROOT, stdio: 'inherit', ...options });
    if (res.error) fail(`could not run ${cmd}: ${res.error.message}`);
    if (res.status !== 0) fail(`${cmd} exited with ${res.status}`);
}

function output(cmd, argv) {
    const res = spawnSync(cmd, argv, { encoding: 'utf8' });
    return res.status === 0 ? res.stdout : null;
}

function sha256(file) {
    return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

/** Configures `buildDir` (always: cmake is quick when nothing changed) and builds `targets`. */
function cmakeBuild(buildDir, configureArgs, targets) {
    run('cmake', ['-S', CPP_DIR, '-B', buildDir, `-DFLIX_BUILD=${flavour}`, ...configureArgs]);
    run('cmake', ['--build', buildDir, '--target', ...targets, '--', `-j${jobs}`]);
}

/**
 * A clean package directory holding the two programs (a Mac app is a
 * directory, and copied whole) and the content, and a README that ends on
 * `notes`.
 */
function stage(packageDir, programs, dataDir, notes = []) {
    fs.rmSync(packageDir, { recursive: true, force: true });
    fs.mkdirSync(packageDir, { recursive: true });
    for (const [from, to] of programs) fs.cpSync(from, path.join(packageDir, to), { recursive: true });
    fs.cpSync(dataDir, path.join(packageDir, 'data'), { recursive: true });
    fs.writeFileSync(
        path.join(packageDir, 'README.txt'),
        [
            'florr',
            '',
            `${programs[0][1]}  plays online: it connects to a server (the login`,
            '    screen\'s Advanced Settings say which one). Keep the data folder',
            '    beside it; it reads the game from there.',
            `${programs[1][1]}  plays alone, with no server and no network. The`,
            '    whole game is inside it: copy it anywhere on its own. The world,',
            '    your account and your settings are kept in your own user folder.',
            '',
            ...notes,
        ].join(os.EOL),
    );
}

function windows() {
    if (!output('zig', ['version'])) fail('the Windows build needs zig on the PATH (`brew install zig`)');
    const buildDir = path.join(CPP_DIR, 'build-win32');
    const depsDir = path.join(buildDir, '_deps');
    fs.mkdirSync(depsDir, { recursive: true });

    // SDL2, once.
    const archive = path.join(depsDir, `SDL2-devel-${SDL2_VERSION}-mingw.tar.gz`);
    if (!fs.existsSync(archive)) {
        run('curl', ['-fsSL', '-o', archive,
            `https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz`]);
    }
    if (sha256(archive) !== SDL2_SHA256) {
        fs.rmSync(archive);
        fail(`${path.relative(ROOT, archive)} does not match its pinned hash; deleted, run again to refetch`);
    }
    const sdlRoot = path.join(depsDir, 'sdl2');
    const sdlDir = path.join(sdlRoot, `SDL2-${SDL2_VERSION}`, 'i686-w64-mingw32');
    if (!fs.existsSync(path.join(sdlDir, 'lib', 'libSDL2.a'))) {
        fs.mkdirSync(sdlRoot, { recursive: true });
        run('tar', ['-xzf', archive, '-C', sdlRoot]);
    }

    // The font metrics table and the .exe icon, by builds of their tools this
    // machine can run.
    const hostDir = path.join(buildDir, 'host');
    cmakeBuild(hostDir, [], ['font_table', 'app_icon']);

    cmakeBuild(buildDir, [
        `-DCMAKE_TOOLCHAIN_FILE=${path.join(CPP_DIR, 'cmake', 'windows-x86-zig.cmake')}`,
        `-DFLIX_SDL2_DIR=${sdlDir}`,
        `-DFLIX_FONT_TABLE_TOOL=${path.join(hostDir, 'font_table')}`,
        `-DFLIX_APP_ICON_TOOL=${path.join(hostDir, 'app_icon')}`,
    ], ['flowrix_client', 'flowrix_offline']);

    const name = 'florr-windows-x86';
    const packageDir = path.join(buildDir, 'package', name);
    stage(packageDir, [
        [path.join(buildDir, 'flowrix_client.exe'), 'florr.exe'],
        [path.join(buildDir, 'flowrix_offline.exe'), 'florr-offline.exe'],
    ], path.join(buildDir, 'data'));
    const zip = path.join(buildDir, `${name}.zip`);
    fs.rmSync(zip, { force: true });
    run('zip', ['-qr', zip, name], { cwd: path.dirname(packageDir) });
    const standalone = path.join(buildDir, 'florr-offline.exe');
    fs.copyFileSync(path.join(buildDir, 'flowrix_offline.exe'), standalone);
    console.log(`\nbuild-native: ${path.relative(ROOT, zip)}`);
    console.log(`build-native: ${path.relative(ROOT, standalone)}`);
}

/** Orders macOS versions: "15.0" before "15.6" before "26.1". */
function compareVersions(a, b) {
    const x = a.split('.').map(Number);
    const y = b.split('.').map(Number);
    for (let i = 0; i < Math.max(x.length, y.length); ++i) {
        if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) - (y[i] || 0);
    }
    return 0;
}

/**
 * The oldest macOS a Mach-O program or library runs on: the newest any of its
 * objects was built for (otool's `minos`).
 */
function macMinimum(file) {
    const found = [...(output('otool', ['-l', file]) || '').matchAll(/^\s*minos (\S+)$/gm)].map((m) => m[1]);
    if (found.length === 0) fail(`otool finds no minimum macOS in ${file}`);
    return found.sort(compareVersions).pop();
}

/**
 * The offline build as a Mac app at `appDir`: the same program, as
 * Contents/MacOS/florr-offline, with an Info.plist and the icon
 * (tools/app_icon.cpp's .iconset, packed by iconutil) beside it. It needs
 * nothing else: the content is compiled in, and the world is kept in
 * ~/Library/Application Support/florrix/offline. Signed ad hoc, as the linker
 * signs the bare program -- an Apple Silicon Mac runs nothing unsigned -- but
 * over the whole bundle, so the signature seals the plist and the icon too.
 */
function macApp(buildDir, appDir) {
    const program = 'florr-offline';
    const contents = path.join(appDir, 'Contents');
    const executable = path.join(contents, 'MacOS', program);
    fs.rmSync(appDir, { recursive: true, force: true });
    fs.mkdirSync(path.dirname(executable), { recursive: true });
    fs.mkdirSync(path.join(contents, 'Resources'));
    fs.copyFileSync(path.join(buildDir, 'flowrix_offline'), executable);

    const iconset = path.join(buildDir, 'florr.iconset');
    fs.rmSync(iconset, { recursive: true, force: true });
    run(path.join(buildDir, 'app_icon'), [path.join(buildDir, 'data'), iconset]);
    run('iconutil', ['-c', 'icns', iconset, '-o', path.join(contents, 'Resources', 'florr.icns')]);

    // A bundle's version is integers and dots; the package's may end in letters.
    const { version } = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
    const bundleVersion = (version.match(/^\d+(\.\d+)*/) || ['0'])[0];
    const info = {
        CFBundleDevelopmentRegion: 'en',
        CFBundleDisplayName: 'florr offline',
        CFBundleExecutable: program,
        CFBundleIconFile: 'florr',
        CFBundleIdentifier: 'io.florrix.offline',
        CFBundleInfoDictionaryVersion: '6.0',
        CFBundleName: 'florr offline',
        CFBundlePackageType: 'APPL',
        CFBundleShortVersionString: bundleVersion,
        CFBundleVersion: bundleVersion,
        LSApplicationCategoryType: 'public.app-category.games',
        LSMinimumSystemVersion: macMinimum(executable),
        // Inside a bundle the window's ALLOW_HIGHDPI is honoured only with
        // this; without it a Retina screen is handed a stretched 1x frame.
        NSHighResolutionCapable: true,
    };
    const plist = path.join(contents, 'Info.plist');
    fs.writeFileSync(plist, [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">',
        '<plist version="1.0">',
        '<dict>',
        ...Object.entries(info).map(([key, value]) =>
            `\t<key>${key}</key>\n\t${value === true ? '<true/>' : `<string>${value}</string>`}`),
        '</dict>',
        '</plist>',
        '',
    ].join('\n'));
    run('plutil', ['-lint', '-s', plist]);
    run('codesign', ['--force', '--sign', '-', appDir]);
    run('codesign', ['--verify', '--strict', appDir]);
    return executable;
}

function mac() {
    if (process.platform !== 'darwin') fail('the Mac build is built on a Mac');
    const buildDir = path.join(CPP_DIR, 'build-mac');
    // Left to itself the compiler builds for this Mac's own release, and the
    // programs would refuse every Mac a point release behind it. They go as
    // far back as the SDL2 linked into them does.
    const sdlLib = path.join((output('pkg-config', ['--variable=libdir', 'sdl2']) || '').trim(), 'libSDL2.a');
    if (!fs.existsSync(sdlLib)) fail('the Mac build needs Homebrew\'s static SDL2 (`brew install sdl2`)');
    cmakeBuild(buildDir, ['-DFLIX_STATIC_SDL2=ON', `-DCMAKE_OSX_DEPLOYMENT_TARGET=${macMinimum(sdlLib)}`],
        ['flowrix_client', 'flowrix_offline', 'app_icon']);

    const appName = 'florr offline.app';
    const app = path.join(buildDir, appName);
    const offline = macApp(buildDir, app);
    const client = path.join(buildDir, 'flowrix_client');
    // SDL2 is linked in, so nothing in the zip reaches for Homebrew: all
    // either program loads is the system's own.
    for (const program of [client, offline]) {
        const linked = (output('otool', ['-L', program]) || '').split('\n').slice(1)
            .map((line) => line.trim().split(' ')[0]).filter(Boolean);
        const foreign = linked.filter((lib) => !lib.startsWith('/usr/lib/') && !lib.startsWith('/System/'));
        if (foreign.length > 0) fail(`${path.relative(ROOT, program)} links ${foreign.join(', ')}`);
    }

    const arch = os.arch() === 'arm64' ? 'arm64' : 'x86_64';
    const name = `florr-mac-${arch}`;
    const packageDir = path.join(buildDir, 'package', name);
    stage(packageDir, [[client, 'florr'], [app, appName]], path.join(buildDir, 'data'), [
        'Neither is signed by a registered Apple developer, so the first time you',
        'open each one macOS refuses. Then open System Settings > Privacy &',
        'Security, and click Open Anyway beside its name.',
        '',
    ]);
    const zip = path.join(buildDir, `${name}.zip`);
    fs.rmSync(zip, { force: true });
    run('ditto', ['-c', '-k', '--keepParent', packageDir, zip]);
    const standalone = path.join(buildDir, `florr-offline-mac-${arch}.zip`);
    fs.rmSync(standalone, { force: true });
    run('ditto', ['-c', '-k', '--keepParent', app, standalone]);
    console.log(`\nbuild-native: ${path.relative(ROOT, zip)}`);
    console.log(`build-native: ${path.relative(ROOT, app)}`);
    console.log(`build-native: ${path.relative(ROOT, standalone)}`);
}

const which = process.argv[2] || 'windows';
if (!['windows', 'mac', 'all'].includes(which)) fail(`unknown platform '${which}': windows, mac or all`);
if (which === 'windows' || which === 'all') windows();
if (which === 'mac' || which === 'all') mac();
