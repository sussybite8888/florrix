#include "client/ui/xp_desktop.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "client/ui/draw.h"

namespace flix::ui {
namespace {

// ---------------------------------------------------------------------------
// The theme
// ---------------------------------------------------------------------------
//
// Luna's shapes in the player's colours: its blue bars become the flower's
// body yellow (#FFE763), their edges the rim's #CFBB50. What Luna never tinted
// -- the beige control face, the green Start button, the red close box, the
// pale yellow balloon -- stays the colour XP drew it.

/// One colour stop down a bar, `at` from 0 (top) to 1 (bottom).
struct Stop {
    double at;
    std::uint32_t rgb;
};
using Stops = std::vector<Stop>;

const Stops kTitleActive = {{0.00, 0xE6C53Cu}, {0.07, 0xFFF4A8u}, {0.20, 0xFBE36Au},
                            {0.50, 0xF2D24Cu}, {0.86, 0xE2BD32u}, {1.00, 0xCDA626u}};
const Stops kTitleInactive = {{0.00, 0xEDE2B4u}, {0.08, 0xFCF7DEu}, {0.40, 0xF3E9BEu},
                              {1.00, 0xE4D69Eu}};
const Stops kTaskbar = {{0.00, 0xFFF6BCu}, {0.10, 0xF8DE60u}, {0.45, 0xF1D04Au},
                        {0.88, 0xE3BC31u}, {1.00, 0xC79F22u}};
const Stops kTaskButton = {{0.00, 0xFFF3A8u}, {0.15, 0xFBE57Au}, {1.00, 0xF0CF4Cu}};
const Stops kTaskButtonDown = {{0.00, 0xC9A11Fu}, {0.20, 0xD6B12Eu}, {1.00, 0xE5C445u}};
const Stops kTray = {{0.00, 0xFFF8CCu}, {0.10, 0xFCE98Au}, {1.00, 0xEECE4Eu}};
const Stops kStart = {{0.00, 0x76C765u}, {0.12, 0x56B446u}, {0.50, 0x3C9C34u},
                      {0.88, 0x2F8A2Au}, {1.00, 0x287A24u}};
const Stops kStartDown = {{0.00, 0x2E7E29u}, {0.30, 0x358D2Fu}, {1.00, 0x45A23Bu}};
const Stops kPane = {{0.00, 0xF8E58Eu}, {1.00, 0xE6C858u}};
const Stops kPaneHeader = {{0.00, 0xFFFFFFu}, {1.00, 0xFBEDB5u}};
const Stops kButtonFace = {{0.00, 0xFFFFFFu}, {0.80, 0xF0EFEAu}, {1.00, 0xD8D2C6u}};
const Stops kButtonDown = {{0.00, 0xD8D2C6u}, {0.30, 0xE4E1D8u}, {1.00, 0xF0EFEAu}};
const Stops kScrollThumb = {{0.00, 0xFFF5C8u}, {1.00, 0xF2DE8Au}};
const Stops kCaptionButton = {{0.00, 0xFFF2A0u}, {0.50, 0xF5D758u}, {1.00, 0xE0B930u}};
const Stops kCloseButton = {{0.00, 0xF19A80u}, {0.40, 0xE0563Bu}, {1.00, 0xC2361Cu}};

constexpr std::uint32_t kWhite = 0xFFFFFFu;
constexpr std::uint32_t kBlack = 0x000000u;
/// XP's control face, which every theme left beige: menus, toolbars, boxes.
constexpr std::uint32_t kFace = 0xECE9D8u;
constexpr std::uint32_t kFaceLine = 0xD5D2C3u;
constexpr std::uint32_t kGreyText = 0x7F7F7Fu;
constexpr std::uint32_t kDisabled = 0xACA899u;
/// The shadow white text casts on the yellow bars.
constexpr std::uint32_t kInkOnYellow = 0x6B5410u;
/// XP's selection blue, in the theme's gold.
constexpr std::uint32_t kSelect = 0xB8921Cu;
constexpr std::uint32_t kFrameActive = 0xD9B935u;
constexpr std::uint32_t kFrameInactive = 0xE8DCA8u;
constexpr std::uint32_t kFrameEdge = 0x8C7413u;
constexpr std::uint32_t kFieldEdge = 0xC9B458u;
constexpr std::uint32_t kPaneTitle = 0x6B5410u;
constexpr std::uint32_t kPaneBody = 0xFFF8DCu;
constexpr std::uint32_t kPaneLink = 0x5C4A0Au;
constexpr std::uint32_t kStartRight = 0xFFF3C7u;
constexpr std::uint32_t kStartRightText = 0x3D3206u;
constexpr std::uint32_t kStartLine = 0xE6A526u;
constexpr std::uint32_t kBalloon = 0xFFFFE1u;
constexpr std::uint32_t kButtonEdge = 0x6B5A12u;

// Tahoma, the face XP set its whole interface in, at its 8 pt; Trebuchet MS
// for the title bars and the Start menu's name; the "start" in Franklin
// Gothic. A digit in a family name would be read as the size by the desktop
// build's font parser, and none has one.
const char* const kUi = "11px Tahoma, Verdana, sans-serif";
const char* const kUiBold = "bold 11px Tahoma, Verdana, sans-serif";
const char* const kTitleFont = "bold 13px \"Trebuchet MS\", Tahoma, sans-serif";
const char* const kStartFont =
    "italic bold 19px \"Franklin Gothic Medium\", \"Trebuchet MS\", Arial, sans-serif";
const char* const kUserFont = "bold 15px \"Trebuchet MS\", Tahoma, sans-serif";
const char* const kMono = "13px \"Lucida Console\", \"Courier New\", monospace";

// The quest, in the user's words.
const char* const kQuestTitle = "You have been sent to a place you never experienced";
const char* const kQuestBody =
    "To return to your normal computer you have to delete all corruption.exe files";

// ---------------------------------------------------------------------------
// The clock
// ---------------------------------------------------------------------------

/// The restart's black screen, its fade to the desktop, and when the quest's
/// balloon comes up out of the tray.
constexpr double kBlackSeconds = 0.9;
constexpr double kFadeSeconds = 0.5;
constexpr double kQuestDelaySeconds = 2.2;
/// How long the goodbye balloon is up before the client goes home.
constexpr double kGoodbyeSeconds = 4.0;
constexpr double kDoubleClickSeconds = 0.5;
/// A progress balloon's own timeout; the quest's stays until it is closed.
constexpr double kProgressBalloonSeconds = 8.0;

// ---------------------------------------------------------------------------
// Metrics, in XP's own pixels
// ---------------------------------------------------------------------------

/// The desktop is laid out on a screen this many pixels tall -- XP's own
/// 1024x768, as wide as the window's shape makes it -- and scaled to fill the
/// window. In the game's 1080-unit design space at one to one it would be XP
/// on a 1080p monitor, everything two-thirds the size it should be.
constexpr double kScreenHeight = 768.0;
constexpr double kTaskbarHeight = 30.0;
constexpr double kStartWidth = 100.0;
constexpr double kTitleHeight = 30.0;
constexpr double kBorder = 4.0;
constexpr double kCornerRadius = 8.0;
constexpr double kMenuBarHeight = 22.0;
constexpr double kToolbarHeight = 38.0;
constexpr double kAddressHeight = 26.0;
constexpr double kPaneWidth = 200.0;
constexpr double kTileWidth = 224.0;
constexpr double kTileHeight = 56.0;
constexpr double kScrollbarWidth = 17.0;
constexpr double kDeskCellWidth = 76.0;
constexpr double kDeskCellHeight = 80.0;
constexpr double kStartMenuWidth = 380.0;
constexpr double kStartMenuHeight = 470.0;
constexpr double kMenuWidth = 180.0;
constexpr double kMenuRowHeight = 22.0;
constexpr double kMenuSeparatorHeight = 7.0;
constexpr double kDialogButtonWidth = 75.0;
constexpr double kDialogButtonHeight = 23.0;
constexpr double kWheelStep = 48.0;

float f(double v) { return static_cast<float>(v); }

// ---------------------------------------------------------------------------
// Paint helpers
// ---------------------------------------------------------------------------

std::uint32_t mix(std::uint32_t a, std::uint32_t b, double t) {
    const auto channel = [&](int shift) {
        const double v = ((a >> shift) & 0xFFu) * (1.0 - t) + ((b >> shift) & 0xFFu) * t;
        return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0, 255.0))) << shift;
    };
    return channel(16) | channel(8) | channel(0);
}

std::uint32_t sample(const Stops& stops, double t) {
    if (t <= stops.front().at) return stops.front().rgb;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].at) {
            const double span = stops[i].at - stops[i - 1].at;
            return mix(stops[i - 1].rgb, stops[i].rgb, span > 0 ? (t - stops[i - 1].at) / span : 1.0);
        }
    }
    return stops.back().rgb;
}

/// A top-to-bottom gradient as thin horizontal bands: the canvas has no
/// gradients, and every bar Luna draws is one. Each band overlaps the next a
/// little so no seam of the background shows between them.
void bands(Canvas& c, Rect r, const Stops& stops) {
    if (r.w <= 0 || r.h <= 0) return;
    const int n = std::clamp(static_cast<int>(std::ceil(r.h)), 1, 48);
    for (int i = 0; i < n; ++i) {
        const double y0 = r.y + r.h * i / n;
        const double y1 = r.y + r.h * (i + 1) / n;
        setFill(c, sample(stops, (i + 0.5) / n));
        c.fillRect(f(r.x), f(y0), f(r.w), f(y1 - y0 + (i + 1 < n ? 0.6 : 0.0)));
    }
}

/// A crisp one-pixel outline, drawn as four strips.
void outline(Canvas& c, Rect r, std::uint32_t rgb, double alpha = 1.0) {
    setFill(c, rgb, alpha);
    c.fillRect(f(r.x), f(r.y), f(r.w), 1.0f);
    c.fillRect(f(r.x), f(r.bottom() - 1), f(r.w), 1.0f);
    c.fillRect(f(r.x), f(r.y), 1.0f, f(r.h));
    c.fillRect(f(r.right() - 1), f(r.y), 1.0f, f(r.h));
}

void fillRect(Canvas& c, Rect r, std::uint32_t rgb, double alpha = 1.0) {
    setFill(c, rgb, alpha);
    c.fillRect(f(r.x), f(r.y), f(r.w), f(r.h));
}

/// Clips to `r` with only its top corners rounded, as a title bar is.
void clipRoundTop(Canvas& c, Rect r, double radius) {
    c.beginPath();
    c.rect(f(r.x), f(r.y), f(r.w), f(r.h));
    c.clip();
    c.beginPath();
    c.roundRect(f(r.x), f(r.y), f(r.w), f(r.h + radius), f(radius));
    c.clip();
}

void clipRound(Canvas& c, Rect r, double radius) {
    c.beginPath();
    c.roundRect(f(r.x), f(r.y), f(r.w), f(r.h), f(radius));
    c.clip();
}

void strokeRound(Canvas& c, Rect r, double radius, std::uint32_t rgb, double width = 1.0,
                 double alpha = 1.0) {
    c.beginPath();
    c.roundRect(f(r.x + width * 0.5), f(r.y + width * 0.5), f(r.w - width), f(r.h - width),
                f(radius));
    setStroke(c, rgb, alpha);
    c.setLineWidth(f(width));
    c.stroke();
}

void write(Canvas& c, const std::string& s, double x, double y, const char* font,
           std::uint32_t rgb, const char* align = "left") {
    c.setFont(font);
    c.setTextAlign(align);
    c.setTextBaseline("middle");
    setFill(c, rgb);
    c.fillText(s, f(x), f(y));
}

/// White on the yellow bars, over a one-pixel shadow so it reads.
void shadowed(Canvas& c, const std::string& s, double x, double y, const char* font,
              std::uint32_t rgb, std::uint32_t shadow, const char* align = "left") {
    write(c, s, x + 1, y + 1, font, shadow, align);
    write(c, s, x, y, font, rgb, align);
}

double measure(Canvas& c, const std::string& s, const char* font) {
    c.setFont(font);
    return c.measureText(s);
}

/// Drops the last glyph of a UTF-8 string, continuation bytes and all.
void popGlyph(std::string& s) {
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0u) == 0x80u) s.pop_back();
    if (!s.empty()) s.pop_back();
}

/// `s` cut down with an ellipsis until it fits `width`.
std::string fit(Canvas& c, const std::string& s, const char* font, double width) {
    if (measure(c, s, font) <= width) return s;
    std::string cut = s;
    while (!cut.empty() && c.measureText(cut + "...") > width) popGlyph(cut);
    return cut + "...";
}

/// `s` broken into lines no wider than `width`, at spaces, with '\n' as a
/// paragraph break.
std::vector<std::string> wrap(Canvas& c, const std::string& s, const char* font, double width) {
    c.setFont(font);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = s.find('\n', start);
        const std::string paragraph = s.substr(start, end == std::string::npos ? end : end - start);
        std::string line;
        std::size_t at = 0;
        while (at <= paragraph.size()) {
            std::size_t space = paragraph.find(' ', at);
            if (space == std::string::npos) space = paragraph.size();
            const std::string word = paragraph.substr(at, space - at);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && c.measureText(tried) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = tried;
            }
            at = space + 1;
        }
        lines.push_back(line);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

std::string clockText() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (local == nullptr) return "12:00 PM";
    const int hour = local->tm_hour % 12 == 0 ? 12 : local->tm_hour % 12;
    char text[16];
    std::snprintf(text, sizeof text, "%d:%02d %s", hour, local->tm_min,
                  local->tm_hour < 12 ? "AM" : "PM");
    return text;
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
//
// Every icon is drawn in a 32-unit square and scaled to the size asked for.

void poly(Canvas& c, std::initializer_list<Vec2> points) {
    c.beginPath();
    bool first = true;
    for (const Vec2 p : points) {
        if (first) c.moveTo(f(p.x), f(p.y));
        else c.lineTo(f(p.x), f(p.y));
        first = false;
    }
    c.closePath();
}

void paint(Canvas& c, std::uint32_t fill, std::uint32_t stroke, double width = 1.0) {
    setFill(c, fill);
    c.fill();
    setStroke(c, stroke);
    c.setLineWidth(f(width));
    c.stroke();
}

void line(Canvas& c, double x0, double y0, double x1, double y1, std::uint32_t rgb, double width) {
    c.beginPath();
    c.moveTo(f(x0), f(y0));
    c.lineTo(f(x1), f(y1));
    setStroke(c, rgb);
    c.setLineWidth(f(width));
    c.stroke();
}

void folderBack(Canvas& c) {
    poly(c, {{2, 8}, {12, 8}, {14, 10.5}, {29, 10.5}, {29, 27}, {2, 27}});
    paint(c, 0xE9C35Cu, 0xB08A30u);
}

void folderFront(Canvas& c) {
    poly(c, {{2, 13.5}, {30.5, 13.5}, {28.5, 27}, {2, 27}});
    paint(c, 0xF8DC82u, 0xC09A3Eu);
    setFill(c, 0xFFF0B8u);
    c.fillRect(3, 14.5f, 26.5f, 1.2f);
}

void page(Canvas& c) {
    poly(c, {{7, 3}, {21, 3}, {26, 8}, {26, 29}, {7, 29}});
    paint(c, kWhite, 0x808080u);
    poly(c, {{21, 3}, {21, 8}, {26, 8}});
    paint(c, 0xE6E6E6u, 0x808080u);
}

void windowIcon(Canvas& c, std::uint32_t body, std::uint32_t edge, std::uint32_t title) {
    c.beginPath();
    c.rect(3, 5, 26, 22);
    paint(c, body, edge);
    setFill(c, title);
    c.fillRect(3.5f, 5.5f, 25, 5);
}

