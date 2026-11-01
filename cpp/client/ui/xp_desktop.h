#pragma once
// The corrupted desktop: what a second glitch crash restarts into.
//
// The first glitch trap really crashes the client and brands the install
// florr.exe. The next one plays the same blue screen, but the "restart" it
// promises lands here -- a Windows XP desktop the player is stuck in. A tray
// balloon sets the quest: every corruption.exe on this computer has to be
// found and deleted, and once the last one is gone the game exits, the ordinary
// way, back to the player's own computer (App::leaveXpDesktop).
//
// It is drawn the way XP's Luna theme is drawn -- the taskbar and its green
// Start button, the rounded title bars, Explorer's task pane, the Start menu,
// the tray balloon, the message boxes -- out of canvas primitives, with the
// theme's blue swapped for the player's yellow. The wallpaper is hel's ground
// under the field of dead flowers lv2 lays over it, which the App paints (see
// XpPainters). Text is handed to the canvas raw, setFont + fillText, as the
// blue screen's is: a page sets it in the player's own Tahoma, and the desktop
// build falls back to its one bundled face.
//
// The file system is a model of its own (XpFileSystem) so the hunt's rules --
// how many there are, where, and that nothing else can be deleted -- can be
// tested without a window.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "canvas.h"
#include "window.h"

#include "client/web/audio.h"
#include "shared/core/types.h"

namespace flix::ui {

/// The picture an item is drawn with, in the desktop, Explorer and the menus.
enum class XpIcon : std::uint8_t {
    Folder,
    Documents,
    Pictures,
    Music,
    Computer,
    Drive,
    Floppy,
    Cd,
    RecycleBin,
    Globe,
    Application,
    Florr,
    Corruption,
    Text,
    Picture,
    System,
};

/// What opening an item does.
enum class XpOpen : std::uint8_t {
    Browse,    ///< a folder, drive or shell folder: shown in Explorer
    Notepad,   ///< a text document: its `text` in Notepad
    Picture,   ///< an image: the picture viewer
    Crash,     ///< corruption.exe: the application error box
    Message,   ///< anything else: a message box, `caption` over `text`
};

struct XpNode {
    std::string name;
    XpIcon icon = XpIcon::Folder;
    XpOpen open = XpOpen::Browse;
    /// The Type column: "File Folder", "Application", "Text Document"...
    std::string type;
    /// The second line under a file in Tiles view; empty for folders.
    std::string size;
    /// Notepad's contents, or the message box's words.
    std::string text;
    std::string caption;
    /// What the address bar shows for a shell folder ("My Computer"); empty
    /// for an ordinary one, whose address is built from its parents.
    std::string address;
    /// An item that stands for another one -- the desktop's My Documents is the
    /// folder under Documents and Settings -- opens that one. -1 for none.
    int target = -1;
    bool corruption = false;
    bool deleted = false;
    int parent = -1;
    std::vector<int> children;
};

class XpFileSystem {
public:
    /// The shipped computer: five corruption.exe files, one on the desktop,
    /// one beside the game in Program Files, one among the system files, one
    /// in My Pictures and one already in the Recycle Bin. `user` names the
    /// folder under Documents and Settings.
    static XpFileSystem standard(const std::string& user);

    const XpNode& node(int id) const { return nodes_[static_cast<std::size_t>(id)]; }
    int size() const { return static_cast<int>(nodes_.size()); }

    int desktop() const { return desktop_; }
    int myComputer() const { return myComputer_; }
    int myDocuments() const { return myDocuments_; }
    int myPictures() const { return myPictures_; }
    int myMusic() const { return myMusic_; }
    int recycleBin() const { return recycleBin_; }

    /// What `id` opens as: itself, or the item it stands for.
    int resolve(int id) const;
    /// A folder's items as Explorer lists them: folders first, then files, each
    /// by name, deleted ones left out.
    std::vector<int> listing(int folder) const;
    /// What the address bar shows while `folder` is open.
    std::string addressOf(int folder) const;
    /// The item named by `path`, '/'-separated from the desktop and following
    /// links ("My Computer/Local Disk (C:)/WINDOWS"); -1 if there is none.
    int find(const std::string& path) const;

    /// Only a corruption.exe can be deleted; everything else is "Access is
    /// denied", as a file in use is.
    bool canDelete(int id) const;
    /// Deletes `id` if it can be; true when it was.
    bool remove(int id);
    /// Deletes everything in the Recycle Bin, which is the one way anything
    /// but a corruption.exe goes.
    void emptyRecycleBin();

