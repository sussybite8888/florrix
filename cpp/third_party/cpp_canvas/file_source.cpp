#include "file_source.h"

#include <fstream>
#include <sstream>

namespace {
FileSourceReader g_reader = nullptr;
}

void setFileSourceReader(FileSourceReader reader) { g_reader = reader; }

bool readFileSource(const std::string& path, std::string& out) {
  if (g_reader && g_reader(path, out)) return true;
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  std::stringstream source;
  source << input.rdbuf();
  out = source.str();
  return true;
}