void drawIcon(Canvas& c, XpIcon icon, double x, double y, double size, bool full = false) {
    c.save();
    c.translate(f(x), f(y));
    c.scale(f(size / 32.0), f(size / 32.0));
    c.setLineJoin("round");
    c.setLineCap("round");
    switch (icon) {
        case XpIcon::Folder:
            folderBack(c);
            folderFront(c);
            break;
        case XpIcon::Documents:
            folderBack(c);
            c.beginPath();
            c.rect(9, 4, 13, 15);
            paint(c, kWhite, 0x8C8C8Cu, 0.8);
            for (double ly = 8; ly < 16; ly += 2.5) line(c, 11, ly, 20, ly, 0x9AA8C0u, 0.8);
            folderFront(c);
            break;
        case XpIcon::Pictures:
            folderBack(c);
            c.beginPath();
            c.rect(8, 4, 16, 13);
            paint(c, kWhite, 0x8C8C8Cu, 0.8);
            setFill(c, 0x88C6F2u);
            c.fillRect(9.5f, 5.5f, 13, 10);
            poly(c, {{9.5, 15.5}, {13, 10}, {16, 13}, {18.5, 11}, {22.5, 15.5}});
            setFill(c, 0x4FA84Au);
            c.fill();
            folderFront(c);
            break;
        case XpIcon::Music:
            folderBack(c);
            setFill(c, 0x2D6FD6u);
            c.fillCircle(14, 13, 2.6f);
            c.fillCircle(21, 11.5f, 2.6f);
            line(c, 16.4, 13, 16.4, 4.5, 0x2D6FD6u, 1.4);
            line(c, 23.4, 11.5, 23.4, 3, 0x2D6FD6u, 1.4);
            line(c, 16.4, 4.5, 23.4, 3, 0x2D6FD6u, 2.0);
            folderFront(c);
            break;
        case XpIcon::Computer:
            c.beginPath();
            c.roundRect(5, 2, 22, 18, 1.5f);
            paint(c, 0xEDE9DAu, 0x77715Fu);
            // Its screen shows hel, with a flower lying in it.
            c.beginPath();
            c.rect(7.5f, 4.5f, 17, 12.5f);
            paint(c, 0x8F3838u, 0x3D2A2Au, 0.8);
            setFill(c, 0xFFE763u);
            c.fillCircle(16, 10.8f, 2.6f);
            c.beginPath();
            c.rect(13, 20, 6, 3);
            paint(c, 0xD6D1BFu, 0x77715Fu, 0.8);
            c.beginPath();
            c.roundRect(8, 23, 16, 2.5f, 1);
            paint(c, 0xEDE9DAu, 0x77715Fu, 0.8);
            poly(c, {{6, 27}, {26, 27}, {28, 30.5}, {4, 30.5}});
            paint(c, 0xEDE9DAu, 0x77715Fu, 0.8);
            break;
        case XpIcon::Drive:
            c.beginPath();
            c.roundRect(3, 10, 26, 13, 2);
            paint(c, 0xDAD6CCu, 0x6E6A60u);
            setFill(c, 0xBDB8ACu);
            c.fillRect(4, 17.5f, 24, 4.5f);
            setFill(c, 0xF4F2ECu);
            c.fillRect(4, 11, 24, 1.2f);
            setFill(c, 0x3BD13Bu);
            c.fillCircle(24.5f, 19.75f, 1.4f);
            break;
        case XpIcon::Floppy:
            c.beginPath();
            c.roundRect(5, 5, 22, 22, 1.5f);
            paint(c, 0x2F4F8Fu, 0x182C55u);
            setFill(c, 0xC9CDD4u);
            c.fillRect(10, 5.5f, 12, 8);
            setFill(c, 0x2F4F8Fu);
            c.fillRect(17, 7, 3, 5);
            setFill(c, kWhite);
            c.fillRect(8.5f, 16, 15, 10);
            break;
        case XpIcon::Cd:
            c.beginPath();
            c.roundRect(3, 18, 26, 10, 2);
            paint(c, 0xDAD6CCu, 0x6E6A60u);
            c.beginPath();
            c.arc(16, 13, 10, 0, f(kTau));
            paint(c, 0xE4E6F0u, 0x8C8CA0u);
            c.beginPath();
            c.arc(16, 13, 6.5f, 3.6f, 5.2f);
            setStroke(c, 0xA9D3F5u);
            c.setLineWidth(2.0f);
            c.stroke();
            c.beginPath();
            c.arc(16, 13, 2.6f, 0, f(kTau));
            paint(c, kWhite, 0x8C8CA0u, 0.8);
            break;
        case XpIcon::RecycleBin:
            if (full) {
                setFill(c, kWhite);
                setStroke(c, 0x9A9A9Au);
                c.setLineWidth(0.8f);
                for (const Vec2 p : {Vec2{12, 6}, Vec2{18, 5}, Vec2{15, 3.5}}) {
                    c.beginPath();
                    c.arc(f(p.x), f(p.y), 3.0f, 0, f(kTau));
                    c.fill();
                    c.stroke();
                }
            }
            poly(c, {{8, 8}, {24, 8}, {22, 29}, {10, 29}});
            paint(c, 0xCFE6F2u, 0x4F7F99u);
            line(c, 12, 11, 12.8, 27, 0x7FA9BFu, 0.8);
            line(c, 16, 11, 16, 27, 0x7FA9BFu, 0.8);
            line(c, 20, 11, 19.2, 27, 0x7FA9BFu, 0.8);
            c.beginPath();
            c.ellipse(16, 8, 9, 2.6f, 0, 0, f(kTau));
            paint(c, 0xE6F3FAu, 0x4F7F99u);
            break;
        case XpIcon::Globe:
            c.beginPath();
            c.arc(16, 16, 12, 0, f(kTau));
            paint(c, 0x3E7FD6u, 0x1E4F99u);
            setFill(c, 0x5DB548u);
            poly(c, {{9, 9}, {14, 8}, {15, 12}, {12, 15}, {13, 19}, {10, 20}, {7, 15}});
            c.fill();
            poly(c, {{18, 14}, {24, 12}, {26.5, 17}, {22, 24}, {19, 22}});
            c.fill();
            c.beginPath();
            c.arc(16, 16, 9, -2.4f, -1.3f);
            setStroke(c, kWhite, 0.6);
            c.setLineWidth(1.4f);
            c.stroke();
            break;
        case XpIcon::Application:
            windowIcon(c, kWhite, 0x3A5FA8u, 0x3C6FE0u);
            setFill(c, 0xD8E2F5u);
            c.fillRect(6, 13, 9, 11);
            setFill(c, 0xE8EEF9u);
            c.fillRect(17, 13, 9, 11);
            break;
        case XpIcon::Florr:
            windowIcon(c, kWhite, 0xA88F22u, 0xE6C53Cu);
            c.beginPath();
            c.arc(16, 18.5f, 6.5f, 0, f(kTau));
            paint(c, 0xFFE763u, 0xCFBB50u, 1.5);
            setFill(c, 0x222222u);
            c.fillCircle(13.8f, 17.6f, 1.0f);
            c.fillCircle(18.2f, 17.6f, 1.0f);
            c.beginPath();
            c.arc(16, 19.2f, 2.6f, 0.5f, 2.64f);
            setStroke(c, 0x222222u);
            c.setLineWidth(0.9f);
            c.stroke();
            break;
        case XpIcon::Corruption:
            windowIcon(c, 0x140404u, 0x7A0A0Au, 0xB01818u);
            setFill(c, 0x00E5FFu, 0.85);
            c.fillRect(5, 14, 9, 1.5f);
            setFill(c, 0xFF2BD6u, 0.85);
            c.fillRect(17, 23, 10, 1.5f);
            c.beginPath();
            c.arc(16, 18.5f, 6, 0, f(kTau));
            paint(c, 0xD91313u, 0x7A0A0Au, 1.2);
            line(c, 13, 16, 15, 18, kWhite, 1.0);
            line(c, 15, 16, 13, 18, kWhite, 1.0);
            line(c, 17, 16, 19, 18, kWhite, 1.0);
            line(c, 19, 16, 17, 18, kWhite, 1.0);
            break;
        case XpIcon::Text:
            page(c);
            for (double ly = 12; ly <= 24; ly += 3) line(c, 10, ly, 23, ly, 0x8FA3C8u, 1.0);
            break;
        case XpIcon::Picture:
            page(c);
            setFill(c, 0x88C6F2u);
            c.fillRect(10, 11, 13, 11);
            poly(c, {{10, 22}, {14, 16}, {17, 19}, {19, 17}, {23, 22}});
            setFill(c, 0x4FA84Au);
            c.fill();
            setFill(c, 0xFFE14Du);
            c.fillCircle(20, 14, 1.6f);
            break;
        case XpIcon::System:
            page(c);
            c.beginPath();
            c.arc(16, 19, 5, 0, f(kTau));
            setStroke(c, 0x7A7A7Au);
            c.setLineWidth(2.6f);
            c.setLineDash({1.6f, 1.4f});
            c.stroke();
            c.setLineDash({});
            setFill(c, 0xA8A8A8u);
            c.fillCircle(16, 19, 4);
            setFill(c, kWhite);
            c.fillCircle(16, 19, 1.6f);
            break;
    }
    c.restore();
}

/// An icon washed in the selection colour, as XP blends a selected one.
void drawSelectedIcon(Canvas& c, XpIcon icon, double x, double y, double size, bool full) {
    constexpr double kWash = 0.45;
    const Color previous = c.colorTint();
    c.setColorTint(Color{static_cast<std::uint8_t>(((kSelect >> 16) & 0xFFu) * kWash),
                         static_cast<std::uint8_t>(((kSelect >> 8) & 0xFFu) * kWash),
                         static_cast<std::uint8_t>((kSelect & 0xFFu) * kWash),
                         static_cast<std::uint8_t>(255 * kWash)});
    drawIcon(c, icon, x, y, size, full);
    c.setColorTint(previous);
}

/// The tray's security shield: red, with a white cross, as XP flags a
/// computer at risk.
void drawShield(Canvas& c, double x, double y) {
    c.save();
    c.translate(f(x), f(y));
    c.beginPath();
    c.moveTo(8, 1);
    c.lineTo(14.5f, 3.5f);
    c.lineTo(14.5f, 8.5f);
    c.quadraticCurveTo(14.5f, 13, 8, 15.5f);
    c.quadraticCurveTo(1.5f, 13, 1.5f, 8.5f);
    c.lineTo(1.5f, 3.5f);
    c.closePath();
    paint(c, 0xD62F2Fu, 0x7F1010u);
    line(c, 5.5, 5.5, 10.5, 10.5, kWhite, 1.8);
    line(c, 10.5, 5.5, 5.5, 10.5, kWhite, 1.8);
    c.restore();
}

// ---------------------------------------------------------------------------
// Layout shared by input and paint
// ---------------------------------------------------------------------------

Rect clientRect(Rect frame) {
    return {frame.x + kBorder, frame.y + kTitleHeight, frame.w - kBorder * 2,
            frame.h - kTitleHeight - kBorder};
}

struct CaptionButtons {
    Rect close, maximize, minimize;
};

CaptionButtons captionButtons(Rect frame, bool full) {
    CaptionButtons b;
    b.close = {frame.right() - 5 - 21, frame.y + 5, 21, 21};
    if (full) {
        b.maximize = {b.close.x - 2 - 21, b.close.y, 21, 21};
        b.minimize = {b.maximize.x - 2 - 21, b.close.y, 21, 21};
    }
    return b;
}

struct ExplorerLayout {
    Rect menuBar, toolbar, address, pane, content;
    Rect back, forward, up;
};

ExplorerLayout explorerLayout(Rect client) {
    ExplorerLayout l;
    l.menuBar = {client.x, client.y, client.w, kMenuBarHeight};
    l.toolbar = {client.x, l.menuBar.bottom(), client.w, kToolbarHeight};
    l.address = {client.x, l.toolbar.bottom(), client.w, kAddressHeight};
    const double top = l.address.bottom();
    const double paneWidth = client.w >= 480.0 ? kPaneWidth : 0.0;
    l.pane = {client.x, top, paneWidth, client.bottom() - top};
    l.content = {client.x + paneWidth, top, client.w - paneWidth, client.bottom() - top};
    l.back = {l.toolbar.x + 4, l.toolbar.y + 3, 72, 32};
    l.forward = {l.back.right() + 2, l.back.y, 40, 32};
    l.up = {l.forward.right() + 4, l.back.y, 36, 32};
    return l;
}

int tileColumns(double width) {
    return std::max(1, static_cast<int>((width - 16.0) / kTileWidth));
}

double tilesHeight(double width, int count) {
    const int columns = tileColumns(width);
    return 20.0 + std::ceil(count / static_cast<double>(columns)) * kTileHeight;
}

/// Where the tiles go: the whole content area, less a scrollbar if they
/// overflow it.
Rect tileView(Rect content, int count) {
    if (tilesHeight(content.w, count) <= content.h) return content;
    return {content.x, content.y, content.w - kScrollbarWidth, content.h};
}

double maxScroll(Rect content, int count) {
    const Rect view = tileView(content, count);
    return std::max(0.0, tilesHeight(view.w, count) - view.h);
}

Rect tileAt(Rect view, int i, double scroll) {
    const int columns = tileColumns(view.w);
    return {view.x + 10 + (i % columns) * kTileWidth, view.y + 10 + (i / columns) * kTileHeight - scroll,
            kTileWidth - 8, kTileHeight - 4};
}

// -- Explorer's task pane ------------------------------------------------------

enum class PaneAction : std::uint8_t { None, Delete, EmptyBin, Go, Restricted };

struct PaneLinkSpec {
    std::string label;
    XpIcon icon;
    PaneAction action;
    int target;
};

struct PanePanelSpec {
    std::string title;
    std::vector<PaneLinkSpec> links;
    std::vector<std::string> details;
};

std::vector<PanePanelSpec> panePanels(const XpFileSystem& fs, int folder, int selected) {
    std::vector<PanePanelSpec> panels;
    PanePanelSpec tasks;
    if (folder == fs.recycleBin()) {
        tasks.title = "Recycle Bin Tasks";
        tasks.links = {{"Empty the Recycle Bin", XpIcon::RecycleBin, PaneAction::EmptyBin, -1},
                       {"Restore all items", XpIcon::RecycleBin, PaneAction::Restricted, -1}};
    } else if (folder == fs.myComputer()) {
        tasks.title = "System Tasks";
        tasks.links = {{"View system information", XpIcon::Computer, PaneAction::Restricted, -1},
                       {"Add or remove programs", XpIcon::Application, PaneAction::Restricted, -1},
                       {"Change a setting", XpIcon::System, PaneAction::Restricted, -1}};
    } else if (selected >= 0) {
        const bool isFolder = fs.node(fs.resolve(selected)).open == XpOpen::Browse;
        const std::string what = isFolder ? "folder" : "file";
        tasks.title = "File and Folder Tasks";
        tasks.links = {{"Rename this " + what, XpIcon::Text, PaneAction::Restricted, -1},
                       {"Move this " + what, XpIcon::Folder, PaneAction::Restricted, -1},
                       {"Copy this " + what, XpIcon::Folder, PaneAction::Restricted, -1},
                       {"Delete this " + what, XpIcon::Corruption, PaneAction::Delete, selected}};
    } else {
        tasks.title = "File and Folder Tasks";
        tasks.links = {{"Make a new folder", XpIcon::Folder, PaneAction::Restricted, -1},
                       {"Publish this folder to the Web", XpIcon::Globe, PaneAction::Restricted, -1},
                       {"Share this folder", XpIcon::Folder, PaneAction::Restricted, -1}};
    }
    panels.push_back(std::move(tasks));

    PanePanelSpec places;
    places.title = "Other Places";
    const int parent = fs.node(folder).parent;
    if (parent >= 0) places.links.push_back({fs.node(parent).name, fs.node(parent).icon, PaneAction::Go, parent});
    if (folder != fs.myDocuments() && parent != fs.myDocuments()) {
        places.links.push_back({"My Documents", XpIcon::Documents, PaneAction::Go, fs.myDocuments()});
    }
    if (folder != fs.myComputer() && parent != fs.myComputer()) {
        places.links.push_back({"My Computer", XpIcon::Computer, PaneAction::Go, fs.myComputer()});
    }
    places.links.push_back({"My Network Places", XpIcon::Globe, PaneAction::Restricted, -1});
    panels.push_back(std::move(places));

    PanePanelSpec details;
    details.title = "Details";
    const XpNode& shown = fs.node(selected >= 0 ? selected : folder);
    details.details = {shown.name, shown.type};
    if (!shown.size.empty()) details.details.push_back("Size: " + shown.size);
    panels.push_back(std::move(details));
    return panels;
}

