#include "test.h"

#include "client/ui/xp_desktop.h"

#include <algorithm>
#include <string>
#include <vector>

using flix::Vec2;
using flix::ui::XpDesktop;
using flix::ui::XpFileSystem;
using flix::ui::XpInput;
using flix::ui::XpOpen;
using flix::web::Sound;

// The corrupted desktop a second glitch crash restarts into.
//
// Only the hunt's rules are exercised here -- where the corruption.exe files
// are, what can be deleted, and when the hunt is over. The desktop's look is a
// question for its screenshot, and its input needs a window.

namespace {

/// Every corruption.exe, by where the READ ME says it is: where you can see
/// it, where the game lives, among the system files, with the pictures, and
/// already thrown away.
const std::vector<std::string> kHidingPlaces = {
    "corruption.exe",
    "My Computer/Local Disk (C:)/Program Files/florr/corruption.exe",
    "My Computer/Local Disk (C:)/WINDOWS/system32/corruption.exe",
    "My Documents/My Pictures/corruption.exe",
    "Recycle Bin/corruption.exe",
};

/// A player at the desktop: frames of input at sixty a second on the game's
/// 1920x1080 design screen.
struct Player {
    XpDesktop& desktop;
    double now = 0.0;
    Vec2 mouse;

    void frame(XpInput in) {
        now += 1.0 / 60.0;
        desktop.update(in, 1920, 1080, now);
    }
    void idle(double seconds) {
        const double until = now + seconds;
        while (now < until) frame(at());
    }
    XpInput at() const {
        XpInput in;
        in.mouse = mouse;
        return in;
    }
    void click(Vec2 where) {
        mouse = where;
        XpInput down = at();
        down.leftPressed = true;
        down.leftDown = true;
        frame(down);
        XpInput up = at();
        up.leftReleased = true;
        frame(up);
    }
    void doubleClick(Vec2 where) {
        click(where);
        click(where);
    }
    void key(bool XpInput::*which) {
        XpInput in = at();
        in.*which = true;
        frame(in);
    }
    /// Selects `node` where it shows, presses Delete, and says Yes.
    void deleteItem(int node) {
        click(desktop.itemCentre(node));
        key(&XpInput::deleteKey);
        key(&XpInput::enter);
    }
};

} // namespace

TEST(the_corrupted_desktop_hides_five_corruption_files_where_the_read_me_says) {
    const XpFileSystem fs = XpFileSystem::standard("tester");
    CHECK_EQ(fs.corruptionTotal(), 5);
    CHECK_EQ(fs.corruptionLeft(), 5);
    for (const std::string& place : kHidingPlaces) {
        const int id = fs.find(place);
        CHECK(id >= 0);
        if (id < 0) continue;
        CHECK(fs.node(id).corruption);
        CHECK(fs.node(id).open == XpOpen::Crash);
    }
    // The READ ME that gives them away is on the desktop, and opens in
    // Notepad.
    const int readMe = fs.find("READ ME.txt");
    CHECK(readMe >= 0);
    if (readMe >= 0) CHECK(fs.node(readMe).open == XpOpen::Notepad);
}

TEST(only_a_corruption_file_can_be_deleted) {
    XpFileSystem fs = XpFileSystem::standard("tester");
    // Everything else is "Access is denied": the shell folders, the
    // documents, the system files.
    for (const char* path : {"My Computer", "My Documents", "Recycle Bin", "READ ME.txt",
                             "My Computer/Local Disk (C:)/WINDOWS/system32/kernel32.dll",
                             "My Computer/Local Disk (C:)/Program Files/florr/florr.exe"}) {
        const int id = fs.find(path);
        CHECK(id >= 0);
        CHECK(!fs.canDelete(id));
        CHECK(!fs.remove(id));
    }
    CHECK_EQ(fs.corruptionLeft(), 5);

    // Each corruption.exe goes once, and is gone from its folder after.
    int left = 5;
    for (const std::string& place : kHidingPlaces) {
        const int id = fs.find(place);
        CHECK(fs.remove(id));
        CHECK(!fs.remove(id));
        CHECK_EQ(fs.corruptionLeft(), --left);
        CHECK_EQ(fs.find(place), -1);
    }
    CHECK_EQ(fs.corruptionLeft(), 0);
}

TEST(emptying_the_recycle_bin_takes_the_one_already_thrown_away) {
    XpFileSystem fs = XpFileSystem::standard("tester");
    CHECK(!fs.listing(fs.recycleBin()).empty());
    fs.emptyRecycleBin();
    CHECK(fs.listing(fs.recycleBin()).empty());
    CHECK_EQ(fs.corruptionLeft(), 4);
    CHECK_EQ(fs.find("Recycle Bin/corruption.exe"), -1);
}

