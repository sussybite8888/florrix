#pragma once
// Game files compiled into the program. The native offline build carries its
// whole content directory inside the executable (cmake/embed_content.cmake),
// so that one file runs from anywhere with nothing beside it.
//
// The files are mounted at a directory path, and from then on answer for that
// directory's files wherever the game reads one: readFile (shared/core/file.h),
// which every content loader, map and tileset goes through, and -- through the
// reader the program hands cpp_canvas (file_source.h) -- the SVG and font
// loaders. A file the embedded set does not have still comes off the disk.

#include <cstddef>
#include <string>

namespace flix {

struct EmbeddedFile {
    /// Its bare name: the content directory is staged flat.
    const char* name;
    const unsigned char* data;
    std::size_t size;
};

/// Serves `files` as the contents of directory `root` from now on. Called once,
/// before anything reads; the files must outlive every read.
void mountEmbeddedFiles(const std::string& root, const EmbeddedFile* files, std::size_t count);

/// The embedded copy of `path` into `out`, byte for byte. False, with `out`
/// untouched, when no mounted directory holds it.
bool readEmbeddedFile(const std::string& path, std::string& out);

} // namespace flix