struct PaneGeometry {
    std::vector<Rect> headers;
    std::vector<Rect> bodies;
    std::vector<Rect> links;        ///< every panel's links, in order
    std::vector<PaneLinkSpec> specs;
};

PaneGeometry paneGeometry(Rect pane, const std::vector<PanePanelSpec>& panels) {
    PaneGeometry g;
    const double x = pane.x + 12;
    const double w = pane.w - 24;
    double y = pane.y + 12;
    for (const PanePanelSpec& panel : panels) {
        const Rect header{x, y, w, 25};
        const double rows = panel.links.empty() ? panel.details.size() * 14.0 : panel.links.size() * 20.0;
        const Rect body{x, header.bottom(), w, rows + 16};
        double ly = body.y + 8;
        for (const PaneLinkSpec& link : panel.links) {
            g.links.push_back({x + 8, ly, w - 16, 18});
            g.specs.push_back(link);
            ly += 20;
        }
        g.headers.push_back(header);
        g.bodies.push_back(body);
        y = body.bottom() + 15;
    }
    return g;
}

// -- the Start menu ------------------------------------------------------------

enum class StartAction : std::uint8_t {
    None, MyDocuments, MyPictures, MyMusic, MyComputer, Internet, Florr, Notepad, Explorer,
    Corruption, Restricted, LogOff, TurnOff,
};

struct StartEntry {
    Rect rect;
    std::string title;
    std::string sub;
    XpIcon icon = XpIcon::Folder;
    StartAction action = StartAction::None;
    /// 0 for the white column, 1 for the yellow one, 2 for the bottom bar.
    int side = 0;
    bool bold = false;
    bool separator = false;
};

constexpr double kStartHeader = 64.0;
constexpr double kStartFooter = 40.0;

std::vector<StartEntry> startEntries(Rect menu) {
    std::vector<StartEntry> e;
    const double top = menu.y + kStartHeader + 2;
    const double bottom = menu.bottom() - kStartFooter;
    const double half = menu.w * 0.5;

    // The white column: two pinned programs, then the recent ones.
    double y = top + 6;
    const auto left = [&](const std::string& title, const std::string& sub, XpIcon icon,
                          StartAction action, double h, bool bold) {
        StartEntry s;
        s.rect = {menu.x + 4, y, half - 8, h};
        s.title = title;
        s.sub = sub;
        s.icon = icon;
        s.action = action;
        s.bold = bold;
        e.push_back(s);
        y += h;
    };
    const auto separator = [&](int side, double x, double w) {
        StartEntry s;
        s.rect = {x, y, w, 9};
        s.side = side;
        s.separator = true;
        e.push_back(s);
        y += 9;
    };
    left("Internet", "Internet Explorer", XpIcon::Globe, StartAction::Internet, 40, true);
    left("florr", "florr.exe", XpIcon::Florr, StartAction::Florr, 40, true);
    separator(0, menu.x + 10, half - 20);
    left("Notepad", "", XpIcon::Text, StartAction::Notepad, 36, false);
    left("Windows Explorer", "", XpIcon::Folder, StartAction::Explorer, 36, false);
    left("Windows Picture and Fax Viewer", "", XpIcon::Picture, StartAction::Restricted, 36, false);
    left("corruption.exe", "", XpIcon::Corruption, StartAction::Corruption, 36, false);
    {
        StartEntry all;
        all.rect = {menu.x + 4, bottom - 36, half - 8, 30};
        all.title = "All Programs";
        all.bold = true;
        all.action = StartAction::None;
        y = all.rect.y - 9;
        separator(0, menu.x + 10, half - 20);
        e.push_back(all);
    }

    // The yellow column.
    y = top + 6;
    const auto right = [&](const std::string& title, XpIcon icon, StartAction action, bool bold) {
        StartEntry s;
        s.rect = {menu.x + half + 4, y, half - 8, 28};
        s.title = title;
        s.icon = icon;
        s.action = action;
        s.side = 1;
        s.bold = bold;
        e.push_back(s);
        y += 28;
    };
    right("My Documents", XpIcon::Documents, StartAction::MyDocuments, true);
    right("My Pictures", XpIcon::Pictures, StartAction::MyPictures, true);
    right("My Music", XpIcon::Music, StartAction::MyMusic, true);
    right("My Computer", XpIcon::Computer, StartAction::MyComputer, true);
    separator(1, menu.x + half + 10, half - 20);
    right("Control Panel", XpIcon::System, StartAction::Restricted, false);
    right("Printers and Faxes", XpIcon::Application, StartAction::Restricted, false);
    separator(1, menu.x + half + 10, half - 20);
    right("Help and Support", XpIcon::Globe, StartAction::Restricted, false);
    right("Search", XpIcon::Folder, StartAction::Restricted, false);
    right("Run...", XpIcon::Application, StartAction::Restricted, false);

    // The bar along the bottom.
    StartEntry logOff;
    logOff.rect = {menu.right() - 232, bottom + 6, 92, 28};
    logOff.title = "Log Off";
    logOff.action = StartAction::LogOff;
    logOff.side = 2;
    e.push_back(logOff);
    StartEntry turnOff = logOff;
    turnOff.rect = {menu.right() - 136, bottom + 6, 130, 28};
    turnOff.title = "Turn Off Computer";
    turnOff.action = StartAction::TurnOff;
    e.push_back(turnOff);
    return e;
}

// -- the right-click menu ------------------------------------------------------

enum class MenuAction : std::uint8_t { None, Open, Delete };

struct MenuEntry {
    Rect rect;
    std::string label;
    MenuAction action = MenuAction::None;
    bool enabled = false;
    bool bold = false;
    bool separator = false;
};

std::vector<MenuEntry> menuEntries(Vec2 at) {
    struct Row { const char* label; MenuAction action; bool enabled; bool bold; };
    static const Row kRows[] = {
        {"Open", MenuAction::Open, true, true},
        {nullptr, MenuAction::None, false, false},
        {"Send To", MenuAction::None, false, false},
        {nullptr, MenuAction::None, false, false},
        {"Cut", MenuAction::None, false, false},
        {"Copy", MenuAction::None, false, false},
        {nullptr, MenuAction::None, false, false},
        {"Create Shortcut", MenuAction::None, false, false},
        {"Delete", MenuAction::Delete, true, false},
        {"Rename", MenuAction::None, false, false},
        {nullptr, MenuAction::None, false, false},
        {"Properties", MenuAction::None, false, false},
    };
    std::vector<MenuEntry> e;
    double y = at.y + 3;
    for (const Row& row : kRows) {
        MenuEntry m;
        m.separator = row.label == nullptr;
        const double h = m.separator ? kMenuSeparatorHeight : kMenuRowHeight;
        m.rect = {at.x + 3, y, kMenuWidth - 6, h};
        if (!m.separator) {
            m.label = row.label;
            m.action = row.action;
            m.enabled = row.enabled;
            m.bold = row.bold;
        }
        e.push_back(m);
        y += h;
    }
    return e;
}

double menuHeight() {
    const std::vector<MenuEntry> e = menuEntries({0, 0});
    return e.back().rect.bottom() + 3;
}

std::vector<Rect> dialogButtons(Rect client, bool yesNo) {
    const int n = yesNo ? 2 : 1;
    const double total = n * kDialogButtonWidth + (n - 1) * 6.0;
    const double x0 = client.x + (client.w - total) * 0.5;
    const double y = client.bottom() - 12 - kDialogButtonHeight;
    std::vector<Rect> r;
    for (int i = 0; i < n; ++i) r.push_back({x0 + i * (kDialogButtonWidth + 6.0), y, kDialogButtonWidth, kDialogButtonHeight});
    return r;
}

const char* const kCrashText =
    "The instruction at \"0x7c9100b8\" referenced memory at \"0x00000000\". The memory could "
    "not be \"read\".\n\nClick on OK to terminate the program";
const char* const kRestrictedText =
    "This operation has been cancelled due to restrictions in effect on this computer. Please "
    "contact your system administrator.";

std::string lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

/// The address a folder really has, which a shell folder's display name
/// hides: My Pictures is C:\Documents and Settings\...\My Pictures, not "My
/// Documents\My Pictures".
std::string realPath(const XpFileSystem& fs, int id) {
    const XpNode& n = fs.node(id);
    if (n.parent < 0) return n.name;
    if (n.icon == XpIcon::Drive) return n.address;
    std::string up = realPath(fs, n.parent);
    if (!up.empty() && up.back() != '\\') up += '\\';
    return up + n.name;
}

} // namespace

// ---------------------------------------------------------------------------
// The file system
// ---------------------------------------------------------------------------

int XpFileSystem::add(int parent, XpNode node) {
    node.parent = parent;
    const int id = size();
    nodes_.push_back(std::move(node));
    if (parent >= 0) nodes_[static_cast<std::size_t>(parent)].children.push_back(id);
    return id;
}

int XpFileSystem::folder(int parent, const std::string& name, XpIcon icon) {
    XpNode n;
    n.name = name;
    n.icon = icon;
    n.open = XpOpen::Browse;
    n.type = "File Folder";
    return add(parent, std::move(n));
}

XpFileSystem XpFileSystem::standard(const std::string& user) {
    XpFileSystem fs;
    const auto file = [&fs](int parent, const std::string& name, XpIcon icon, XpOpen open,
                            const std::string& type, const std::string& size,
                            const std::string& text = {}, const std::string& caption = {}) {
        XpNode n;
        n.name = name;
        n.icon = icon;
        n.open = open;
        n.type = type;
        n.size = size;
        n.text = text;
        n.caption = caption;
        return fs.add(parent, std::move(n));
    };
    const auto corruption = [&](int parent) {
        XpNode n;
        n.name = "corruption.exe";
        n.icon = XpIcon::Corruption;
        n.open = XpOpen::Crash;
        n.type = "Application";
        n.size = "666 KB";
        n.corruption = true;
        return fs.add(parent, std::move(n));
    };
    // A file the system owns: an .exe that will not start, anything else
    // Windows has no program for.
    const auto systemFile = [&](int parent, const std::string& name) {
        const std::string ext = name.substr(name.find_last_of('.') + 1);
        if (ext == "exe") {
            return file(parent, name, XpIcon::Application, XpOpen::Message, "Application", "512 KB",
                        name + " is not a valid Win32 application.", name);
        }
        const std::string type = ext == "dll"   ? "Application Extension"
                                 : ext == "sys" ? "System file"
                                 : ext == "dat" ? "DAT File"
                                                : "Configuration Settings";
        return file(parent, name, XpIcon::System, XpOpen::Message, type, "64 KB",
                    "Windows cannot open this file:\n\nFile: " + name +
                        "\n\nTo open this file, Windows needs to know what program created it.",
                    "Windows");
    };

    {
        XpNode desk;
        desk.name = "Desktop";
        desk.type = "System Folder";
        desk.address = "Desktop";
        fs.desktop_ = fs.add(-1, std::move(desk));
    }
    {
        XpNode computer;
        computer.name = "My Computer";
        computer.icon = XpIcon::Computer;
        computer.type = "System Folder";
        computer.address = "My Computer";
        fs.myComputer_ = fs.add(fs.desktop_, std::move(computer));
    }

    // -- My Computer --
    int c = -1;
    {
        XpNode drive;
        drive.name = "Local Disk (C:)";
        drive.icon = XpIcon::Drive;
        drive.type = "Local Disk";
        drive.address = "C:\\";
        c = fs.add(fs.myComputer_, std::move(drive));
    }
    file(fs.myComputer_, "3\xC2\xBD Floppy (A:)", XpIcon::Floppy, XpOpen::Message,
         "3\xC2\xBD-Inch Floppy Disk", "", "A:\\ is not accessible.\n\nThe device is not ready.",
         "A:\\");
    file(fs.myComputer_, "CD Drive (D:)", XpIcon::Cd, XpOpen::Message, "CD Drive", "",
         "D:\\ is not accessible.\n\nThe device is not ready.", "D:\\");

    // -- C:\Documents and Settings --
    const int documentsAndSettings = fs.folder(c, "Documents and Settings");
    fs.folder(documentsAndSettings, "All Users");
    const int home = fs.folder(documentsAndSettings, user);
    fs.myDocuments_ = fs.folder(home, "My Documents", XpIcon::Documents);
    fs.nodes_[static_cast<std::size_t>(fs.myDocuments_)].address = "My Documents";
    fs.folder(home, "Favorites");
    fs.myMusic_ = fs.folder(fs.myDocuments_, "My Music", XpIcon::Music);
    fs.myPictures_ = fs.folder(fs.myDocuments_, "My Pictures", XpIcon::Pictures);
    file(fs.myPictures_, "dead_flower.bmp", XpIcon::Picture, XpOpen::Picture, "Bitmap Image", "192 KB");
    file(fs.myPictures_, "garden.bmp", XpIcon::Picture, XpOpen::Picture, "Bitmap Image", "768 KB");
    corruption(fs.myPictures_);
    file(fs.myDocuments_, "diary.txt", XpIcon::Text, XpOpen::Notepad, "Text Document", "1 KB",
         "day 1\n"
         "found a garden. the flowers here are friendly.\n\n"
         "day 4\n"
         "a door opened somewhere it should not have.\n"
         "the grass went red on the other side.\n\n"
         "day 9\n"
         "every flower i meet now has crosses for eyes.\n\n"
         "day 12\n"
         "the game crashed twice. the second time it did not\n"
         "send me back. it sent me here.\n\n"
         "day ?\n"
         "if you find this, delete them. all of them.");

    // -- C:\Program Files --
    const int programFiles = fs.folder(c, "Program Files");
    const int florr = fs.folder(programFiles, "florr");
    const std::string florrMessage =
        "florr.exe cannot start while corruption.exe is on this computer.";
    file(florr, "florr.exe", XpIcon::Florr, XpOpen::Message, "Application", "2,048 KB", florrMessage,
         "florr.exe");
    systemFile(florr, "mobs.dat");
    systemFile(florr, "petals.dat");
    corruption(florr);
    const std::string internetMessage =
        "The page cannot be displayed.\n\nThere is nothing out there any more.";
    const int ie = fs.folder(programFiles, "Internet Explorer");
    file(ie, "iexplore.exe", XpIcon::Globe, XpOpen::Message, "Application", "91 KB", internetMessage,
         "Internet Explorer");
    const int mediaPlayer = fs.folder(programFiles, "Windows Media Player");
    systemFile(mediaPlayer, "wmplayer.exe");

    // -- C:\WINDOWS --
    const int windowsDir = fs.folder(c, "WINDOWS");
    const int system32 = fs.folder(windowsDir, "system32");
    for (const char* name : {"advapi32.dll", "calc.exe", "cmd.exe", "comctl32.dll", "comdlg32.dll",
                             "gdi32.dll", "hal.dll", "kernel32.dll", "mspaint.exe", "msvcrt.dll",
                             "ntdll.dll", "ntoskrnl.exe", "shell32.dll", "taskmgr.exe",
                             "user32.dll", "wininet.dll", "winlogon.exe", "winmine.exe"}) {
        systemFile(system32, name);
    }
    corruption(system32);
    fs.folder(windowsDir, "Fonts");
    fs.folder(windowsDir, "Temp");
    const int web = fs.folder(windowsDir, "Web");
    const int wallpaper = fs.folder(web, "Wallpaper");
    file(wallpaper, "hel.bmp", XpIcon::Picture, XpOpen::Picture, "Bitmap Image", "1,406 KB");
    systemFile(windowsDir, "explorer.exe");
    systemFile(windowsDir, "notepad.exe");
    file(windowsDir, "win.ini", XpIcon::Text, XpOpen::Notepad, "Configuration Settings", "1 KB",
         "; for 16-bit app support\n[fonts]\n[extensions]\n[mci extensions]\n[files]\n[Mail]\n"
         "MAPI=1\n[corruption]\ncopies=5\nlast_seen=everywhere");

    file(c, "AUTOEXEC.BAT", XpIcon::System, XpOpen::Notepad, "MS-DOS Batch File", "0 KB", "");
    file(c, "boot.ini", XpIcon::Text, XpOpen::Notepad, "Configuration Settings", "1 KB",
         "[boot loader]\ntimeout=30\ndefault=multi(0)disk(0)rdisk(0)partition(1)\\WINDOWS\n"
         "[operating systems]\n"
         "multi(0)disk(0)rdisk(0)partition(1)\\WINDOWS=\"florr.exe\" /fastdetect");

    // -- the desktop's own items, in the order the column lists them --
    {
        XpNode documents;
        documents.name = "My Documents";
        documents.icon = XpIcon::Documents;
        documents.type = "System Folder";
        documents.target = fs.myDocuments_;
        fs.add(fs.desktop_, std::move(documents));
    }
    // My Computer went in first; it is moved after My Documents, where XP
    // puts it.
    {
        std::vector<int>& top = fs.nodes_[static_cast<std::size_t>(fs.desktop_)].children;
        std::rotate(top.begin(), top.begin() + 1, top.end());
    }
    file(fs.desktop_, "Internet Explorer", XpIcon::Globe, XpOpen::Message, "Shortcut", "1 KB",
         internetMessage, "Internet Explorer");
    file(fs.desktop_, "florr.exe", XpIcon::Florr, XpOpen::Message, "Shortcut", "1 KB", florrMessage,
         "florr.exe");
    file(fs.desktop_, "READ ME.txt", XpIcon::Text, XpOpen::Notepad, "Text Document", "1 KB",
         "If you are reading this, you made it out of the garden.\n\n"
         "This is not your computer. It is where the flowers go.\n\n"
         "The corruption copied itself five times:\n"
         "  - one where you can see it\n"
         "  - one where the game lives\n"
         "  - one among the system files\n"
         "  - one with the pictures\n"
         "  - and one that was already thrown away\n\n"
         "Delete every corruption.exe and you can go back.");
    corruption(fs.desktop_);
    {
        XpNode bin;
        bin.name = "Recycle Bin";
        bin.icon = XpIcon::RecycleBin;
        bin.type = "System Folder";
        bin.address = "Recycle Bin";
        fs.recycleBin_ = fs.add(fs.desktop_, std::move(bin));
    }
    corruption(fs.recycleBin_);
    file(fs.recycleBin_, "old_petals.txt", XpIcon::Text, XpOpen::Notepad, "Text Document", "1 KB",
         "basic x5\nrose x2\nlight x3\n\nnone of them work here.");
    return fs;
}

