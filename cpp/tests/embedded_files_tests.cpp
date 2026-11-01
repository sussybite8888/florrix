#include "test.h"

#include "file_source.h"
#include "shared/core/embedded_files.h"
#include "shared/core/file.h"

#include <string>

using flix::EmbeddedFile;

// Files compiled into the program, as the native offline build carries its
// content (shared/core/embedded_files.h). Mounts are for the whole process, so
// each test mounts at a root of its own that no other test reads under.

namespace {

const unsigned char kJson[] = {'{', '"', 'a', '"', ':', '1', '}'};
const unsigned char kArt[] = {'<', 's', 'v', 'g', '/', '>'};
const unsigned char kEmpty[] = {0};
const EmbeddedFile kFiles[] = {
    {"mobs.json", kJson, sizeof kJson},
    {"grass_c_0.svg", kArt, sizeof kArt},
    {"empty.txt", kEmpty, 0},
};

} // namespace

TEST(an_embedded_file_answers_for_its_directory_through_readFile) {
    flix::mountEmbeddedFiles("<embedded-test-a>", kFiles, 3);
    std::string out;
    CHECK(flix::readFile("<embedded-test-a>/mobs.json", out));
    CHECK_EQ(out, std::string("{\"a\":1}"));
    // Byte for byte, an empty file included.
    CHECK(flix::readFile("<embedded-test-a>/empty.txt", out));
    CHECK(out.empty());
}

TEST(an_embedded_file_is_found_by_its_bare_name_under_either_separator) {
    flix::mountEmbeddedFiles("<embedded-test-b>/", kFiles, 3);
    std::string out;
    // A tileset names its art under tiles/, and the content was staged flat.
    CHECK(flix::readEmbeddedFile("<embedded-test-b>/tiles/grass_c_0.svg", out));
    CHECK_EQ(out, std::string("<svg/>"));
    CHECK(flix::readEmbeddedFile("<embedded-test-b>\\mobs.json", out));
    CHECK_EQ(out, std::string("{\"a\":1}"));
}

TEST(an_embedded_mount_leaves_every_other_path_to_the_disk) {
    flix::mountEmbeddedFiles("<embedded-test-c>", kFiles, 3);
    std::string out = "untouched";
    // Not in the set, outside the root, or a root that only shares a prefix.
    CHECK(!flix::readEmbeddedFile("<embedded-test-c>/petals.json", out));
    CHECK(!flix::readEmbeddedFile("elsewhere/mobs.json", out));
    CHECK(!flix::readEmbeddedFile("<embedded-test-c>-other/mobs.json", out));
    CHECK(!flix::readEmbeddedFile("<embedded-test-c>", out));
    CHECK_EQ(out, std::string("untouched"));
    // And the disk still answers through readFile for what it holds.
    CHECK(flix::readFile(std::string(FLIX_TEST_DATA_DIR) + "/mobs.json", out));
    CHECK(!out.empty());
}

TEST(the_canvas_reads_through_the_reader_it_is_handed) {
    flix::mountEmbeddedFiles("<embedded-test-d>", kFiles, 3);
    std::string out;
    CHECK(!readFileSource("<embedded-test-d>/grass_c_0.svg", out));
    setFileSourceReader(&flix::readEmbeddedFile);
    CHECK(readFileSource("<embedded-test-d>/grass_c_0.svg", out));
    CHECK_EQ(out, std::string("<svg/>"));
    setFileSourceReader(nullptr);
}
