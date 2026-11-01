#include "shared/core/embedded_files.h"

#include <unordered_map>
#include <vector>

namespace flix {
namespace {

struct Mount {
    std::string root;   ///< forward slashes, no trailing one
    std::unordered_map<std::string, const EmbeddedFile*> byName;
};

std::vector<Mount>& mounts() {
    static std::vector<Mount> list;
    return list;
}

/// Either separator, as a path built on Windows may carry both.
std::string normalised(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

} // namespace

void mountEmbeddedFiles(const std::string& root, const EmbeddedFile* files, std::size_t count) {
    Mount mount;
    mount.root = normalised(root);
    for (std::size_t i = 0; i < count; ++i) mount.byName[files[i].name] = &files[i];
    mounts().push_back(std::move(mount));
}

bool readEmbeddedFile(const std::string& path, std::string& out) {
    if (mounts().empty()) return false;
    const std::string wanted = normalised(path);
    for (const Mount& mount : mounts()) {
        if (wanted.size() <= mount.root.size() + 1 ||
            wanted.compare(0, mount.root.size(), mount.root) != 0 ||
            wanted[mount.root.size()] != '/') {
            continue;
        }
        // By the bare name, whatever directories the reader put in between:
        // a tileset names its art under `tiles/`, and it was staged flat.
        const std::size_t slash = wanted.find_last_of('/');
        const auto found = mount.byName.find(wanted.substr(slash + 1));
        if (found == mount.byName.end()) continue;
        out.assign(reinterpret_cast<const char*>(found->second->data), found->second->size);
        return true;
    }
    return false;
}

} // namespace flix