int XpFileSystem::resolve(int id) const {
    const int target = node(id).target;
    return target >= 0 ? target : id;
}

std::vector<int> XpFileSystem::listing(int folder) const {
    std::vector<int> items;
    for (const int child : node(folder).children) {
        if (!node(child).deleted) items.push_back(child);
    }
    // The desktop keeps the order its column was laid out in; every other
    // folder is folders first, then files, each by name, as XP sorts.
    if (folder == desktop_) return items;
    std::stable_sort(items.begin(), items.end(), [&](int a, int b) {
        const bool fa = node(resolve(a)).open == XpOpen::Browse;
        const bool fb = node(resolve(b)).open == XpOpen::Browse;
        if (fa != fb) return fa;
        return lower(node(a).name) < lower(node(b).name);
    });
    return items;
}

std::string XpFileSystem::addressOf(int folder) const {
    const XpNode& n = node(folder);
    return n.address.empty() ? realPath(*this, folder) : n.address;
}

int XpFileSystem::find(const std::string& path) const {
    int at = desktop_;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string name = path.substr(start, end - start);
        int found = -1;
        for (const int child : node(resolve(at)).children) {
            if (!node(child).deleted && node(child).name == name) {
                found = child;
                break;
            }
        }
        if (found < 0) return -1;
        at = found;
        start = end + 1;
    }
    return at;
}

bool XpFileSystem::canDelete(int id) const {
    return id >= 0 && id < size() && node(id).corruption && !node(id).deleted;
}

bool XpFileSystem::remove(int id) {
    if (!canDelete(id)) return false;
    nodes_[static_cast<std::size_t>(id)].deleted = true;
    return true;
}

void XpFileSystem::emptyRecycleBin() {
    for (const int child : node(recycleBin_).children) {
        nodes_[static_cast<std::size_t>(child)].deleted = true;
    }
}

int XpFileSystem::corruptionTotal() const {
    return static_cast<int>(std::count_if(nodes_.begin(), nodes_.end(),
                                          [](const XpNode& n) { return n.corruption; }));
}

int XpFileSystem::corruptionLeft() const {
    return static_cast<int>(std::count_if(nodes_.begin(), nodes_.end(), [](const XpNode& n) {
        return n.corruption && !n.deleted;
    }));
}

// ---------------------------------------------------------------------------
// The desktop: lifetime
// ---------------------------------------------------------------------------

void XpDesktop::open(double now, const std::string& user) {
    *this = XpDesktop{};
    active_ = true;
    openedAt_ = now;
    now_ = now;
    // XP's own name for a computer nobody set up.
    user_ = user.empty() ? "Owner" : user;
    fs_ = XpFileSystem::standard(user_);
}

void XpDesktop::close() { *this = XpDesktop{}; }

bool XpDesktop::finished(double now) const {
    return active_ && doneAt_ >= 0.0 && now - doneAt_ >= kGoodbyeSeconds;
}

std::vector<web::Sound> XpDesktop::takeSounds() {
    std::vector<web::Sound> taken;
    taken.swap(sounds_);
    return taken;
}

std::string XpDesktop::frontTitle() const {
    return windows_.empty() ? std::string() : titleOf(windows_.back());
}

Vec2 XpDesktop::itemCentre(int node) const {
    // In the front-most Explorer that lists it, or else on the desktop.
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        if (it->minimized || it->kind != Kind::Explorer) continue;
        const ExplorerLayout l = explorerLayout(clientRect(windowRect(*it)));
        const std::vector<int> items = fs_.listing(it->folder);
        const Rect view = tileView(l.content, static_cast<int>(items.size()));
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i] != node) continue;
            const Rect tile = tileAt(view, static_cast<int>(i), it->scroll);
            return {(tile.x + 26) * scale_, (tile.y + tile.h * 0.5) * scale_};
        }
    }
    for (const DeskSlot& slot : deskSlots()) {
        if (slot.node != node) continue;
        const Rect cell = deskCell(slot.col, slot.row);
        return {(cell.x + cell.w * 0.5) * scale_, (cell.y + 20) * scale_};
    }
    return {-1, -1};
}

std::string XpDesktop::titleOf(const Win& w) const {
    switch (w.kind) {
        case Kind::Explorer: return fs_.node(w.folder).name;
        case Kind::Notepad:
            return (w.file >= 0 ? fs_.node(w.file).name : std::string("Untitled")) + " - Notepad";
        case Kind::Picture: return fs_.node(w.file).name + " - Windows Picture and Fax Viewer";
        case Kind::Message: return w.caption;
    }
    return {};
}

XpIcon XpDesktop::iconOf(const Win& w) const {
    switch (w.kind) {
        case Kind::Explorer: return fs_.node(w.folder).icon;
        case Kind::Notepad: return XpIcon::Text;
        case Kind::Picture: return XpIcon::Picture;
        case Kind::Message: break;
    }
    return XpIcon::Folder;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

Rect XpDesktop::taskbar() const { return {0, height_ - kTaskbarHeight, width_, kTaskbarHeight}; }

Rect XpDesktop::startButton() const {
    return {0, height_ - kTaskbarHeight, kStartWidth, kTaskbarHeight};
}

Rect XpDesktop::tray() const {
    constexpr double kTrayWidth = 96.0;
    return {width_ - kTrayWidth, height_ - kTaskbarHeight, kTrayWidth, kTaskbarHeight};
}

Rect XpDesktop::trayIcon() const {
    const Rect t = tray();
    return {t.x + 10, t.y + 7, 16, 16};
}

std::vector<int> XpDesktop::taskWindows() const {
    std::vector<int> ids;
    for (const Win& w : windows_) {
        if (w.kind != Kind::Message) ids.push_back(w.id);
    }
    // Opened order, which is what the taskbar keeps whatever is in front.
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<Rect> XpDesktop::taskButtons() const {
    const std::size_t n = taskWindows().size();
    std::vector<Rect> rects;
    if (n == 0) return rects;
    const double left = kStartWidth + 10;
    const double right = tray().x - 6;
    const double w = std::min(160.0, (right - left) / static_cast<double>(n) - 3.0);
    for (std::size_t i = 0; i < n; ++i) {
        rects.push_back({left + static_cast<double>(i) * (w + 3.0), taskbar().y + 3, w, 24});
    }
    return rects;
}

Rect XpDesktop::startMenu() const {
    const double h = std::min(kStartMenuHeight, height_ - kTaskbarHeight);
    return {0, height_ - kTaskbarHeight - h, kStartMenuWidth, h};
}

Rect XpDesktop::windowRect(const Win& w) const {
    if (w.maximized) return {0, 0, width_, height_ - kTaskbarHeight};
    return w.frame;
}

Rect XpDesktop::deskCell(int col, int row) const {
    const double x = col >= 0 ? 4 + col * kDeskCellWidth : width_ - 4 + col * kDeskCellWidth;
    const double y = row >= 0 ? 6 + row * kDeskCellHeight
                              : height_ - kTaskbarHeight - 6 + row * kDeskCellHeight;
    return {x, y, kDeskCellWidth, kDeskCellHeight};
}

std::vector<XpDesktop::DeskSlot> XpDesktop::deskSlots() const {
    // Down the left-hand column in order, as XP auto-arranges; the Recycle
    // Bin in the bottom-right corner, where XP keeps it; and the corruption
    // where it dropped itself, out on its own.
    std::vector<DeskSlot> slots;
    int row = 0;
    for (const int id : fs_.listing(fs_.desktop())) {
        if (id == fs_.recycleBin()) slots.push_back({id, -1, -1});
        else if (fs_.node(id).corruption) slots.push_back({id, -3, 2});
        else slots.push_back({id, 0, row++});
    }
    return slots;
}

Rect XpDesktop::contextMenuRect() const {
    return {menu_.at.x, menu_.at.y, kMenuWidth, menuHeight()};
}

Rect XpDesktop::nextFrame(double w, double h) {
    const int k = static_cast<int>(windows_.size() % 6);
    w = std::min(w, width_ - 20);
    h = std::min(h, height_ - kTaskbarHeight - 20);
    const double x = std::clamp(80.0 + 28.0 * k, 0.0, std::max(0.0, width_ - w));
    const double y = std::clamp(40.0 + 28.0 * k, 0.0, std::max(0.0, height_ - kTaskbarHeight - h));
    return {x, y, w, h};
}

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

XpDesktop::Win* XpDesktop::find(int id) {
    for (Win& w : windows_) {
        if (w.id == id) return &w;
    }
    return nullptr;
}

XpDesktop::Win* XpDesktop::modal() {
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        if (it->kind == Kind::Message) return &*it;
    }
    return nullptr;
}

int XpDesktop::activeId() const {
    // The front window is the active one, unless the desktop or the taskbar
    // took the keyboard since -- clicking the desktop greys every title bar.
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        if (it->kind == Kind::Message) return it->id;
        if (!it->minimized) return it->id == keyTarget_ ? it->id : 0;
    }
    return 0;
}

void XpDesktop::focus(int id) {
    const auto it = std::find_if(windows_.begin(), windows_.end(), [&](const Win& w) { return w.id == id; });
    if (it == windows_.end()) return;
    Win moved = std::move(*it);
    windows_.erase(it);
    windows_.push_back(std::move(moved));
    keyTarget_ = id;
}

void XpDesktop::closeWindow(int id) {
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [&](const Win& w) { return w.id == id; }),
                   windows_.end());
    if (dragging_ == id) dragging_ = -1;
    if (keyTarget_ == id) {
        keyTarget_ = 0;
        for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
            if (!it->minimized) {
                keyTarget_ = it->id;
                break;
            }
        }
    }
}

void XpDesktop::openExplorer(int folder) {
    Win w;
    w.id = nextId_++;
    w.kind = Kind::Explorer;
    w.frame = nextFrame(680, 480);
    w.folder = folder;
    windows_.push_back(std::move(w));
    keyTarget_ = windows_.back().id;
    cue(web::Sound::Navigate);
}

void XpDesktop::openViewer(Kind kind, int file) {
    for (Win& w : windows_) {
        if (w.kind == kind && w.file == file && file >= 0) {
            w.minimized = false;
            focus(w.id);
            return;
        }
    }
    Win w;
    w.id = nextId_++;
    w.kind = kind;
    w.frame = kind == Kind::Notepad ? nextFrame(560, 400) : nextFrame(560, 470);
    w.file = file;
    windows_.push_back(std::move(w));
    keyTarget_ = windows_.back().id;
}

void XpDesktop::message(const std::string& caption, const std::string& text, Glyph glyph,
                        bool yesNo, int confirm) {
    Win w;
    w.id = nextId_++;
    w.kind = Kind::Message;
    // Provisional: the draw sizes a box to its words, which only a canvas can
    // measure, and centres it.
    w.frame = {width_ * 0.5 - 170, height_ * 0.5 - 80, 340, 140};
    w.caption = caption;
    w.message = text;
    w.glyph = glyph;
    w.yesNo = yesNo;
    w.confirm = confirm;
    windows_.push_back(std::move(w));
    keyTarget_ = windows_.back().id;
    menu_.open = false;
    startOpen_ = false;
    switch (glyph) {
        case Glyph::Error: cue(web::Sound::Error); break;
        case Glyph::Warning:
        case Glyph::Info: cue(web::Sound::Notice); break;
        case Glyph::Question:
        case Glyph::Bin: cue(web::Sound::Question); break;
    }
}

void XpDesktop::crashMessage(const std::string& name) {
    message(name + " - Application Error", kCrashText, Glyph::Error);
    // In place of the ordinary error's sound, which message() has just cued.
    sounds_.back() = web::Sound::CorruptionError;
}

void XpDesktop::restrictionMessage() {
    message("Restrictions", kRestrictedText, Glyph::Error);
}

void XpDesktop::navigate(Win& w, int folder, bool record) {
    if (record && w.folder >= 0 && w.folder != folder) {
        w.back.push_back(w.folder);
        w.forward.clear();
    }
    w.folder = folder;
    w.selected = -1;
    w.scroll = 0.0;
    cue(web::Sound::Navigate);
}