TEST(the_corrupted_desktops_folders_read_like_xps) {
    const XpFileSystem fs = XpFileSystem::standard("tester");
    // Shell folders by their names; everything else by its path, including
    // what sits under a shell folder.
    CHECK_EQ(fs.addressOf(fs.myComputer()), std::string("My Computer"));
    CHECK_EQ(fs.addressOf(fs.myDocuments()), std::string("My Documents"));
    CHECK_EQ(fs.addressOf(fs.find("My Computer/Local Disk (C:)")), std::string("C:\\"));
    CHECK_EQ(fs.addressOf(fs.find("My Computer/Local Disk (C:)/WINDOWS/system32")),
             std::string("C:\\WINDOWS\\system32"));
    CHECK_EQ(fs.addressOf(fs.myPictures()),
             std::string("C:\\Documents and Settings\\tester\\My Documents\\My Pictures"));
    // The desktop's My Documents is the folder under Documents and Settings.
    CHECK_EQ(fs.resolve(fs.find("My Documents")), fs.myDocuments());

    // Folders first, then files, each by name.
    const std::vector<int> c = fs.listing(fs.find("My Computer/Local Disk (C:)"));
    CHECK(c.size() >= 5);
    bool filesStarted = false;
    for (const int id : c) {
        const bool isFolder = fs.node(id).open == XpOpen::Browse;
        filesStarted |= !isFolder;
        CHECK(!(isFolder && filesStarted));
    }
}

TEST(the_corrupted_desktop_opens_on_a_fresh_computer_and_closes_clean) {
    XpDesktop desktop;
    CHECK(!desktop.active());
    desktop.open(10.0, "");
    CHECK(desktop.active());
    CHECK_EQ(desktop.files().corruptionLeft(), 5);
    // No user: XP's own name for a computer nobody set up.
    CHECK(desktop.files().find("My Computer/Local Disk (C:)/Documents and Settings/Owner") >= 0);
    CHECK(!desktop.finished(1000.0));
    desktop.close();
    CHECK(!desktop.active());
    CHECK(!desktop.finished(1000.0));
}

TEST(the_hunt_plays_through_to_the_end_with_the_mouse_and_keyboard) {
    XpDesktop desktop;
    desktop.open(0.0, "tester");
    Player player{desktop};
    // Past the restart's black screen; the quest's balloon is up by now.
    player.idle(3.0);
    const XpFileSystem& fs = desktop.files();

    // Opening a corruption.exe crashes it rather than getting rid of it.
    const int onDesktop = fs.find("corruption.exe");
    player.doubleClick(desktop.itemCentre(onDesktop));
    CHECK_EQ(desktop.frontTitle(), std::string("corruption.exe - Application Error"));
    player.key(&XpInput::escape);
    CHECK_EQ(desktop.frontTitle(), std::string());
    CHECK_EQ(fs.corruptionLeft(), 5);

    // Delete asks first, and Yes deletes it.
    player.click(desktop.itemCentre(onDesktop));
    player.key(&XpInput::deleteKey);
    CHECK_EQ(desktop.frontTitle(), std::string("Confirm File Delete"));
    player.key(&XpInput::enter);
    CHECK_EQ(fs.corruptionLeft(), 4);
    CHECK_EQ(desktop.itemCentre(onDesktop).x, -1.0);

    // Nothing else goes.
    player.click(desktop.itemCentre(fs.find("READ ME.txt")));
    player.key(&XpInput::deleteKey);
    CHECK_EQ(desktop.frontTitle(), std::string("Error Deleting File or Folder"));
    player.key(&XpInput::enter);
    CHECK_EQ(fs.corruptionLeft(), 4);

    // My Computer, and down through one Explorer to the system files.
    player.doubleClick(desktop.itemCentre(fs.myComputer()));
    CHECK_EQ(desktop.frontTitle(), std::string("My Computer"));
    for (const char* folder : {"My Computer/Local Disk (C:)", "My Computer/Local Disk (C:)/WINDOWS",
                               "My Computer/Local Disk (C:)/WINDOWS/system32"}) {
        player.doubleClick(desktop.itemCentre(fs.find(folder)));
    }
    CHECK_EQ(desktop.frontTitle(), std::string("system32"));
    player.deleteItem(fs.find("My Computer/Local Disk (C:)/WINDOWS/system32/corruption.exe"));
    CHECK_EQ(fs.corruptionLeft(), 3);

    // Backspace with nothing selected is Back: up to C:, and into the game's
    // own folder.
    player.key(&XpInput::backspace);
    player.key(&XpInput::backspace);
    CHECK_EQ(desktop.frontTitle(), std::string("Local Disk (C:)"));
    player.doubleClick(desktop.itemCentre(fs.find("My Computer/Local Disk (C:)/Program Files")));
    player.doubleClick(desktop.itemCentre(fs.find("My Computer/Local Disk (C:)/Program Files/florr")));
    CHECK_EQ(desktop.frontTitle(), std::string("florr"));
    player.deleteItem(fs.find("My Computer/Local Disk (C:)/Program Files/florr/corruption.exe"));
    CHECK_EQ(fs.corruptionLeft(), 2);

    // The Recycle Bin, opened from the desktop in a window of its own.
    player.doubleClick(desktop.itemCentre(fs.recycleBin()));
    CHECK_EQ(desktop.frontTitle(), std::string("Recycle Bin"));
    player.deleteItem(fs.find("Recycle Bin/corruption.exe"));
    CHECK_EQ(fs.corruptionLeft(), 1);

    // And the pictures.
    player.doubleClick(desktop.itemCentre(fs.find("My Documents")));
    CHECK_EQ(desktop.frontTitle(), std::string("My Documents"));
    player.doubleClick(desktop.itemCentre(fs.myPictures()));
    CHECK_EQ(desktop.frontTitle(), std::string("My Pictures"));
    player.deleteItem(fs.find("My Documents/My Pictures/corruption.exe"));
    CHECK_EQ(fs.corruptionLeft(), 0);

    // The goodbye balloon has its moment, and then the desktop is done.
    CHECK(!desktop.finished(player.now));
    player.idle(4.5);
    CHECK(desktop.finished(player.now));
}