    int corruptionTotal() const;
    int corruptionLeft() const;

private:
    int add(int parent, XpNode node);
    int folder(int parent, const std::string& name, XpIcon icon = XpIcon::Folder);

    std::vector<XpNode> nodes_;
    int desktop_ = -1;
    int myComputer_ = -1;
    int myDocuments_ = -1;
    int myPictures_ = -1;
    int myMusic_ = -1;
    int recycleBin_ = -1;
};

/// What the App paints for the desktop: the parts that are the game's own art.
struct XpPainters {
    /// Fills a `w` by `h` screen with the wallpaper.
    std::function<void(Canvas&, double w, double h)> wallpaper;
    /// A flower centred on (x, y), `radius` to its rim, with dead eyes when
    /// `dead`: the Start button, the user picture and the picture viewer.
    std::function<void(Canvas&, double x, double y, double radius, bool dead)> flower;
};

/// One frame of the player's input, in the window's own units: what
/// XpDesktop::update reads off the Window, and what a test hands it directly.
struct XpInput {
    Vec2 mouse;
    bool leftPressed = false;
    bool leftReleased = false;
    bool leftDown = false;
    bool rightPressed = false;
    float wheel = 0.0f;
    bool deleteKey = false;
    bool enter = false;
    bool backspace = false;
    bool escape = false;
};

class XpDesktop {
public:
    /// Boots the desktop: a fresh computer with every corruption.exe on it,
    /// a moment of black screen, then the desktop and the quest's balloon.
    /// `user` is who the Start menu greets and whose documents these are.
    void open(double now, const std::string& user);
    void close();
    bool active() const { return active_; }

    /// True once every corruption.exe is gone and the goodbye balloon has had
    /// its moment: the App exits the game on it.
    bool finished(double now) const;

    /// One frame of the window's input, and the hand over a link.
    void update(Window&, double now);
    /// The same from `in`, on a `width` by `height` screen in the window's
    /// units.
    void update(const XpInput& in, double width, double height, double now);
    void draw(Canvas&, double now, const XpPainters&);

    /// What the desktop has made a noise about since this was last called,
    /// oldest first -- its startup, each box and balloon as it comes up, every
    /// folder opened, everything thrown away -- for the App to play
    /// (web::playSound). Taking them empties the list.
    std::vector<web::Sound> takeSounds();

    const XpFileSystem& files() const { return fs_; }
    /// The front window's title -- a message box's caption -- or "" with none.
    std::string frontTitle() const;
    /// Where `node` is on screen, in the window's units: its tile in the
    /// front-most Explorer listing it, or its desktop icon; (-1, -1) if it is
    /// in neither.
    Vec2 itemCentre(int node) const;

private:
    enum class Kind : std::uint8_t { Explorer, Notepad, Picture, Message };
    /// The picture in a message box; Bin is the Recycle Bin over a delete.
    enum class Glyph : std::uint8_t { Error, Warning, Question, Info, Bin };

    struct Win {
        int id = 0;
        Kind kind = Kind::Explorer;
        Rect frame;
        bool maximized = false;
        bool minimized = false;
        // Explorer
        int folder = -1;
        std::vector<int> back;
        std::vector<int> forward;
        int selected = -1;
        double scroll = 0.0;
        // Notepad and the picture viewer: the file shown. -1 is Untitled.
        int file = -1;
        // Message boxes
        std::string caption;
        std::string message;
        Glyph glyph = Glyph::Error;
        bool yesNo = false;
        /// What Yes does: delete this item, or empty the Recycle Bin when it is
        /// the bin itself. -1 for a box with nothing to confirm.
        int confirm = -1;
        /// Whether the box has been sized to its words, which only a canvas
        /// can measure: the first draw does it (layoutMessage).
        bool laidOut = false;
    };

    /// Something that takes a press and acts only if it is let go over it.
    struct Press {
        enum class What : std::uint8_t {
            None, Close, Maximize, Minimize, DialogButton, StartItem, MenuItem,
            TaskButton, ToolBack, ToolForward, ToolUp, PaneLink, BalloonClose,
        };
        What what = What::None;
        int window = -1;
        int index = -1;
    };

    struct Balloon {
        std::string title;
        std::string body;
        double shownAt = -1.0;
        /// Seconds it stays up by itself; <= 0 until it is closed.
        double lasts = 0.0;
        bool visible = false;
    };

    struct ContextMenu {
        bool open = false;
        Vec2 at;
        int window = -1;   ///< -1 for the desktop
        int item = -1;
    };

    struct DeskSlot {
        int node;
        int col;   ///< negative counts from the right
        int row;   ///< negative counts up from the taskbar
    };