void XpDesktop::openItem(int node, Win* from) {
    const int target = fs_.resolve(node);
    const XpNode& n = fs_.node(target);
    switch (n.open) {
        case XpOpen::Browse:
            // XP opens a folder in the window it was opened from, and a new
            // window only from the desktop.
            if (from != nullptr && from->kind == Kind::Explorer) navigate(*from, target);
            else openExplorer(target);
            break;
        case XpOpen::Notepad: openViewer(Kind::Notepad, target); break;
        case XpOpen::Picture: openViewer(Kind::Picture, target); break;
        case XpOpen::Crash: crashMessage(n.name); break;
        case XpOpen::Message: {
            const Glyph glyph = n.icon == XpIcon::Florr   ? Glyph::Warning
                                : n.icon == XpIcon::Globe ? Glyph::Info
                                                          : Glyph::Error;
            message(n.caption.empty() ? n.name : n.caption, n.text, glyph);
            break;
        }
    }
}

void XpDesktop::askDelete(int node) {
    const XpNode& n = fs_.node(node);
    if (!fs_.canDelete(node)) {
        message("Error Deleting File or Folder",
                "Cannot delete " + n.name +
                    ": Access is denied.\n\nMake sure the disk is not full or write-protected and "
                    "that the file is not currently in use.",
                Glyph::Error);
        return;
    }
    message("Confirm File Delete", "Are you sure you want to delete '" + n.name + "'?", Glyph::Bin,
            true, node);
}

void XpDesktop::askEmptyRecycleBin() {
    if (fs_.listing(fs_.recycleBin()).empty()) return;
    message("Confirm Multiple File Delete",
            "Are you sure you want to delete all of the items in the Recycle Bin?", Glyph::Bin, true,
            fs_.recycleBin());
}

void XpDesktop::afterDelete(int corruptionBefore) {
    if (deskSelected_ >= 0 && fs_.node(deskSelected_).deleted) deskSelected_ = -1;
    for (Win& w : windows_) {
        if (w.selected >= 0 && fs_.node(w.selected).deleted) w.selected = -1;
    }
    // A document that went with the bin takes its window with it.
    std::vector<int> gone;
    for (const Win& w : windows_) {
        if (w.file >= 0 && fs_.node(w.file).deleted) gone.push_back(w.id);
    }
    for (const int id : gone) closeWindow(id);

    const int left = fs_.corruptionLeft();
    if (left == corruptionBefore) return;
    if (left == 0) {
        doneAt_ = now_;
        showBalloon("All corruption.exe files deleted", "Returning you to your normal computer...", 0.0,
                    web::Sound::AllClear);
        return;
    }
    showBalloon("corruption.exe deleted",
                left == 1 ? std::string("1 corruption.exe file left.")
                          : std::to_string(left) + " corruption.exe files left.",
                kProgressBalloonSeconds);
}

void XpDesktop::showBalloon(const std::string& title, const std::string& body, double lasts,
                            web::Sound sound) {
    balloon_.title = title;
    balloon_.body = body;
    balloon_.shownAt = now_;
    balloon_.lasts = lasts;
    balloon_.visible = true;
    cue(sound);
}

void XpDesktop::showQuestBalloon() {
    if (doneAt_ >= 0.0) return;
    std::string body = kQuestBody;
    // Asked for again from the tray, it says how far along the hunt is.
    if (questShown_) {
        const int left = fs_.corruptionLeft();
        body += "\n\n" + std::to_string(left) + " of " + std::to_string(fs_.corruptionTotal()) +
                " left.";
    }
    showBalloon(kQuestTitle, body, 0.0);
}

void XpDesktop::startItem(int action) {
    switch (static_cast<StartAction>(action)) {
        case StartAction::MyDocuments: openExplorer(fs_.myDocuments()); break;
        case StartAction::MyPictures: openExplorer(fs_.myPictures()); break;
        case StartAction::MyMusic: openExplorer(fs_.myMusic()); break;
        case StartAction::MyComputer: openExplorer(fs_.myComputer()); break;
        case StartAction::Explorer: openExplorer(fs_.myDocuments()); break;
        case StartAction::Notepad: openViewer(Kind::Notepad, -1); break;
        case StartAction::Internet:
            openItem(fs_.find("Internet Explorer"), nullptr);
            break;
        case StartAction::Florr: openItem(fs_.find("My Computer/Local Disk (C:)/Program Files/florr/florr.exe"), nullptr); break;
        case StartAction::Corruption: crashMessage("corruption.exe"); break;
        case StartAction::Restricted: restrictionMessage(); break;
        case StartAction::LogOff:
        case StartAction::TurnOff:
            message("Windows", "Windows cannot shut down while corruption.exe is still on this computer.",
                    Glyph::Warning);
            break;
        case StartAction::None: break;
    }
}

bool XpDesktop::doubleClick(int window, int item, double now) {
    const bool twice = window == lastClickWindow_ && item == lastClickItem_ &&
                       now - lastClickAt_ <= kDoubleClickSeconds;
    lastClickWindow_ = window;
    lastClickItem_ = item;
    // A third click starts a fresh pair rather than opening it again.
    lastClickAt_ = twice ? -10.0 : now;
    return twice;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void XpDesktop::update(Window& window, double now) {
    XpInput in;
    in.mouse = {window.mouseX(), window.mouseY()};
    in.leftPressed = window.mousePressed(MouseButton::Left);
    in.leftReleased = window.mouseReleased(MouseButton::Left);
    in.leftDown = window.mouseDown(MouseButton::Left);
    in.rightPressed = window.mousePressed(MouseButton::Right);
    in.wheel = window.wheelDelta();
    in.deleteKey = window.keyPressed(Key::Delete);
    in.enter = window.keyPressed(Key::Enter);
    in.backspace = window.keyPressed(Key::Backspace);
    in.escape = window.keyPressed(Key::Escape);
    update(in, window.width(), window.height(), now);
    if (hand_) window.setCursorShape(CursorShape::Hand);
}

void XpDesktop::update(const XpInput& in, double width, double height, double now) {
    if (!active_) return;
    now_ = now;
    scale_ = std::max(0.25, height / kScreenHeight);
    width_ = width / scale_;
    height_ = height / scale_;
    mouse_ = {in.mouse.x / scale_, in.mouse.y / scale_};
    hand_ = false;

    const double t = now - openedAt_;
    // The startup sound, as the desktop fades in out of the black.
    if (!startupSounded_ && t >= kBlackSeconds) {
        startupSounded_ = true;
        cue(web::Sound::DesktopStartup);
    }
    // Nothing to click on while the screen is still coming up.
    if (t < kBlackSeconds + kFadeSeconds * 0.5) return;
    if (!questShown_ && t >= kQuestDelaySeconds) {
        showQuestBalloon();
        questShown_ = true;
    }
    if (balloon_.visible && balloon_.lasts > 0.0 && now - balloon_.shownAt > balloon_.lasts) {
        balloon_.visible = false;
    }

    if (dragging_ >= 0) {
        if (Win* w = find(dragging_)) {
            Rect next = dragFrame_;
            next.x += mouse_.x - dragFrom_.x;
            next.y += mouse_.y - dragFrom_.y;
            // The title bar never leaves the screen, so a window can always
            // be dragged back.
            next.x = std::clamp(next.x, 60.0 - next.w, width_ - 60.0);
            next.y = std::clamp(next.y, 0.0, height_ - kTaskbarHeight - kTitleHeight);
            w->frame = next;
        }
        if (!in.leftDown) dragging_ = -1;
    }

    if (in.leftPressed) pressLeft(mouse_, now);
    if (in.rightPressed) pressRight(mouse_);
    if (in.leftReleased) releaseLeft(mouse_);

    // The wheel scrolls whichever Explorer is under the pointer.
    if (in.wheel != 0.0f && !modal()) {
        for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
            if (it->minimized || !windowRect(*it).contains(mouse_)) continue;
            if (it->kind == Kind::Explorer) {
                const ExplorerLayout l = explorerLayout(clientRect(windowRect(*it)));
                const int n = static_cast<int>(fs_.listing(it->folder).size());
                it->scroll = std::clamp(it->scroll - in.wheel * kWheelStep, 0.0, maxScroll(l.content, n));
            }
            break;
        }
    }

    keys(in);

    // The hand over the task pane's links, as XP shows it over a link.
    if (!modal() && !startOpen_ && !menu_.open) {
        for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
            if (it->minimized || !windowRect(*it).contains(mouse_)) continue;
            if (it->kind == Kind::Explorer) {
                const ExplorerLayout l = explorerLayout(clientRect(windowRect(*it)));
                if (l.pane.contains(mouse_)) {
                    const PaneGeometry g = paneGeometry(l.pane, panePanels(fs_, it->folder, it->selected));
                    for (const Rect& r : g.links) {
                        hand_ |= r.contains(mouse_);
                    }
                }
            }
            break;
        }
    }
}

void XpDesktop::pressLeft(Vec2 m, double now) {
    press_ = {};
    // An open menu takes the click whether it lands on it or not: XP closes
    // it, and the click goes no further.
    if (menu_.open) {
        const std::vector<MenuEntry> entries = menuEntries(menu_.at);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].enabled && entries[i].rect.contains(m)) {
                press_ = {Press::What::MenuItem, -1, static_cast<int>(i)};
                return;
            }
        }
        if (!contextMenuRect().contains(m)) menu_.open = false;
        return;
    }
    if (startOpen_) {
        if (startMenu().contains(m)) {
            const std::vector<StartEntry> entries = startEntries(startMenu());
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (!entries[i].separator && entries[i].rect.contains(m)) {
                    press_ = {Press::What::StartItem, -1, static_cast<int>(i)};
                    return;
                }
            }
            return;
        }
        startOpen_ = false;
        return;
    }
    // A message box is modal: nothing behind it answers until it is gone.
    if (Win* box = modal()) {
        if (windowRect(*box).contains(m)) windowPress(*box, m, now);
        return;
    }
    if (balloon_.visible && balloonBox_.contains(m)) {
        const Rect close{balloonBox_.right() - 22, balloonBox_.y + 7, 15, 15};
        if (close.contains(m)) press_ = {Press::What::BalloonClose};
        else balloon_.visible = false;
        return;
    }
    if (taskbar().contains(m)) {
        if (startButton().contains(m)) {
            startOpen_ = true;
            return;
        }
        const std::vector<int> ids = taskWindows();
        const std::vector<Rect> rects = taskButtons();
        for (std::size_t i = 0; i < rects.size(); ++i) {
            if (rects[i].contains(m)) {
                press_ = {Press::What::TaskButton, ids[i], static_cast<int>(i)};
                return;
            }
        }
        if (trayIcon().contains(m)) showQuestBalloon();
        return;
    }
    for (int i = static_cast<int>(windows_.size()) - 1; i >= 0; --i) {
        const Win& w = windows_[static_cast<std::size_t>(i)];
        if (w.minimized || !windowRect(w).contains(m)) continue;
        const int id = w.id;
        focus(id);
        windowPress(*find(id), m, now);
        return;
    }
    // The desktop.
    keyTarget_ = 0;
    for (const DeskSlot& slot : deskSlots()) {
        const Rect cell = deskCell(slot.col, slot.row);
        const Rect hitBox{cell.x + 6, cell.y + 2, cell.w - 12, cell.h - 8};
        if (hitBox.contains(m)) {
            deskSelected_ = slot.node;
            if (doubleClick(-1, slot.node, now)) openItem(slot.node, nullptr);
            return;
        }
    }
    deskSelected_ = -1;
}

void XpDesktop::windowPress(Win& w, Vec2 m, double now) {
    const Rect r = windowRect(w);
    keyTarget_ = w.id;
    const CaptionButtons caption = captionButtons(r, w.kind != Kind::Message);
    if (caption.close.contains(m)) {
        press_ = {Press::What::Close, w.id};
        return;
    }
    if (w.kind != Kind::Message) {
        if (caption.maximize.contains(m)) {
            press_ = {Press::What::Maximize, w.id};
            return;
        }
        if (caption.minimize.contains(m)) {
            press_ = {Press::What::Minimize, w.id};
            return;
        }
    }
    const Rect title{r.x, r.y, r.w, kTitleHeight};
    if (title.contains(m)) {
        // Double-clicking a title bar maximizes, or puts the window back.
        if (w.kind != Kind::Message && doubleClick(w.id, -100, now)) {
            w.maximized = !w.maximized;
            return;
        }
        if (!w.maximized) {
            dragging_ = w.id;
            dragFrom_ = m;
            dragFrame_ = w.frame;
        }
        return;
    }
    const Rect client = clientRect(r);
    if (w.kind == Kind::Message) {
        const std::vector<Rect> buttons = dialogButtons(client, w.yesNo);
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            if (buttons[i].contains(m)) press_ = {Press::What::DialogButton, w.id, static_cast<int>(i)};
        }
        return;
    }
    if (w.kind != Kind::Explorer) return;

    const ExplorerLayout l = explorerLayout(client);
    if (l.back.contains(m)) press_ = {Press::What::ToolBack, w.id};
    else if (l.forward.contains(m)) press_ = {Press::What::ToolForward, w.id};
    else if (l.up.contains(m)) press_ = {Press::What::ToolUp, w.id};
    else if (l.pane.contains(m)) {
        const PaneGeometry g = paneGeometry(l.pane, panePanels(fs_, w.folder, w.selected));
        for (std::size_t i = 0; i < g.links.size(); ++i) {
            if (g.links[i].contains(m)) press_ = {Press::What::PaneLink, w.id, static_cast<int>(i)};
        }
    } else if (l.content.contains(m)) {
        const std::vector<int> items = fs_.listing(w.folder);
        const Rect view = tileView(l.content, static_cast<int>(items.size()));
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (view.contains(m) && tileAt(view, static_cast<int>(i), w.scroll).contains(m)) {
                w.selected = items[i];
                if (doubleClick(w.id, items[i], now)) openItem(items[i], &w);
                return;
            }
        }
        w.selected = -1;
    }
}

void XpDesktop::pressRight(Vec2 m) {
    menu_.open = false;
    startOpen_ = false;
    if (modal() || taskbar().contains(m)) return;
    for (int i = static_cast<int>(windows_.size()) - 1; i >= 0; --i) {
        const Win& front = windows_[static_cast<std::size_t>(i)];
        if (front.minimized || !windowRect(front).contains(m)) continue;
        const int id = front.id;
        focus(id);
        Win& w = *find(id);
        if (w.kind != Kind::Explorer) return;
        const ExplorerLayout l = explorerLayout(clientRect(windowRect(w)));
        const std::vector<int> items = fs_.listing(w.folder);
        const Rect view = tileView(l.content, static_cast<int>(items.size()));
        for (std::size_t k = 0; k < items.size(); ++k) {
            if (view.contains(m) && tileAt(view, static_cast<int>(k), w.scroll).contains(m)) {
                w.selected = items[k];
                menu_ = {true, m, w.id, items[k]};
                break;
            }
        }
        return;
    }
    keyTarget_ = 0;
    for (const DeskSlot& slot : deskSlots()) {
        const Rect cell = deskCell(slot.col, slot.row);
        if (Rect{cell.x + 6, cell.y + 2, cell.w - 12, cell.h - 8}.contains(m)) {
            deskSelected_ = slot.node;
            menu_ = {true, m, -1, slot.node};
            return;
        }
    }
}

