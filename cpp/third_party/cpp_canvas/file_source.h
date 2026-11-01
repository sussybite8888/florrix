#pragma once
// Where cpp_canvas reads a file it is asked for by path: an SVG document
// (SvgDocument::fromFile), a font (Font::loadFromFile). From the disk, unless
// the program has put a reader in front of it -- one with files compiled into
// the executable answers for those first, so a program that carries its art
// inside itself needs no directory of it beside it.

#include <string>

/// Answers `path` into `out`, or returns false to leave it to the disk.
using FileSourceReader = bool (*)(const std::string& path, std::string& out);

/// Installs `reader` in front of the disk, for every read from now on. Null
/// takes it away.
void setFileSourceReader(FileSourceReader reader);

/// All of `path`, byte for byte: the reader's answer, else the file on disk.
/// False when neither has it.
bool readFileSource(const std::string& path, std::string& out);