TEST(the_hunt_is_heard_from_the_startup_to_the_last_corruption_file) {
    // The desktop says what it makes a noise about and the App plays it
    // (web::playSound), so which sound, and when, is the desktop's to get right.
    XpDesktop desktop;
    desktop.open(0.0, "tester");
    Player player{desktop};
    const XpFileSystem& fs = desktop.files();
    const auto took = [&](std::vector<Sound> expected) { return desktop.takeSounds() == expected; };
    const auto heard = [&](Sound sound) {
        const std::vector<Sound> taken = desktop.takeSounds();
        return std::find(taken.begin(), taken.end(), sound) != taken.end();
    };

    // Silent through the restart's black screen, then the startup as the
    // desktop fades in, and the quest's balloon popping up after it.
    player.idle(0.5);
    CHECK(took({}));
    player.idle(2.5);
    CHECK(took({Sound::DesktopStartup, Sound::Balloon}));
    player.idle(1.0);
    CHECK(took({}));

    // corruption.exe's crash box is not the ordinary error.
    const int onDesktop = fs.find("corruption.exe");
    player.doubleClick(desktop.itemCentre(onDesktop));
    CHECK(took({Sound::CorruptionError}));
    player.key(&XpInput::escape);
    CHECK(took({}));

    // Asked first; then thrown away, dying, and the balloon says how many are
    // left.
    player.click(desktop.itemCentre(onDesktop));
    player.key(&XpInput::deleteKey);
    CHECK(took({Sound::Question}));
    player.key(&XpInput::enter);
    CHECK(took({Sound::Purge, Sound::Balloon}));

    // What cannot go is an error.
    player.click(desktop.itemCentre(fs.find("READ ME.txt")));
    player.key(&XpInput::deleteKey);
    CHECK(took({Sound::Error}));
    player.key(&XpInput::enter);

    // Every folder opened clicks, whether in a new window or the same one.
    player.doubleClick(desktop.itemCentre(fs.myComputer()));
    CHECK(took({Sound::Navigate}));
    for (const char* folder : {"My Computer/Local Disk (C:)", "My Computer/Local Disk (C:)/WINDOWS",
                               "My Computer/Local Disk (C:)/WINDOWS/system32"}) {
        player.doubleClick(desktop.itemCentre(fs.find(folder)));
        CHECK(took({Sound::Navigate}));
    }
    player.deleteItem(fs.find("My Computer/Local Disk (C:)/WINDOWS/system32/corruption.exe"));
    CHECK(took({Sound::Question, Sound::Purge, Sound::Balloon}));

    player.key(&XpInput::backspace);
    player.key(&XpInput::backspace);
    player.doubleClick(desktop.itemCentre(fs.find("My Computer/Local Disk (C:)/Program Files")));
    player.doubleClick(desktop.itemCentre(fs.find("My Computer/Local Disk (C:)/Program Files/florr")));
    player.deleteItem(fs.find("My Computer/Local Disk (C:)/Program Files/florr/corruption.exe"));
    CHECK(heard(Sound::Purge));
    player.doubleClick(desktop.itemCentre(fs.recycleBin()));
    player.deleteItem(fs.find("Recycle Bin/corruption.exe"));
    CHECK(heard(Sound::Purge));
    player.doubleClick(desktop.itemCentre(fs.find("My Documents")));
    player.doubleClick(desktop.itemCentre(fs.myPictures()));
    CHECK(took({Sound::Navigate, Sound::Navigate}));
    CHECK_EQ(fs.corruptionLeft(), 1);

    // The last one: thrown away, and the all-clear in place of the balloon's
    // pop.
    player.deleteItem(fs.find("My Documents/My Pictures/corruption.exe"));
    CHECK_EQ(fs.corruptionLeft(), 0);
    CHECK(took({Sound::Question, Sound::Purge, Sound::AllClear}));
}