Rect XpDesktop::pressRect(const Press& p) {
    Win* w = p.window >= 0 ? find(p.window) : nullptr;
    switch (p.what) {
        case Press::What::Close:
        case Press::What::Maximize:
        case Press::What::Minimize: {
            if (w == nullptr) return {};
            const CaptionButtons b = captionButtons(windowRect(*w), w->kind != Kind::Message);
            return p.what == Press::What::Close      ? b.close
                   : p.what == Press::What::Maximize ? b.maximize
                                                     : b.minimize;
        }
        case Press::What::DialogButton: {
            if (w == nullptr) return {};
            const std::vector<Rect> buttons = dialogButtons(clientRect(windowRect(*w)), w->yesNo);
            return p.index < static_cast<int>(buttons.size()) ? buttons[static_cast<std::size_t>(p.index)] : Rect{};
        }
        case Press::What::StartItem: {
            const std::vector<StartEntry> entries = startEntries(startMenu());
            return entries[static_cast<std::size_t>(p.index)].rect;
        }
        case Press::What::MenuItem:
            return menuEntries(menu_.at)[static_cast<std::size_t>(p.index)].rect;
        case Press::What::TaskButton: {
            const std::vector<Rect> rects = taskButtons();
            return p.index < static_cast<int>(rects.size()) ? rects[static_cast<std::size_t>(p.index)] : Rect{};
        }
        case Press::What::ToolBack:
        case Press::What::ToolForward:
        case Press::What::ToolUp: {
            if (w == nullptr) return {};
            const ExplorerLayout l = explorerLayout(clientRect(windowRect(*w)));
            return p.what == Press::What::ToolBack      ? l.back
                   : p.what == Press::What::ToolForward ? l.forward
                                                        : l.up;
        }
        case Press::What::PaneLink: {
            if (w == nullptr) return {};
            const ExplorerLayout l = explorerLayout(clientRect(windowRect(*w)));
            const PaneGeometry g = paneGeometry(l.pane, panePanels(fs_, w->folder, w->selected));
            return p.index < static_cast<int>(g.links.size()) ? g.links[static_cast<std::size_t>(p.index)] : Rect{};
        }
        case Press::What::BalloonClose:
            return {balloonBox_.right() - 22, balloonBox_.y + 7, 15, 15};
        case Press::What::None: break;
    }
    return {};
}

void XpDesktop::releaseLeft(Vec2 m) {
    const Press p = press_;
    press_ = {};
    // A press only counts if it is let go over the thing it went down on.
    if (p.what == Press::What::None || !pressRect(p).contains(m)) return;
    Win* w = p.window >= 0 ? find(p.window) : nullptr;
    switch (p.what) {
        case Press::What::Close:
            if (w != nullptr) closeWindow(w->id);
            break;
        case Press::What::Maximize:
            if (w != nullptr) w->maximized = !w->maximized;
            break;
        case Press::What::Minimize:
            if (w != nullptr) {
                w->minimized = true;
                if (keyTarget_ == w->id) keyTarget_ = 0;
            }
            break;
        case Press::What::DialogButton:
            if (w != nullptr) answer(w->id, p.index);
            break;
        case Press::What::StartItem: {
            const StartAction action = startEntries(startMenu())[static_cast<std::size_t>(p.index)].action;
            startOpen_ = false;
            startItem(static_cast<int>(action));
            break;
        }
        case Press::What::MenuItem: {
            const MenuAction action = menuEntries(menu_.at)[static_cast<std::size_t>(p.index)].action;
            const ContextMenu menu = menu_;
            menu_.open = false;
            if (action == MenuAction::Open) openItem(menu.item, menu.window >= 0 ? find(menu.window) : nullptr);
            else if (action == MenuAction::Delete) askDelete(menu.item);
            break;
        }
        case Press::What::TaskButton:
            if (w != nullptr) {
                // The active window's button minimizes it; any other's brings
                // its window up front.
                if (!w->minimized && activeId() == w->id) {
                    w->minimized = true;
                    keyTarget_ = 0;
                } else {
                    w->minimized = false;
                    focus(w->id);
                }
            }
            break;
        case Press::What::ToolBack:
            if (w != nullptr && !w->back.empty()) {
                w->forward.push_back(w->folder);
                const int folder = w->back.back();
                w->back.pop_back();
                navigate(*w, folder, false);
            }
            break;
        case Press::What::ToolForward:
            if (w != nullptr && !w->forward.empty()) {
                w->back.push_back(w->folder);
                const int folder = w->forward.back();
                w->forward.pop_back();
                navigate(*w, folder, false);
            }
            break;
        case Press::What::ToolUp:
            if (w != nullptr && fs_.node(w->folder).parent >= 0) navigate(*w, fs_.node(w->folder).parent);
            break;
        case Press::What::PaneLink:
            if (w != nullptr) {
                const ExplorerLayout l = explorerLayout(clientRect(windowRect(*w)));
                const PaneGeometry g = paneGeometry(l.pane, panePanels(fs_, w->folder, w->selected));
                const PaneLinkSpec link = g.specs[static_cast<std::size_t>(p.index)];
                switch (link.action) {
                    case PaneAction::Delete: askDelete(link.target); break;
                    case PaneAction::EmptyBin: askEmptyRecycleBin(); break;
                    case PaneAction::Go: navigate(*w, link.target); break;
                    case PaneAction::Restricted: restrictionMessage(); break;
                    case PaneAction::None: break;
                }
            }
            break;
        case Press::What::BalloonClose:
            balloon_.visible = false;
            break;
        case Press::What::None: break;
    }
}

void XpDesktop::answer(int boxId, int button) {
    Win* box = find(boxId);
    if (box == nullptr) return;
    const bool yes = box->yesNo && button == 0;
    const int confirm = box->confirm;
    closeWindow(boxId);
    if (!yes || confirm < 0) return;
    const int before = fs_.corruptionLeft();
    if (confirm == fs_.recycleBin()) fs_.emptyRecycleBin();
    else fs_.remove(confirm);
    // Thrown away -- and when a corruption.exe went with it, the thing inside
    // it dying too.
    cue(fs_.corruptionLeft() < before ? web::Sound::Purge : web::Sound::Recycle);
    afterDelete(before);
}

