// The native offline build: the whole game, server and client, in one desktop
// program, as the offline page is in one HTML file.
//
// What differs from that page is only what a desktop has and a tab does not:
//
//   * A real socket. The server listens on 127.0.0.1 alone, on whatever port
//     is free (so it never collides with a server already running here), and
//     the client dials it there. Nothing off this machine can connect, and
//     Windows has no firewall prompt to raise for a loopback listener.
//   * A real file system. The world, the session and the settings live in the
//     player's own per-user folder (SDL_GetPrefPath: %APPDATA%\florrix\offline
//     on Windows, ~/Library/Application Support/florrix/offline on a Mac), so
//     the program runs from wherever it is, a read-only folder included.
//   * Nothing beside it. The game's whole content -- the JSON, the maps, their
//     tilesets and art, the font -- is compiled into the executable
//     (cmake/embed_content.cmake) and read from there, so the one file is the
//     whole game and can be copied anywhere on its own.
//   * One loop, owned here. Server and client take turns on this thread, as
//     they do in the page's frame callback: the server's step waits at most a
//     few milliseconds for the network, the client's frame paces itself to the
//     display, and neither needs a lock against the other.

#include <SDL.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "client/app.h"
#include "file_source.h"
#include "server/game_server.h"
#include "shared/core/embedded_files.h"

namespace flix {
// The game's content, compiled in: embedded_content.cpp, which the build
// writes from the staged directory.
extern const EmbeddedFile kEmbeddedContent[];
extern const std::size_t kEmbeddedContentCount;
} // namespace flix

namespace {

/// Where the compiled-in content is mounted, and so the data directory every
/// reader is handed. No folder has this name -- Windows forbids the brackets
/// in one -- so every read under it is an embedded one.
constexpr const char* kEmbeddedRoot = "<embedded>";

/// The `/admin db` key, as the offline page fixes it: this world is the
/// player's own, and a key derived per install would only be a thing to lose.
constexpr const char* kAdminDbKey = "0ff1ce0ff1ce0ff1";

/// A path SDL handed out, with forward slashes, and freed. Windows takes either
/// separator, and the database finds its backup directory by its last '/'.
std::string fromSdl(char* path) {
    if (path == nullptr) return {};
    std::string out = path;
    SDL_free(path);
    for (char& c : out) {
        if (c == '\\') c = '/';
    }
    return out;
}

/// Says what went wrong where a person will see it. A Windows GUI program has
/// no console for stderr to reach.
void report(const std::string& message) {
    std::fprintf(stderr, "%s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "florr offline", message.c_str(), nullptr);
}

void usage(const char* program) {
    std::printf(
        "usage: %s [options]\n"
        "  --data <dir>       read the content from a staged directory instead of\n"
        "                     the copy compiled into the program\n"
        "  --state <dir>      where the world, session and settings live\n"
        "                     (default: the per-user folder SDL names)\n"
        "  --width <px>       window width (default 1280)\n"
        "  --height <px>      window height (default 720)\n"
        "  --user <name>      log in (or register) automatically\n"
        "  --password <pw>    password for --user\n"
        "  --lobby            log in but stay on the title screen\n"
        "  --frames <n>       render n frames then exit\n"
        "  --screenshot <f>   write the last frame to f as a PPM\n",
        program);
}

} // namespace

int main(int argc, char** argv) {
    flix::AppConfig clientConfig;
    std::string dataDir;
    std::string stateDir;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--data") dataDir = next("--data");
        else if (arg == "--state") stateDir = next("--state");
        else if (arg == "--width") clientConfig.windowWidth = std::atoi(next("--width"));
        else if (arg == "--height") clientConfig.windowHeight = std::atoi(next("--height"));
        else if (arg == "--user") clientConfig.autoUsername = next("--user");
        else if (arg == "--password") clientConfig.autoPassword = next("--password");
        else if (arg == "--lobby") clientConfig.autoJoin = false;
        else if (arg == "--frames") clientConfig.screenshotAfterFrames = std::atoi(next("--frames"));
        else if (arg == "--screenshot") clientConfig.screenshotPath = next("--screenshot");
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    // The content compiled in, mounted where the readers will look for it:
    // the game's own loaders through readFile, the SVG and font loaders
    // through the reader cpp_canvas is handed.
    if (dataDir.empty()) {
        flix::mountEmbeddedFiles(kEmbeddedRoot, flix::kEmbeddedContent,
                                 flix::kEmbeddedContentCount);
        setFileSourceReader(&flix::readEmbeddedFile);
        dataDir = kEmbeddedRoot;
    }
    if (stateDir.empty()) stateDir = fromSdl(SDL_GetPrefPath("florrix", "offline"));
    if (stateDir.empty()) {
        report("Could not find a folder to keep the world in.");
        return 1;
    }
    if (stateDir.back() != '/') stateDir += '/';

    flix::ServerConfig serverConfig;
    serverConfig.port = 0;
    serverConfig.loopbackOnly = true;
    serverConfig.dataDir = dataDir;
    serverConfig.databasePath = stateDir + "inventory.json";
    serverConfig.fixedAdminDbKey = kAdminDbKey;
    flix::GameServer server;
    std::string error;
    if (!server.start(serverConfig, error)) {
        report("Could not start the game: " + error);
        return 1;
    }

    clientConfig.host = "127.0.0.1";
    clientConfig.port = server.boundPort();
    clientConfig.dataDir = dataDir;
    clientConfig.sessionFile = stateDir + "session";
    // What the offline page can do that a client of a real server cannot: hand
    // its own player the admin console. The server is in this process and the
    // world is nobody else's. The session token is resolved to the account by
    // the server itself; no message carries the grant.
    clientConfig.grantAdmin = [&server](const std::string& sessionToken) {
        return server.grantAdmin(sessionToken);
    };

    flix::App app;
    // A glitch trap's crash takes the whole process down, server and all, so
    // the world is written out first.
    app.setCrashHook([&server] { server.persistAll(); });
    if (!app.start(clientConfig, error)) {
        server.shutdown();
        report("Could not start the game: " + error);
        return 1;
    }

    while (true) {
        server.step();
        if (!app.step()) break;
    }
    app.shutdown();
    server.shutdown();
    return 0;
}