    // -- layout ---------------------------------------------------------------
    Rect taskbar() const;
    Rect startButton() const;
    Rect tray() const;
    Rect trayIcon() const;
    /// The windows with a taskbar button, in the order they were opened, and
    /// their buttons left to right.
    std::vector<int> taskWindows() const;
    std::vector<Rect> taskButtons() const;
    Rect startMenu() const;
    Rect windowRect(const Win&) const;
    Rect deskCell(int col, int row) const;
    std::vector<DeskSlot> deskSlots() const;
    Rect contextMenuRect() const;
    Rect nextFrame(double w, double h);
    Rect pressRect(const Press&);

    // -- behaviour ------------------------------------------------------------
    Win* find(int id);
    /// The front message box, which nothing behind answers while it is up.
    Win* modal();
    /// The window whose title bar is lit: 0 when the desktop has the keyboard.
    int activeId() const;
    void focus(int id);
    void closeWindow(int id);
    void openItem(int node, Win* from);
    void openExplorer(int folder);
    void openViewer(Kind kind, int file);
    /// A message box, sounding as its glyph says: an error, a notice, or a
    /// question.
    void message(const std::string& caption, const std::string& text, Glyph glyph,
                 bool yesNo = false, int confirm = -1);
    /// corruption.exe's application error, which is an error box with a
    /// sound of its own.
    void crashMessage(const std::string& name);
    void restrictionMessage();
    void askDelete(int node);
    void askEmptyRecycleBin();
    /// A message box's button `button` was chosen.
    void answer(int boxId, int button);
    /// Tidies up after a delete and reports how the hunt stands, when it
    /// took a corruption.exe with it.
    void afterDelete(int corruptionBefore);
    void navigate(Win& w, int folder, bool record = true);
    void showBalloon(const std::string& title, const std::string& body, double lasts,
                     web::Sound sound = web::Sound::Balloon);
    void cue(web::Sound sound) { sounds_.push_back(sound); }
    void showQuestBalloon();
    void startItem(int action);
    bool doubleClick(int window, int item, double now);
    std::string titleOf(const Win&) const;
    XpIcon iconOf(const Win&) const;

    void pressLeft(Vec2 mouse, double now);
    void windowPress(Win& w, Vec2 mouse, double now);
    void pressRight(Vec2 mouse);
    void releaseLeft(Vec2 mouse);
    void keys(const XpInput&);

    // -- drawing --------------------------------------------------------------
    void layoutMessage(Canvas&, Win&);
    void drawDesktopIcons(Canvas&);
    void drawWindow(Canvas&, Win&, bool active, double now, const XpPainters&);
    void drawExplorer(Canvas&, const Win&, Rect client, bool active);
    void drawNotepad(Canvas&, const Win&, Rect client);
    void drawPicture(Canvas&, const Win&, Rect client, double now, const XpPainters&);
    void drawMessage(Canvas&, const Win&, Rect client);
    void drawTaskbar(Canvas&, const XpPainters&);
    void drawStartMenu(Canvas&, const XpPainters&);
    void drawBalloon(Canvas&);
    void drawContextMenu(Canvas&);

    bool active_ = false;
    double openedAt_ = 0.0;
    double now_ = 0.0;
    /// When the last corruption.exe went; -1 while the hunt is on.
    double doneAt_ = -1.0;
    std::string user_;
    XpFileSystem fs_;
    std::vector<Win> windows_;   ///< back to front
    int nextId_ = 1;
    int deskSelected_ = -1;
    /// Who the keyboard talks to: a window's id, or 0 for the desktop.
    int keyTarget_ = 0;
    bool startOpen_ = false;
    Balloon balloon_;
    /// Where the balloon was last drawn, for the next frame's clicks.
    Rect balloonBox_;
    bool questShown_ = false;
    /// Whether the startup sound has played: once, as the desktop fades in.
    bool startupSounded_ = false;
    /// What takeSounds() hands over next.
    std::vector<web::Sound> sounds_;
    ContextMenu menu_;
    Press press_;
    int dragging_ = -1;
    Vec2 dragFrom_;
    Rect dragFrame_;
    double lastClickAt_ = -10.0;
    int lastClickWindow_ = -2;
    int lastClickItem_ = -2;
    /// Window units per desktop pixel (see kScreenHeight).
    double scale_ = 1.0;
    /// Whether the pointer is over a link, for the Window to show a hand.
    bool hand_ = false;
    /// The pointer and the screen, in the desktop's own 768-tall pixels.
    Vec2 mouse_;
    double width_ = 1365.0;
    double height_ = 768.0;
};

} // namespace flix::ui