void XpDesktop::keys(const XpInput& in) {
    if (in.escape) {
        if (menu_.open) menu_.open = false;
        else if (startOpen_) startOpen_ = false;
        else if (Win* box = modal()) answer(box->id, box->yesNo ? 1 : 0);
        return;
    }
    if (Win* box = modal()) {
        if (in.enter) answer(box->id, 0);
        return;
    }
    if (menu_.open || startOpen_) return;

    Win* w = keyTarget_ != 0 ? find(keyTarget_) : nullptr;
    const bool explorer = w != nullptr && w->kind == Kind::Explorer && !w->minimized;
    const int selected = explorer ? w->selected : keyTarget_ == 0 ? deskSelected_ : -1;
    // Delete deletes; so does Backspace with something selected, which is the
    // key a Mac calls delete. With nothing selected Backspace is XP's Back.
    const bool erase = in.deleteKey || (in.backspace && selected >= 0);
    if (erase && selected >= 0) {
        askDelete(selected);
    } else if (in.enter && selected >= 0) {
        openItem(selected, explorer ? w : nullptr);
    } else if (in.backspace && explorer && !w->back.empty()) {
        w->forward.push_back(w->folder);
        const int folder = w->back.back();
        w->back.pop_back();
        navigate(*w, folder, false);
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

namespace {

void drawGlyph(Canvas& c, int glyph, double x, double y, double size) {
    c.save();
    c.translate(f(x), f(y));
    c.scale(f(size / 32.0), f(size / 32.0));
    c.setLineCap("round");
    c.setLineJoin("round");
    switch (glyph) {
        case 0:   // error
            c.beginPath();
            c.arc(16, 16, 14, 0, f(kTau));
            paint(c, 0xD9322Du, 0x8E1512u, 1.2);
            setFill(c, kWhite, 0.3);
            c.beginPath();
            c.ellipse(16, 9.5f, 9, 5, 0, 0, f(kTau));
            c.fill();
            line(c, 10.5, 10.5, 21.5, 21.5, kWhite, 3.2);
            line(c, 21.5, 10.5, 10.5, 21.5, kWhite, 3.2);
            break;
        case 1:   // warning
            poly(c, {{16, 3}, {30, 28}, {2, 28}});
            paint(c, 0xFFD43Bu, 0x9C7A00u, 1.5);
            setFill(c, kBlack);
            c.fillRect(14.6f, 10.5f, 2.8f, 10);
            c.fillCircle(16, 24, 1.7f);
            break;
        case 2:   // question
        case 3: { // information
            c.beginPath();
            c.arc(16, 16, 14, 0, f(kTau));
            paint(c, kWhite, 0x3B6FCFu, 1.5);
            c.setFont("bold 20px Tahoma, sans-serif");
            c.setTextAlign("center");
            c.setTextBaseline("middle");
            setFill(c, 0x2C5BC2u);
            c.fillText(glyph == 2 ? "?" : "i", 16, 17);
            break;
        }
        default:  // the Recycle Bin, over a delete
            drawIcon(c, XpIcon::RecycleBin, 0, 0, 32, true);
            break;
    }
    c.restore();
}

/// XP's push button: a pale face, a dark rounded edge, an orange glow when
/// the pointer is over it and a gold one on the default.
void xpButton(Canvas& c, Rect r, const std::string& label, bool hover, bool down, bool isDefault) {
    c.save();
    clipRound(c, r, 3);
    bands(c, r, down ? kButtonDown : kButtonFace);
    c.restore();
    if (hover || isDefault) {
        strokeRound(c, {r.x + 1, r.y + 1, r.w - 2, r.h - 2}, 2.5, hover ? 0xF8B330u : 0xE5C04Au, 2.0);
    }
    strokeRound(c, r, 3, kButtonEdge);
    write(c, label, r.x + r.w * 0.5, r.y + r.h * 0.5 + 0.5, kUi, kBlack, "center");
}

void separatorLine(Canvas& c, double x, double y, double w) {
    fillRect(c, {x, y, w, 1}, kFaceLine);
    fillRect(c, {x, y + 1, w, 1}, kWhite);
}

/// A green circle with a white arrow in it: Back, Forward, Go.
void arrowButton(Canvas& c, double cx, double cy, double r, bool pointsLeft, bool enabled) {
    c.beginPath();
    c.arc(f(cx), f(cy), f(r), 0, f(kTau));
    paint(c, enabled ? 0x3DA834u : 0xC9C6B8u, enabled ? 0x1F6A1Au : 0x9E9B8Eu);
    const double s = pointsLeft ? -1.0 : 1.0;
    c.beginPath();
    c.moveTo(f(cx + s * r * 0.5), f(cy));
    c.lineTo(f(cx - s * r * 0.15), f(cy - r * 0.5));
    c.lineTo(f(cx - s * r * 0.15), f(cy - r * 0.2));
    c.lineTo(f(cx - s * r * 0.55), f(cy - r * 0.2));
    c.lineTo(f(cx - s * r * 0.55), f(cy + r * 0.2));
    c.lineTo(f(cx - s * r * 0.15), f(cy + r * 0.2));
    c.lineTo(f(cx - s * r * 0.15), f(cy + r * 0.5));
    c.closePath();
    setFill(c, kWhite);
    c.fill();
}

void menuBar(Canvas& c, Rect bar, std::initializer_list<const char*> items) {
    fillRect(c, bar, kFace);
    fillRect(c, {bar.x, bar.bottom() - 1, bar.w, 1}, kFaceLine);
    double x = bar.x + 8;
    for (const char* item : items) {
        write(c, item, x, bar.y + bar.h * 0.5, kUi, kBlack);
        x += measure(c, item, kUi) + 14;
    }
}

} // namespace

void XpDesktop::draw(Canvas& c, double now, const XpPainters& painters) {
    if (!active_) return;
    now_ = now;
    scale_ = std::max(0.25, c.height() / kScreenHeight);
    width_ = c.width() / scale_;
    height_ = c.height() / scale_;
    const double t = now - openedAt_;

    c.save();
    c.scale(f(scale_), f(scale_));
    if (t < kBlackSeconds) {
        fillRect(c, {0, 0, width_, height_}, kBlack);
        c.restore();
        return;
    }
    if (painters.wallpaper) painters.wallpaper(c, width_, height_);
    drawDesktopIcons(c);
    const int active = activeId();
    for (Win& w : windows_) {
        if (!w.minimized) drawWindow(c, w, w.id == active, now, painters);
    }
    drawTaskbar(c, painters);
    if (startOpen_) drawStartMenu(c, painters);
    drawBalloon(c);
    if (menu_.open) drawContextMenu(c);
    if (t < kBlackSeconds + kFadeSeconds) {
        fillRect(c, {0, 0, width_, height_}, kBlack, 1.0 - (t - kBlackSeconds) / kFadeSeconds);
    }
    c.restore();
}

void XpDesktop::drawDesktopIcons(Canvas& c) {
    const bool binFull = !fs_.listing(fs_.recycleBin()).empty();
    for (const DeskSlot& slot : deskSlots()) {
        const Rect cell = deskCell(slot.col, slot.row);
        const XpNode& n = fs_.node(slot.node);
        const bool selected = slot.node == deskSelected_;
        const bool full = slot.node == fs_.recycleBin() && binFull;
        const double ix = cell.x + (cell.w - 32) * 0.5;
        if (selected) drawSelectedIcon(c, n.icon, ix, cell.y + 4, 32, full);
        else drawIcon(c, n.icon, ix, cell.y + 4, 32, full);

        std::vector<std::string> lines = wrap(c, n.name, kUi, cell.w - 6);
        if (!selected && lines.size() > 2) {
            lines.resize(2);
            lines[1] = fit(c, lines[1] + "...", kUi, cell.w - 6);
        }
        const double cx = cell.x + cell.w * 0.5;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const double y = cell.y + 46 + static_cast<double>(i) * 13;
            if (selected) {
                const double w = measure(c, lines[i], kUi) + 4;
                fillRect(c, {cx - w * 0.5, y - 7, w, 13}, kSelect);
                write(c, lines[i], cx, y, kUi, kWhite, "center");
            } else {
                shadowed(c, lines[i], cx, y, kUi, kWhite, kBlack, "center");
            }
        }
    }
}

void XpDesktop::layoutMessage(Canvas& c, Win& w) {
    const std::vector<std::string> lines = wrap(c, w.message, kUi, 400);
    double textWidth = 0;
    for (const std::string& l : lines) textWidth = std::max(textWidth, measure(c, l, kUi));
    const double textHeight = static_cast<double>(lines.size()) * 13.0;
    const double buttons = w.yesNo ? kDialogButtonWidth * 2 + 6 : kDialogButtonWidth;
    const double clientW = std::max({56 + textWidth + 20, buttons + 24, measure(c, w.caption, kTitleFont) + 50, 260.0});
    const double clientH = 16 + std::max(32.0, textHeight) + 18 + kDialogButtonHeight + 12;
    const double fw = clientW + kBorder * 2;
    const double fh = clientH + kTitleHeight + kBorder;
    w.frame = {std::floor((width_ - fw) * 0.5), std::floor((height_ - kTaskbarHeight - fh) * 0.5), fw, fh};
    w.laidOut = true;
}

void XpDesktop::drawWindow(Canvas& c, Win& w, bool active, double now, const XpPainters& painters) {
    if (w.kind == Kind::Message && !w.laidOut) layoutMessage(c, w);
    const Rect r = windowRect(w);
    const double radius = w.maximized ? 0.0 : kCornerRadius;

    // XP's drop shadow, faint, down and to the right.
    if (!w.maximized) fillRect(c, {r.x + 3, r.y + 4, r.w, r.h}, kBlack, 0.12);

    c.save();
    clipRoundTop(c, r, std::max(radius, 0.01));
    fillRect(c, r, active ? kFrameActive : kFrameInactive);
    bands(c, {r.x, r.y, r.w, kTitleHeight}, active ? kTitleActive : kTitleInactive);
    c.restore();
    // The dark edge round it all, rounded at the top only.
    c.beginPath();
    c.moveTo(f(r.x + 0.5), f(r.bottom() - 0.5));
    c.lineTo(f(r.x + 0.5), f(r.y + radius));
    if (radius > 0) c.arcTo(f(r.x + 0.5), f(r.y + 0.5), f(r.x + radius), f(r.y + 0.5), f(radius));
    c.lineTo(f(r.right() - radius), f(r.y + 0.5));
    if (radius > 0) c.arcTo(f(r.right() - 0.5), f(r.y + 0.5), f(r.right() - 0.5), f(r.y + radius), f(radius));
    c.lineTo(f(r.right() - 0.5), f(r.bottom() - 0.5));
    c.closePath();
    setStroke(c, active ? kFrameEdge : 0xB8A866u);
    c.setLineWidth(1.0f);
    c.stroke();

    // The title: an icon, then the caption in white.
    const std::string title = titleOf(w);
    const XpIcon icon = iconOf(w);
    const bool full = w.kind != Kind::Message;
    const CaptionButtons caption = captionButtons(r, full);
    const double textX = full ? r.x + 28 : r.x + 9;
    const double textRoom = (full ? caption.minimize.x : caption.close.x) - textX - 6;
    if (full) drawIcon(c, icon, r.x + 7, r.y + 7, 16);
    shadowed(c, fit(c, title, kTitleFont, textRoom), textX, r.y + kTitleHeight * 0.5 + 1, kTitleFont,
             kWhite, active ? kInkOnYellow : 0xB8A866u);

    // The caption buttons: yellow minimize and maximize, XP's red close.
    const auto captionButton = [&](Rect b, int kind) {
        const bool hover = b.contains(mouse_);
        c.save();
        if (!active) c.setGlobalAlpha(0.6f);
        clipRound(c, b, 3);
        bands(c, b, kind == 0 ? kCloseButton : kCaptionButton);
        if (hover) fillRect(c, b, kWhite, 0.18);
        c.restore();
        strokeRound(c, b, 3, kWhite, 1.0, active ? 1.0 : 0.6);
        const double cx = b.x + b.w * 0.5;
        const double cy = b.y + b.h * 0.5;
        const std::uint32_t glyph = kWhite;
        const std::uint32_t shade = kind == 0 ? 0x8E2A14u : kInkOnYellow;
        if (kind == 0) {
            line(c, cx - 4 + 0.6, cy - 4 + 0.6, cx + 4 + 0.6, cy + 4 + 0.6, shade, 2.2);
            line(c, cx + 4 + 0.6, cy - 4 + 0.6, cx - 4 + 0.6, cy + 4 + 0.6, shade, 2.2);
            line(c, cx - 4, cy - 4, cx + 4, cy + 4, glyph, 2.2);
            line(c, cx + 4, cy - 4, cx - 4, cy + 4, glyph, 2.2);
        } else if (kind == 1) {
            const Rect box{cx - 5, cy - 5, 10, 10};
            outline(c, {box.x + 1, box.y + 1, box.w, box.h}, shade);
            fillRect(c, {box.x + 1, box.y + 1, box.w, 2.5}, shade);
            outline(c, box, glyph);
            fillRect(c, {box.x, box.y, box.w, 2.5}, glyph);
        } else {
            fillRect(c, {cx - 4 + 1, cy + 3 + 1, 7, 2.5}, shade);
            fillRect(c, {cx - 4, cy + 3, 7, 2.5}, glyph);
        }
    };
    captionButton(caption.close, 0);
    if (full) {
        captionButton(caption.maximize, 1);
        captionButton(caption.minimize, 2);
    }

    const Rect client = clientRect(r);
    switch (w.kind) {
        case Kind::Explorer: drawExplorer(c, w, client, active); break;
        case Kind::Notepad: drawNotepad(c, w, client); break;
        case Kind::Picture: drawPicture(c, w, client, now, painters); break;
        case Kind::Message: drawMessage(c, w, client); break;
    }
}

void XpDesktop::drawExplorer(Canvas& c, const Win& w, Rect client, bool active) {
    const ExplorerLayout l = explorerLayout(client);
    menuBar(c, l.menuBar, {"File", "Edit", "View", "Favorites", "Tools", "Help"});

    // -- the toolbar --
    fillRect(c, l.toolbar, kFace);
    separatorLine(c, l.toolbar.x, l.toolbar.bottom() - 2, l.toolbar.w);
    const auto raised = [&](Rect b, bool enabled) {
        if (!enabled || !b.contains(mouse_) || modal()) return;
        c.save();
        clipRound(c, b, 3);
        bands(c, b, kButtonFace);
        c.restore();
        strokeRound(c, b, 3, 0xCEC9B5u);
    };
    const bool canBack = !w.back.empty();
    const bool canForward = !w.forward.empty();
    const bool canUp = fs_.node(w.folder).parent >= 0;
    raised(l.back, canBack);
    raised(l.forward, canForward);
    raised(l.up, canUp);
    arrowButton(c, l.back.x + 15, l.back.y + 16, 11, true, canBack);
    write(c, "Back", l.back.x + 31, l.back.y + 16, kUi, canBack ? kBlack : kDisabled);
    poly(c, {{l.back.right() - 9, l.back.y + 15}, {l.back.right() - 4, l.back.y + 15}, {l.back.right() - 6.5, l.back.y + 18}});
    setFill(c, canBack ? kBlack : kDisabled);
    c.fill();
    arrowButton(c, l.forward.x + 15, l.forward.y + 16, 11, false, canForward);
    drawIcon(c, XpIcon::Folder, l.up.x + 6, l.up.y + 5, 24);
    c.beginPath();
    const double ux = l.up.x + 24;
    const double uy = l.up.y + 10;
    c.moveTo(f(ux), f(uy - 6));
    c.lineTo(f(ux + 6), f(uy));
    c.lineTo(f(ux + 2.5), f(uy));
    c.lineTo(f(ux + 2.5), f(uy + 6));
    c.lineTo(f(ux - 2.5), f(uy + 6));
    c.lineTo(f(ux - 2.5), f(uy));
    c.lineTo(f(ux - 6), f(uy));
    c.closePath();
    paint(c, canUp ? 0x3DA834u : 0xC9C6B8u, canUp ? 0x1F6A1Au : 0x9E9B8Eu, 0.8);
    double tx = l.up.right() + 8;
    fillRect(c, {tx, l.toolbar.y + 6, 1, l.toolbar.h - 12}, kFaceLine);
    tx += 8;
    // Search and Folders are there, as they are on every XP window.
    c.beginPath();
    c.arc(f(tx + 10), f(l.toolbar.y + 17), 6.5f, 0, f(kTau));
    paint(c, 0xDDEEFFu, 0x5A5A5Au, 2.0);
    line(c, tx + 15, l.toolbar.y + 22, tx + 20, l.toolbar.y + 27, 0x7A4E1Cu, 3.0);
    write(c, "Search", tx + 25, l.toolbar.y + 19, kUi, kBlack);
    tx += 25 + measure(c, "Search", kUi) + 14;
    drawIcon(c, XpIcon::Folder, tx, l.toolbar.y + 7, 22);
    write(c, "Folders", tx + 26, l.toolbar.y + 19, kUi, kBlack);
    tx += 26 + measure(c, "Folders", kUi) + 10;
    fillRect(c, {tx, l.toolbar.y + 6, 1, l.toolbar.h - 12}, kFaceLine);
    tx += 10;
    for (int gy = 0; gy < 2; ++gy) {
        for (int gx = 0; gx < 2; ++gx) {
            fillRect(c, {tx + gx * 9, l.toolbar.y + 9 + gy * 9, 7, 7}, 0x4F7FD0u);
        }
    }

    // -- the address bar --
    fillRect(c, l.address, kFace);
    separatorLine(c, l.address.x, l.address.bottom() - 2, l.address.w);
    write(c, "Address", l.address.x + 6, l.address.y + 12, kUi, kGreyText);
    const double fieldX = l.address.x + 58;
    const Rect go{l.address.right() - 46, l.address.y + 2, 42, 20};
    const Rect field{fieldX, l.address.y + 2, go.x - 6 - fieldX, 20};
    fillRect(c, field, kWhite);
    outline(c, field, kFieldEdge);
    drawIcon(c, fs_.node(w.folder).icon, field.x + 3, field.y + 2, 16);
    write(c, fit(c, fs_.addressOf(w.folder), kUi, field.w - 46), field.x + 24, field.y + 10.5, kUi, kBlack);
    const Rect drop{field.right() - 17, field.y + 1, 16, field.h - 2};
    c.save();
    clipRound(c, drop, 2);
    bands(c, drop, kCaptionButton);
    c.restore();
    poly(c, {{drop.x + 4.5, drop.y + 7}, {drop.right() - 4.5, drop.y + 7}, {drop.x + drop.w * 0.5, drop.y + 11.5}});
    setFill(c, kInkOnYellow);
    c.fill();
    arrowButton(c, go.x + 10, go.y + 10, 8, false, true);
    write(c, "Go", go.x + 22, go.y + 10.5, kUi, kBlack);

    // -- the task pane --
    if (l.pane.w > 0) {
        bands(c, l.pane, kPane);
        const std::vector<PanePanelSpec> panels = panePanels(fs_, w.folder, w.selected);
        const PaneGeometry g = paneGeometry(l.pane, panels);
        c.save();
        c.beginPath();
        c.rect(f(l.pane.x), f(l.pane.y), f(l.pane.w), f(l.pane.h));
        c.clip();
        std::size_t link = 0;
        for (std::size_t i = 0; i < panels.size(); ++i) {
            const Rect header = g.headers[i];
            const Rect body = g.bodies[i];
            c.save();
            clipRoundTop(c, header, 4);
            bands(c, header, kPaneHeader);
            c.restore();
            write(c, panels[i].title, header.x + 12, header.y + header.h * 0.5, kUiBold, kPaneTitle);
            // The roll-up chevron in its white circle.
            const double cx = header.right() - 14;
            const double cy = header.y + header.h * 0.5;
            c.beginPath();
            c.arc(f(cx), f(cy), 8, 0, f(kTau));
            paint(c, kWhite, 0xD8C27Au);
            for (int k = 0; k < 2; ++k) {
                c.beginPath();
                c.moveTo(f(cx - 3), f(cy + 1 - k * 4));
                c.lineTo(f(cx), f(cy - 2 - k * 4 + 2));
                c.lineTo(f(cx + 3), f(cy + 1 - k * 4));
                setStroke(c, kPaneTitle);
                c.setLineWidth(1.3f);
                c.stroke();
            }
            fillRect(c, body, kPaneBody);
            outline(c, body, kWhite);
            if (panels[i].links.empty()) {
                double dy = body.y + 15;
                for (std::size_t d = 0; d < panels[i].details.size(); ++d) {
                    write(c, fit(c, panels[i].details[d], d == 0 ? kUiBold : kUi, body.w - 20),
                          body.x + 10, dy, d == 0 ? kUiBold : kUi, kBlack);
                    dy += 14;
                }
            }
            for (const PaneLinkSpec& spec : panels[i].links) {
                const Rect r = g.links[link++];
                const bool hover = r.contains(mouse_) && !modal();
                if (spec.action == PaneAction::Delete) {
                    line(c, r.x + 3, r.y + 4, r.x + 13, r.y + 14, 0xD9322Du, 2.4);
                    line(c, r.x + 13, r.y + 4, r.x + 3, r.y + 14, 0xD9322Du, 2.4);
                } else {
                    drawIcon(c, spec.icon, r.x, r.y + 1, 16, spec.icon == XpIcon::RecycleBin);
                }
                const std::uint32_t ink = hover ? 0x9A7A10u : kPaneLink;
                const std::string label = fit(c, spec.label, kUi, r.w - 22);
                write(c, label, r.x + 22, r.y + 9, kUi, ink);
                if (hover) fillRect(c, {r.x + 22, r.y + 15, measure(c, label, kUi), 1}, ink);
            }
        }
        c.restore();
    }

    // -- the files --
    fillRect(c, l.content, kWhite);
    const std::vector<int> items = fs_.listing(w.folder);
    const int n = static_cast<int>(items.size());
    const Rect view = tileView(l.content, n);
    const bool binFull = !fs_.listing(fs_.recycleBin()).empty();
    c.save();
    c.beginPath();
    c.rect(f(view.x), f(view.y), f(view.w), f(view.h));
    c.clip();
    for (int i = 0; i < n; ++i) {
        const Rect tile = tileAt(view, i, w.scroll);
        if (tile.bottom() < view.y || tile.y > view.bottom()) continue;
        const XpNode& node = fs_.node(items[static_cast<std::size_t>(i)]);
        const bool selected = items[static_cast<std::size_t>(i)] == w.selected;
        const bool full = items[static_cast<std::size_t>(i)] == fs_.recycleBin() && binFull;
        if (selected && active) drawSelectedIcon(c, node.icon, tile.x + 4, tile.y + 5, 42, full);
        else drawIcon(c, node.icon, tile.x + 4, tile.y + 5, 42, full);
        const double tx2 = tile.x + 54;
        const std::string name = fit(c, node.name, kUi, tile.w - 58);
        if (selected) {
            fillRect(c, {tx2 - 2, tile.y + 9, measure(c, name, kUi) + 4, 14}, active ? kSelect : 0xD4D0C8u);
        }
        write(c, name, tx2, tile.y + 16, kUi, selected && active ? kWhite : kBlack);
        write(c, fit(c, node.type, kUi, tile.w - 58), tx2, tile.y + 30, kUi, kGreyText);
        if (!node.size.empty()) write(c, node.size, tx2, tile.y + 43, kUi, kGreyText);
    }
    c.restore();

    // -- the scrollbar, when the files overflow --
    if (view.w < l.content.w) {
        const Rect bar{view.right(), l.content.y, kScrollbarWidth, l.content.h};
        fillRect(c, bar, 0xFBF9F0u);
        const Rect upArrow{bar.x, bar.y, bar.w, kScrollbarWidth};
        const Rect downArrow{bar.x, bar.bottom() - kScrollbarWidth, bar.w, kScrollbarWidth};
        for (const Rect& a : {upArrow, downArrow}) {
            const Rect inset{a.x + 1, a.y + 1, a.w - 2, a.h - 2};
            c.save();
            clipRound(c, inset, 2);
            bands(c, inset, kScrollThumb);
            c.restore();
            strokeRound(c, inset, 2, 0xD2BC62u);
        }
        poly(c, {{upArrow.x + 5, upArrow.y + 10}, {upArrow.right() - 5, upArrow.y + 10}, {upArrow.x + 8.5, upArrow.y + 6}});
        setFill(c, kInkOnYellow);
        c.fill();
        poly(c, {{downArrow.x + 5, downArrow.y + 7}, {downArrow.right() - 5, downArrow.y + 7}, {downArrow.x + 8.5, downArrow.y + 11}});
        c.fill();
        const double track = bar.h - kScrollbarWidth * 2;
        const double total = tilesHeight(view.w, n);
        const double thumbH = std::max(20.0, track * view.h / total);
        const double range = std::max(1.0, total - view.h);
        const double thumbY = bar.y + kScrollbarWidth + (track - thumbH) * std::clamp(w.scroll / range, 0.0, 1.0);
        const Rect thumb{bar.x + 1, thumbY, bar.w - 2, thumbH};
        c.save();
        clipRound(c, thumb, 3);
        bands(c, thumb, kScrollThumb);
        c.restore();
        strokeRound(c, thumb, 3, 0xD2BC62u);
        for (int k = -1; k <= 1; ++k) fillRect(c, {thumb.x + 4, thumb.y + thumb.h * 0.5 + k * 3, thumb.w - 8, 1}, 0xC9A62Eu);
    }
}

void XpDesktop::drawNotepad(Canvas& c, const Win& w, Rect client) {
    const Rect bar{client.x, client.y, client.w, 20};
    menuBar(c, bar, {"File", "Edit", "Format", "View", "Help"});
    const Rect area{client.x, bar.bottom(), client.w, client.h - bar.h};
    fillRect(c, area, kWhite);
    outline(c, area, kFieldEdge);
    if (w.file < 0) return;
    c.save();
    c.beginPath();
    c.rect(f(area.x + 1), f(area.y + 1), f(area.w - 2), f(area.h - 2));
    c.clip();
    const std::string& text = fs_.node(w.file).text;
    double y = area.y + 12;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        write(c, text.substr(start, end - start), area.x + 5, y, kMono, kBlack);
        y += 16;
        start = end + 1;
    }
    c.restore();
}

void XpDesktop::drawPicture(Canvas& c, const Win& w, Rect client, double now, const XpPainters& painters) {
    (void)now;
    fillRect(c, client, kWhite);
    const Rect toolbar{client.x, client.bottom() - 40, client.w, 40};
    fillRect(c, toolbar, 0xF3F1E7u);
    fillRect(c, {toolbar.x, toolbar.y, toolbar.w, 1}, kFaceLine);
    // The viewer's round buttons along the bottom: the arrows, the zooms, the
    // turns, delete, print, save.
    constexpr int kButtons = 9;
    const double spacing = 34.0;
    const double x0 = toolbar.x + (toolbar.w - spacing * kButtons) * 0.5 + spacing * 0.5;
    for (int i = 0; i < kButtons; ++i) {
        const double cx = x0 + i * spacing;
        const double cy = toolbar.y + 20;
        c.beginPath();
        c.arc(f(cx), f(cy), 12, 0, f(kTau));
        paint(c, 0xFDFCF8u, 0xB7B2A0u);
        if (i < 2) arrowButton(c, cx, cy, 8, i == 0, true);
        else if (i == 6) {
            line(c, cx - 4, cy - 4, cx + 4, cy + 4, 0xD9322Du, 2.4);
            line(c, cx + 4, cy - 4, cx - 4, cy + 4, 0xD9322Du, 2.4);
        } else {
            c.beginPath();
            c.arc(f(cx), f(cy), 5, 0, f(kTau));
            setStroke(c, 0x4F7FD0u);
            c.setLineWidth(2.0f);
            c.stroke();
        }
    }

    const Rect stage{client.x, client.y, client.w, toolbar.y - client.y};
    const double side = std::max(40.0, std::min(stage.w, stage.h) - 40);
    const Rect photo{stage.x + (stage.w - side) * 0.5, stage.y + (stage.h - side) * 0.5, side, side};
    c.save();
    c.beginPath();
    c.rect(f(photo.x), f(photo.y), f(photo.w), f(photo.h));
    c.clip();
    const std::string& name = fs_.node(w.file).name;
    if (name == "hel.bmp" && painters.wallpaper) {
        // The wallpaper itself, at half size.
        c.translate(f(photo.x), f(photo.y));
        c.scale(0.5f, 0.5f);
        painters.wallpaper(c, photo.w * 2, photo.h * 2);
    } else {
        fillRect(c, photo, name == "garden.bmp" ? 0x1EA761u : 0x8F3838u);
        if (painters.flower) painters.flower(c, photo.x + side * 0.5, photo.y + side * 0.5, side * 0.28, true);
    }
    c.restore();
    outline(c, photo, 0x9E9B8Eu);
}

void XpDesktop::drawMessage(Canvas& c, const Win& w, Rect client) {
    fillRect(c, client, kFace);
    drawGlyph(c, static_cast<int>(w.glyph), client.x + 12, client.y + 14, 32);
    const std::vector<std::string> lines = wrap(c, w.message, kUi, 400);
    double y = client.y + 22;
    for (const std::string& l : lines) {
        write(c, l, client.x + 56, y, kUi, kBlack);
        y += 13;
    }
    const std::vector<Rect> buttons = dialogButtons(client, w.yesNo);
    const bool pressing = press_.what == Press::What::DialogButton && press_.window == w.id;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const bool hover = buttons[i].contains(mouse_);
        const char* label = w.yesNo ? (i == 0 ? "Yes" : "No") : "OK";
        xpButton(c, buttons[i], label, hover, hover && pressing && press_.index == static_cast<int>(i), i == 0);
    }
}

void XpDesktop::drawTaskbar(Canvas& c, const XpPainters& painters) {
    const Rect bar = taskbar();
    bands(c, bar, kTaskbar);

    // -- start --
    const Rect start = startButton();
    c.save();
    c.beginPath();
    c.rect(f(start.x), f(start.y), f(start.w), f(start.h));
    c.clip();
    c.beginPath();
    c.roundRect(f(start.x - 14), f(start.y), f(start.w + 14), f(start.h), 12);
    c.clip();
    bands(c, start, startOpen_ ? kStartDown : kStart);
    if (!startOpen_ && start.contains(mouse_)) fillRect(c, start, kWhite, 0.12);
    c.restore();
    // A flower where Windows put its flag.
    if (painters.flower) painters.flower(c, start.x + 21, start.y + start.h * 0.5, 9, false);
    shadowed(c, "start", start.x + 36, start.y + start.h * 0.5 + 1, kStartFont, kWhite, 0x1E5E1Au);

    // -- the open windows --
    const std::vector<int> ids = taskWindows();
    const std::vector<Rect> rects = taskButtons();
    const int active = activeId();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const Win* w = nullptr;
        for (const Win& candidate : windows_) {
            if (candidate.id == ids[i]) w = &candidate;
        }
        if (w == nullptr) continue;
        const Rect b = rects[i];
        const bool down = w->id == active;
        c.save();
        clipRound(c, b, 3);
        bands(c, b, down ? kTaskButtonDown : kTaskButton);
        if (!down && b.contains(mouse_)) fillRect(c, b, kWhite, 0.2);
        c.restore();
        strokeRound(c, b, 3, down ? 0xA9861Au : 0xC9A62Eu);
        drawIcon(c, iconOf(*w), b.x + 6, b.y + 4, 16);
        shadowed(c, fit(c, titleOf(*w), kUi, b.w - 32), b.x + 26, b.y + b.h * 0.5 + 0.5, kUi, kWhite, kInkOnYellow);
    }

    // -- the tray --
    const Rect t = tray();
    bands(c, t, kTray);
    fillRect(c, {t.x, t.y, 1, t.h}, 0xC9A62Eu);
    fillRect(c, {t.x + 1, t.y, 1, t.h}, 0xFFF8D2u);
    drawShield(c, trayIcon().x, trayIcon().y);
    shadowed(c, clockText(), t.right() - 9, t.y + t.h * 0.5 + 0.5, kUi, kWhite, kInkOnYellow, "right");
}

void XpDesktop::drawStartMenu(Canvas& c, const XpPainters& painters) {
    const Rect menu = startMenu();
    fillRect(c, {menu.x + 3, menu.y + 3, menu.w, menu.h}, kBlack, 0.18);
    c.save();
    clipRoundTop(c, menu, 7);
    fillRect(c, menu, kWhite);
    // The header with the user's picture and name.
    const Rect header{menu.x, menu.y, menu.w, kStartHeader};
    bands(c, header, kTitleActive);
    fillRect(c, {menu.x, header.bottom(), menu.w, 2}, kStartLine);
    // The two columns.
    const double half = menu.w * 0.5;
    const double top = header.bottom() + 2;
    const double bottom = menu.bottom() - kStartFooter;
    fillRect(c, {menu.x + half, top, half, bottom - top}, kStartRight);
    fillRect(c, {menu.x + half, top, 1, bottom - top}, 0xF0D88Au);
    // The bar along the bottom.
    bands(c, {menu.x, bottom, menu.w, kStartFooter}, kTaskbar);
    c.restore();
    strokeRound(c, {menu.x, menu.y, menu.w, menu.h + 8}, 7, 0xC9A62Eu);

    const Rect picture{menu.x + 8, menu.y + 8, 48, 48};
    c.beginPath();
    c.roundRect(f(picture.x), f(picture.y), f(picture.w), f(picture.h), 4);
    paint(c, 0xFFF6CFu, kWhite, 2.5);
    if (painters.flower) painters.flower(c, picture.x + 24, picture.y + 24, 17, false);
    shadowed(c, fit(c, user_, kUserFont, menu.w - 80), menu.x + 66, menu.y + 33, kUserFont, kWhite, kInkOnYellow);

    for (const StartEntry& e : startEntries(menu)) {
        if (e.separator) {
            fillRect(c, {e.rect.x, e.rect.y + 4, e.rect.w, 1}, e.side == 1 ? 0xE8D08Au : 0xE4E0D0u);
            continue;
        }
        const bool hover = e.rect.contains(mouse_);
        if (e.side == 2) {
            if (hover) {
                c.save();
                clipRound(c, e.rect, 3);
                fillRect(c, e.rect, kWhite, 0.25);
                c.restore();
            }
            const double cx = e.rect.x + 14;
            const double cy = e.rect.y + 14;
            const Rect square{cx - 11, cy - 11, 22, 22};
            c.beginPath();
            c.roundRect(f(square.x), f(square.y), f(square.w), f(square.h), 4);
            const bool power = e.action == StartAction::TurnOff;
            paint(c, power ? 0xE0472Cu : 0xF4B63Bu, kWhite, 1.2);
            if (power) {
                c.beginPath();
                c.arc(f(cx), f(cy + 1), 5.5f, -1.0f, f(kPi + 1.0));
                setStroke(c, kWhite);
                c.setLineWidth(2.0f);
                c.stroke();
                line(c, cx, cy - 6, cx, cy, kWhite, 2.0);
            } else {
                line(c, cx - 5, cy, cx + 3, cy, kWhite, 2.2);
                line(c, cx + 3, cy, cx, cy - 3, kWhite, 2.2);
                line(c, cx + 3, cy, cx, cy + 3, kWhite, 2.2);
            }
            shadowed(c, e.title, e.rect.x + 30, cy + 0.5, kUi, kWhite, kInkOnYellow);
            continue;
        }
        if (hover) fillRect(c, e.rect, kSelect);
        if (e.title == "All Programs") {
            const std::uint32_t ink = hover ? kWhite : kBlack;
            write(c, e.title, e.rect.x + e.rect.w - 50, e.rect.y + e.rect.h * 0.5, kUiBold, ink, "right");
            const Rect arrow{e.rect.right() - 40, e.rect.y + 6, 18, 18};
            c.beginPath();
            c.roundRect(f(arrow.x), f(arrow.y), f(arrow.w), f(arrow.h), 3);
            paint(c, 0x3DA834u, 0x1F6A1Au);
            poly(c, {{arrow.x + 6, arrow.y + 4}, {arrow.x + 13, arrow.y + 9}, {arrow.x + 6, arrow.y + 14}});
            setFill(c, kWhite);
            c.fill();
            continue;
        }
        const double iconSize = e.side == 0 ? 32 : 24;
        drawIcon(c, e.icon, e.rect.x + 4, e.rect.y + (e.rect.h - iconSize) * 0.5, iconSize);
        const double textX = e.rect.x + iconSize + 10;
        const double room = e.rect.right() - textX - 4;
        const std::uint32_t ink = hover ? kWhite : e.side == 1 ? kStartRightText : kBlack;
        if (!e.sub.empty()) {
            write(c, fit(c, e.title, kUiBold, room), textX, e.rect.y + e.rect.h * 0.5 - 7, kUiBold, ink);
            write(c, fit(c, e.sub, kUi, room), textX, e.rect.y + e.rect.h * 0.5 + 7, kUi, hover ? kWhite : kGreyText);
        } else {
            write(c, fit(c, e.title, e.bold ? kUiBold : kUi, room), textX, e.rect.y + e.rect.h * 0.5,
                  e.bold ? kUiBold : kUi, ink);
        }
    }
}

void XpDesktop::drawBalloon(Canvas& c) {
    balloonBox_ = {};
    if (!balloon_.visible) return;
    constexpr double kBodyWidth = 300.0;
    const std::vector<std::string> titleLines = wrap(c, balloon_.title, kUiBold, kBodyWidth);
    const std::vector<std::string> bodyLines = wrap(c, balloon_.body, kUi, kBodyWidth + 22);
    double textWidth = 0;
    for (const std::string& l : titleLines) textWidth = std::max(textWidth, measure(c, l, kUiBold) + 22);
    for (const std::string& l : bodyLines) textWidth = std::max(textWidth, measure(c, l, kUi));
    const double w = std::max(220.0, textWidth + 20 + 22);
    const double h = 12 + titleLines.size() * 14.0 + 8 + bodyLines.size() * 14.0 + 10;
    const Rect icon = trayIcon();
    const double tipX = icon.x + icon.w * 0.5;
    const double tipY = icon.y - 2;
    const double right = std::min(width_ - 6, tipX + 40);
    const Rect box{right - w, tipY - 18 - h, w, h};
    balloonBox_ = box;

    const auto shape = [&](double dx, double dy) {
        c.beginPath();
        c.roundRect(f(box.x + dx), f(box.y + dy), f(box.w), f(box.h), 8);
        c.moveTo(f(tipX + 18 + dx), f(box.bottom() - 1 + dy));
        c.lineTo(f(tipX + dx), f(tipY + dy));
        c.lineTo(f(tipX + 2 + dx), f(box.bottom() - 1 + dy));
        c.closePath();
    };
    shape(3, 3);
    setFill(c, kBlack, 0.2);
    c.fill();
    shape(0, 0);
    setFill(c, kBalloon);
    c.fill();
    setStroke(c, kBlack);
    c.setLineWidth(1.0f);
    c.stroke();
    // Cover the seam where the tail meets the box.
    fillRect(c, {tipX + 2.5, box.bottom() - 1.5, 15, 2}, kBalloon);

    drawGlyph(c, 1, box.x + 10, box.y + 9, 16);
    double y = box.y + 17;
    for (const std::string& l : titleLines) {
        write(c, l, box.x + 32, y, kUiBold, kBlack);
        y += 14;
    }
    y += 8;
    for (const std::string& l : bodyLines) {
        write(c, l, box.x + 10, y, kUi, kBlack);
        y += 14;
    }
    const Rect close{box.right() - 22, box.y + 7, 15, 15};
    const bool hover = close.contains(mouse_);
    c.beginPath();
    c.roundRect(f(close.x), f(close.y), f(close.w), f(close.h), 2);
    paint(c, hover ? 0xFBE7A0u : kBalloon, hover ? 0xC9A62Eu : 0xB0B0A0u);
    line(c, close.x + 4.5, close.y + 4.5, close.right() - 4.5, close.bottom() - 4.5, 0x555555u, 1.5);
    line(c, close.right() - 4.5, close.y + 4.5, close.x + 4.5, close.bottom() - 4.5, 0x555555u, 1.5);
}

void XpDesktop::drawContextMenu(Canvas& c) {
    const Rect r = contextMenuRect();
    fillRect(c, {r.x + 3, r.y + 3, r.w, r.h}, kBlack, 0.2);
    fillRect(c, r, kWhite);
    outline(c, r, kDisabled);
    for (const MenuEntry& e : menuEntries(menu_.at)) {
        if (e.separator) {
            fillRect(c, {e.rect.x + 2, e.rect.y + 3, e.rect.w - 4, 1}, kDisabled);
            continue;
        }
        const bool hover = e.enabled && e.rect.contains(mouse_);
        if (hover) fillRect(c, e.rect, kSelect);
        const std::uint32_t ink = !e.enabled ? kDisabled : hover ? kWhite : kBlack;
        write(c, e.label, e.rect.x + 18, e.rect.y + e.rect.h * 0.5, e.bold ? kUiBold : kUi, ink);
    }
}

} // namespace flix::ui
