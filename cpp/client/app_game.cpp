// A live game, minus its overlays: what this client sends, and what it does
// when the body it was sending for dies.
//
// The input frame is the only thing a movement key produces -- nothing local
// acts on it, because the drawn flower is the server's position eased (see
// client/interpolation.h) -- and it is produced at the simulation rate rather
// than once per rendered frame, so a 144Hz client is not simulated for six
// times what a 25Hz one is.
//
// Death is the other half. The card is a screen of its own (Screen::Dead)
// rather than a dialog, because everything under it keeps running: the world
// dims but still draws, the HUD and the panels stay at full brightness, and
// Close leaves the player dead with the card gone -- its dim and its Enter
// shortcut with it.

#include "client/app.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "client/render/art_cache.h"
#include "client/ui/draw.h"
#include "client/ui/item_tile.h"
#include "client/ui/menu_style.h"
#include "client/web/audio.h"
#include "shared/game/constants.h"

#ifdef __EMSCRIPTEN__
// The runtime's own abort(), with the stop screen's stop code as its reason:
// the page's onAbort puts it on its status line, and the console gets
// "Aborted(...)". The plain C abort() would hand onAbort an empty reason. It
// throws, so it never returns.
EM_JS(void, flix_glitch_abort, (), { abort('CRITICAL_PROCESS_DIED'); });
#endif

namespace flix {

using namespace flix::ui;

namespace {

/// The death card's geometry, so the paint and the hit test cannot drift.
///
/// Laid out to death_ui_screenshot.png, which is 1:1 with design units (its
/// loadout slots are this build's 70 across). Everything hangs off the dead
/// flower, and the flower sits on the screen's centre -- exactly over the
/// corpse the camera is pinned to -- so each row is an offset from it rather
/// than a place in a stack. Text rows are given by the middle of their glyphs,
/// which is where ui::text centres a line.
struct DeathCard {
    Vec2 destroyedBy;   ///< centre of "You were destroyed by:"
    Vec2 killer;        ///< centre of the killer's name
    Vec2 flower;        ///< centre of the dead flower
    Rect continueBox;
    Rect closeBox;
};

constexpr double kDeathDestroyedBySize = 18.0;
constexpr double kDeathDestroyedByY = -118.5;
constexpr double kDeathKillerSize = 24.0;
constexpr double kDeathKillerY = -89.0;

/// The dead flower, at this times its 25-unit art: its yellow is 35 across
/// the radius in the screenshot, and the art's is 23.5.
constexpr double kDeathFlowerScale = 1.49;
/// Turned a little anticlockwise, as the reference's DeadFlowerIcon turns it.
constexpr double kDeathFlowerTilt = -0.2;
/// A frown: the face's curve control, where the living flower's 15 smiles.
constexpr double kDeathFlowerMouth = 5.0;
/// The loadout round it, in the flower's art units. The reference's ellipse
/// shape -- wider than tall, the top half behind the body and the bottom half
/// in front -- at the size and phase the screenshot measures: 44 by 29 rather
/// than gardn's 40 by 25, with the first petal 0.96 radians round, low on the
/// right.
constexpr double kDeathRingX = 44.0;
constexpr double kDeathRingY = 29.0;
constexpr double kDeathRingPhase = 0.96;

/// Continue is a gardn button: a 132x40 box under a 6-unit rim at 0.8 of its
/// fill, which button() derives. Close is the same button, smaller and grey.
constexpr double kDeathContinueY = 95.0;
constexpr double kDeathContinueWidth = 132.0;
constexpr double kDeathContinueHeight = 40.0;
constexpr double kDeathContinueRim = 6.0;
constexpr double kDeathContinueRadius = 4.0;
constexpr double kDeathContinueTextSize = 24.0;
constexpr std::uint32_t kDeathContinueFill = 0x62CE49u;
/// A jumpscare death's Continue is the corruption red of the flower that
/// slammed the screen (FaceHasCorruption's body colour), not the ordinary
/// green: it does not go home, it respawns you at the pad the trap named.
constexpr std::uint32_t kDeathContinueJumpscareFill = 0xD91313u;
constexpr double kDeathCloseY = 140.0;
constexpr double kDeathCloseWidth = 68.0;
constexpr double kDeathCloseHeight = 24.0;
constexpr double kDeathCloseRim = 4.0;
constexpr double kDeathCloseRadius = 3.0;
constexpr double kDeathCloseTextSize = 16.0;
constexpr std::uint32_t kDeathCloseFill = 0x666666u;

/// How far the card reaches below the flower: Close's box and half its rim.
constexpr double kDeathCardBelow = kDeathCloseY + kDeathCloseHeight * 0.5 + kDeathCloseRim * 0.5;

/// How far past the top edge the parked card's last row sits. The card stops
/// being painted at a slide of 0.01, still 1% short of parked, and that 1%
/// must not bring Close's rim back onto the screen.
constexpr double kDeathParkClearance = 16.0;

/// `slide` is the container's animation: 1 is home and 0 parks the whole card
/// above the screen, which is the reference's animate hook --
/// `translate(0, (animation - 1) * height * 0.6)` -- with one change: 60% of a
/// screen is only a floor. At 1080 units tall it leaves Close hanging off the
/// top edge for the last frames of the slide out, so the card is lifted as far
/// as it takes to clear the edge whenever that is more.
DeathCard deathCardLayout(double width, double height, double slide) {
    const double park =
        std::max(height * 0.6, height * 0.5 + kDeathCardBelow + kDeathParkClearance);
    const Vec2 flower{width * 0.5, height * 0.5 + (slide - 1.0) * park};

    DeathCard card;
    card.flower = flower;
    card.destroyedBy = {flower.x, flower.y + kDeathDestroyedByY};
    card.killer = {flower.x, flower.y + kDeathKillerY};
    card.continueBox = {flower.x - kDeathContinueWidth * 0.5,
                        flower.y + kDeathContinueY - kDeathContinueHeight * 0.5,
                        kDeathContinueWidth, kDeathContinueHeight};
    card.closeBox = {flower.x - kDeathCloseWidth * 0.5,
                     flower.y + kDeathCloseY - kDeathCloseHeight * 0.5, kDeathCloseWidth,
                     kDeathCloseHeight};
    return card;
}

/// The reference's DeadFlowerIcon: the corpse's face with the active row of
/// the loadout laid round it. One icon per slot, whatever the petal's count,
/// at the size a tile draws it. Equipment -- a zero-count petal, the antennae
/// or a third eye -- is not a seat in the ring: it is worn, as on the corpse.
void drawDeathFlower(Canvas& canvas, const WorldRenderer& renderer, const SpriteCache& sprites,
                     const std::vector<Profile::Slot>& loadout, Vec2 centre, double time) {
    struct Seat {
        std::uint16_t petal;
        Rarity rarity;
    };
    std::vector<Seat> ring;
    std::uint16_t equipFlags = EquipNone;
    const std::size_t active =
        std::min(loadout.size(), static_cast<std::size_t>(kLoadoutActiveSlots));
    for (std::size_t i = 0; i < active; ++i) {
        const Profile::Slot& slot = loadout[i];
        if (slot.empty() || slot.petalIndex >= content().petalCount()) continue;
        const PetalConfig& config = content().petal(slot.petalIndex);
        if (config.count > 0 && !config.hidden) ring.push_back({slot.petalIndex, slot.rarity});
        equipFlags |= config.equipFlags;
    }

    canvas.save();
    canvas.translate(static_cast<float>(centre.x), static_cast<float>(centre.y));
    canvas.scale(static_cast<float>(kDeathFlowerScale), static_cast<float>(kDeathFlowerScale));
    const auto paintSeats = [&](bool front) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const double angle =
                kTau * static_cast<double>(i) / static_cast<double>(ring.size()) +
                kDeathRingPhase;
            const double y = std::sin(angle) * kDeathRingY;
            if ((y > 0.0) != front) continue;
            const PetalStats stats = content().petalStats(ring[i].petal, ring[i].rarity);
            drawPetalCluster(canvas, sprites, ring[i].petal, ring[i].rarity, stats.size, 1,
                             std::cos(angle) * kDeathRingX, y, 0.0, time);
        }
    };
    paintSeats(false);
    canvas.save();
    canvas.rotate(static_cast<float>(kDeathFlowerTilt));
    renderer.drawDeadFlower(canvas, equipFlags, kDeathFlowerMouth, time);
    canvas.restore();
    paintSeats(true);
    canvas.restore();
}

/// The glitch crash's acts, in seconds: the tear, the stop screen filling 0%
/// to 100%, and how long it holds at 100% before the client really crashes.
/// Shared by the draw and the crash, so the last frame drawn is the one the
/// crash leaves on screen.
constexpr double kGlitchTearSeconds = 2.4;
constexpr double kGlitchBsodSeconds = 5.0;
constexpr double kGlitchCrashHoldSeconds = 1.0;

/// The player's body colour on the stop screen: a step lighter than the
/// Windows blue behind it, so the flower's own darker rim reads against it.
constexpr std::uint32_t kGlitchFlowerBlue = 0x4FA6F2u;

/// The jumpscare's beats, in seconds: the lunge in, a hold staring back (the
/// lunge is part of it), then a fade that hands the screen to the death card
/// sliding in underneath. Short on purpose: a jumpscare is a flinch, not a
/// cutscene. Shared by the picture and the scream, so the two land together.
constexpr double kJumpscareGrowSeconds = 0.14;   // the flower rushing the screen
constexpr double kJumpscareHoldSeconds = 0.45;   // full and shaking
constexpr double kJumpscareFadeSeconds = 0.35;   // dissolving to the card

} // namespace

void App::sendInputFrame(double dt) {
    net::InputFrame input;
    input.sequence = ++inputSequence_;

    // What the camera actually draws, in world units rather than pixels: the
    // render scales by userZoom, so zooming out shows more world through the
    // same window and the server has to widen what it streams to match. It
    // rides every frame, including the menu-open one below, so a resize or a
    // wheel zoom takes effect on the next tick rather than at the next join.
    const double zoom = camera_.zoom() > 1e-6 ? camera_.zoom() : 1.0;
    input.viewportWidth = static_cast<std::uint16_t>(
        std::min(65535.0, std::round(window_.width() / zoom)));
    input.viewportHeight = static_cast<std::uint16_t>(
        std::min(65535.0, std::round(window_.height() / zoom)));

    // An open menu owns the POINTER, and only the pointer. Steering toward a
    // cursor that is over a panel to drag an item around would send the flower
    // running at whatever slot the hand happened to stop on, so the cursor half
    // -- and the mouse buttons with it -- goes quiet while a panel is up. The
    // movement keys are not the menu's to take: they aim at nothing, cost the
    // open panel nothing, and being frozen in place with the inventory up is
    // how a player gets eaten. They still stop for the one thing that really is
    // typing -- a focused field, which keyboardCaptured() answers for below.
    const bool menuOpen = menus_.anyOpen();

    // Movement follows the cursor, which is the control scheme this game is
    // built around: the flower runs toward the pointer, at a speed set by how
    // far away it is. The movement keys are offered as an alternative rather
    // than a supplement, and win when held so the two cannot fight -- and
    // "Use Mouse Controls", which the K binding toggles in game, is what says
    // whether the cursor half is there at all. The arrows are not a binding:
    // the reference reads them beside whatever the four keys are bound to.
    const ClientSettings& settings = menus_.settings();
    Vec2 keyboard{0, 0};
    // A typed key is a character, not a direction. The reference's key handler
    // returns before it records anything once an <input> has focus, so while
    // the chat line -- or a panel's own field -- is taking keystrokes, none of
    // them reach the movement set. The cursor half keeps working: only the
    // keyboard half goes quiet.
    if (!keyboardCaptured()) {
        if (boundKeyDown(window_, settings.controlKey(ControlAction::MoveUp)) ||
            window_.keyDown(Key::Up)) {
            keyboard.y -= 1;
        }
        if (boundKeyDown(window_, settings.controlKey(ControlAction::MoveDown)) ||
            window_.keyDown(Key::Down)) {
            keyboard.y += 1;
        }
        if (boundKeyDown(window_, settings.controlKey(ControlAction::MoveLeft)) ||
            window_.keyDown(Key::Left)) {
            keyboard.x -= 1;
        }
        if (boundKeyDown(window_, settings.controlKey(ControlAction::MoveRight)) ||
            window_.keyDown(Key::Right)) {
            keyboard.x += 1;
        }
    }

    const Vec2 cursorWorld = camera_.screenToWorld({window_.mouseX(), window_.mouseY()});
    const Vec2 toCursor = cursorWorld - net_.view().selfDrawnPosition();

    // The on-screen stick, when there is one. It REPLACES the cursor half
    // rather than joining it: with touch controls up the pointer is wherever
    // the last tap landed, and letting that steer would send the flower
    // running at whatever was last pressed for as long as nobody tapped
    // anywhere else.
    const bool touchControls = touchControlsVisible();
    ui::MobileControls::Stick stick;
    const bool stickPushed = touchControls && mobile_.stick(stick);

    if (keyboard.lengthSq() > 0) {
        const Vec2 direction = keyboard.normalized();
        input.moveAngle = direction.angle();
        input.moveStrength = 1.0;
    } else if (stickPushed) {
        // Deflection straight to speed, no floor: the same law the cursor
        // follows, measured against the base radius instead of a distance.
        input.moveAngle = stick.direction.angle();
        input.moveStrength = stick.magnitude;
    } else if (settings.useMouseControls && !touchControls && !menuOpen) {
        const double distance = toCursor.length();
        input.moveAngle = distance > 1e-6 ? toCursor.angle() : 0.0;
        input.moveStrength = std::min(1.0, distance / kFullSpeedCursorDistance);
    } else {
        // Standing still is a decision, not a gap: with the cursor half off,
        // a frame with no key held has to say "no movement" rather than leave
        // the last angle to be read as one.
        input.moveAngle = 0.0;
        input.moveStrength = 0.0;
    }

    // Aim follows the cursor, even under keyboard movement: where the petals
    // point and where you walk are separate decisions. A thumb cannot make
    // that second decision -- there is no second pointer to make it with --
    // so a pushed stick aims as well as moves, and a centred one leaves the
    // aim where it last was rather than snapping it to a stale tap.
    //
    // The distance rides with the angle, for a worn cursor's pets. A stick has
    // no distance of its own, so it reaches as far as the cursor law it moves
    // by: full deflection is kFullSpeedCursorDistance ahead of the flower.
    const auto aimDistance = [](double distance) {
        return static_cast<std::uint16_t>(std::clamp(std::round(distance), 0.0, 65535.0));
    };
    if (stickPushed) {
        input.aimAngle = stick.direction.angle();
        input.aimDistance = aimDistance(stick.magnitude * kFullSpeedCursorDistance);
    }
    // A pointer parked on an open panel is not an aim, for the same reason it
    // is not a heading: hold the last one rather than pointing the petals at
    // whichever slot the hand stopped over.
    else if (menuOpen) {
        input.aimAngle = lastAimAngle_;
        input.aimDistance = lastAimDistance_;
    } else if (!touchControls) {
        input.aimAngle = toCursor.lengthSq() > 1e-12 ? toCursor.angle() : 0.0;
        input.aimDistance = aimDistance(toCursor.length());
    } else {
        input.aimAngle = lastAimAngle_;
        input.aimDistance = lastAimDistance_;
    }
    lastAimAngle_ = input.aimAngle;
    lastAimDistance_ = input.aimDistance;

    const Vec2 pointer{window_.mouseX(), window_.mouseY()};
    // A press that landed on a line of chat is a text selection, not a swing.
    // Checked against the PREVIOUS frame's runs because this pass runs before
    // anything has been painted, and only on the press itself -- a hover that
    // blocked the attack would make the whole lower-left corner unshootable.
    ui::TextSelect& selectable = ui::TextSelect::instance();
    const bool textDrag = selectable.dragging() ||
                          (window_.mousePressed(MouseButton::Left) &&
                           selectable.overTextLastFrame(pointer));
    if (!chatOpen_ && !menuOpen && !textDrag && !menus_.capturesMouse(pointer) &&
        !tutorial_.capturesMouse(pointer)) {
        if (window_.mouseDown(MouseButton::Left) ||
            boundKeyDown(window_, settings.controlKey(ControlAction::ExtendPetals))) {
            input.flags |= net::InputAttack;
        }
        if (window_.mouseDown(MouseButton::Right) ||
            boundKeyDown(window_, settings.controlKey(ControlAction::RetractPetals))) {
            input.flags |= net::InputDefend;
        }
    }
    // Outside that guard on purpose: the two on-screen buttons are their own
    // contacts and answer for themselves. None of what the guard is about --
    // a press that landed on a panel, on a line of chat, or on the tutorial
    // card -- can be true of a finger the stick's own hit test claimed.
    if (touchControls) {
        if (mobile_.attackPressed()) input.flags |= net::InputAttack;
        if (mobile_.retractPressed()) input.flags |= net::InputDefend;
    }

    net_.sendInput(input);
}

void App::updatePlaying(double dt) {
    // Chat swallows the keyboard while open, or typing would also drive the
    // flower and trip every hotkey.
    if (chatOpen_ && !menus_.wantsText()) {
        editChatLine();
    } else {
        // The menus get first refusal on the keyboard: a hotkey they claim is
        // not also a chat key. Escape is one of theirs now -- it opens the
        // settings panel and no longer leaves the game, which is the red exit
        // button in the top strip's job alone.
        const bool consumed = menus_.handleKeys(window_);
        if (!consumed) {
            if (boundKeyPressed(window_, menus_.settings().controlKey(ControlAction::Chat)) ||
                pressedChatBox()) {
                chatOpen_ = true;
            }
        }
    }

    // Input is produced at the simulation rate rather than per rendered frame:
    // a 144 Hz client must not get six times the inputs of a 30 Hz one.
    inputAccumulator_ += dt;
    const double step = net::kTickSeconds;
    int produced = 0;
    while (inputAccumulator_ >= step && produced < 4) {
        inputAccumulator_ -= step;
        sendInputFrame(step);
        ++produced;
    }
    // A long stall must not queue a burst of catch-up input.
    if (inputAccumulator_ > step * 4) inputAccumulator_ = 0;

    // The wheel zooms the camera unless a panel -- or the tutorial box, which
    // is a DOM element and eats the event before the canvas -- is over it, and
    // unless the transcript took it to read back through itself.
    if (!scrollChat() && !menus_.capturesMouse({window_.mouseX(), window_.mouseY()}) &&
        !tutorial_.capturesMouse({window_.mouseX(), window_.mouseY()})) {
        menus_.settings().zoom =
            clamp(menus_.settings().zoom + window_.wheelDelta() * 0.05, kMinZoom, kMaxZoom);
    }
}

void App::updateDead(double dt) {
    (void)dt;
    // The transcript keeps drawing over the dimmed world, so it keeps
    // scrolling. There is no camera zoom to lose the wheel to here, so nothing
    // hangs on the answer.
    scrollChat();
    // Chat swallows the keyboard while it is open, as it does in the living
    // game. Not returned from -- a click on Continue or Close closes the line
    // AND presses the button, which is one click, not two.
    if (chatOpen_ && !menus_.wantsText()) {
        editChatLine();
    } else if (pressedChatBox() || window_.keyPressed(Key::Enter)) {
        // Enter is chat's, card or no card: Continue has no key, and the slot
        // under the dimmed world still says "Press Enter to chat...".
        chatOpen_ = true;
    }
    if (!deathCardVisible_) return;

    // The reference acts on the press, not the release, so the card is gone by
    // the time the button comes back up and a pressed state is never seen.
    if (!window_.mousePressed(MouseButton::Left)) return;

    const Vec2 mouse{window_.mouseX(), window_.mouseY()};
    // The tutorial box is painted over the death card and swallows the click.
    if (tutorial_.capturesMouse(mouse)) return;
    // The ANIMATED boxes, not the resting ones: a button is only where it is
    // painted, and during the slide-in that is on its way down the screen.
    const DeathCard card = deathCardLayout(window_.width(), window_.height(), deathCardSlide_);
    if (hit(card.continueBox, mouse)) {
        if (net_.jumpscareDeath()) {
            // A jumpscare's red Continue does not leave to the title: it asks
            // the server to respawn the flower at the pad the trap named, which
            // the server answers with a fresh body (pollNetwork re-enters the
            // world on that accept). The card slides away the way Close's does
            // so the button cannot be pressed twice while the answer is in
            // flight.
            net_.requestRespawn();
            deathCardVisible_ = false;
        } else {
            leaveToTitle();
        }
        return;
    }
    // Close takes the card away -- it slides back up the way it came -- and
    // the world's dim fades out with it. The player stays dead, and the HUD
    // and the minimap keep drawing behind where it was.
    if (hit(card.closeBox, mouse)) deathCardVisible_ = false;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void App::drawDeathCard(Canvas& canvas, double time) {
    // No scrim of its own. The death dim is a wash over the WORLD alone,
    // painted long before this -- see the call in frame() -- and the card is
    // one of the things that stays at full brightness over it.
    const DeathCard card = deathCardLayout(canvas.width(), canvas.height(), deathCardSlide_);
    const Vec2 mouse{window_.mouseX(), window_.mouseY()};

    // Bold white with the ordinary 0.12-of-size outline. No hint line under
    // the buttons: Continue is a click, with no key behind it.
    TextStyle line;
    line.align = Align::Centre;
    line.size = kDeathDestroyedBySize;
    text(canvas, "You were destroyed by:", card.destroyedBy.x, card.destroyedBy.y, line);

    line.size = kDeathKillerSize;
    const std::string killer = net_.killerName().empty()
        ? "A mysterious entity"
        : net_.killerName();
    text(canvas, killer, card.killer.x, card.killer.y, line);

    drawDeathFlower(canvas, renderer_, sprites_, net_.profile().loadout, card.flower, time);

    const bool pressing = window_.mouseDown(MouseButton::Left);
    const bool overContinue = hit(card.continueBox, mouse);
    ButtonStyle continueStyle;
    continueStyle.fill =
        net_.jumpscareDeath() ? kDeathContinueJumpscareFill : kDeathContinueFill;
    continueStyle.outlineWidth = kDeathContinueRim;
    continueStyle.radius = kDeathContinueRadius;
    continueStyle.textSize = kDeathContinueTextSize;
    button(canvas, card.continueBox, "Continue", overContinue, overContinue && pressing,
           continueStyle);

    const bool overClose = hit(card.closeBox, mouse);
    ButtonStyle closeStyle;
    closeStyle.fill = kDeathCloseFill;
    closeStyle.outlineWidth = kDeathCloseRim;
    closeStyle.radius = kDeathCloseRadius;
    closeStyle.textSize = kDeathCloseTextSize;
    button(canvas, card.closeBox, "Close", overClose, overClose && pressing, closeStyle);
}

void App::beginJumpscare() {
    jumpscareStartSeconds_ = timeSeconds_;
    web::playJumpscareSound(kJumpscareGrowSeconds, kJumpscareHoldSeconds, kJumpscareFadeSeconds);
}

void App::drawJumpscare(Canvas& canvas, double time) {
    if (jumpscareStartSeconds_ < 0.0) return;

    constexpr double kGrowSeconds = kJumpscareGrowSeconds;
    constexpr double kHoldSeconds = kJumpscareHoldSeconds;
    constexpr double kFadeSeconds = kJumpscareFadeSeconds;
    const double elapsed = time - jumpscareStartSeconds_;
    if (elapsed >= kHoldSeconds + kFadeSeconds) {
        jumpscareStartSeconds_ = -1.0;
        return;
    }

    const double w = canvas.width();
    const double h = canvas.height();

    // Opaque through the hold, then ramps away over the fade so the red card
    // shows through as the scare lets go.
    const double alpha = elapsed < kHoldSeconds
        ? 1.0
        : std::clamp(1.0 - (elapsed - kHoldSeconds) / kFadeSeconds, 0.0, 1.0);

    // A near-black wash tinted toward the corruption red, so the flash reads as
    // the flower's own colour rather than a grey screen.
    setFill(canvas, 0x1A0000u, alpha);
    canvas.fillRect(0, 0, static_cast<float>(w), static_cast<float>(h));

    // The body's drawn circle is radius 26.5 in the artwork's own units; it is
    // scaled so the face overflows the shorter screen edge, rushing in over the
    // first frames and shaking while it holds the way a jumpscare snaps at you.
    constexpr double kBodyArtRadius = 26.5;
    const double grow = 0.55 + 0.75 * std::clamp(elapsed / kGrowSeconds, 0.0, 1.0);
    const double fit = (std::min(w, h) * 0.62) / kBodyArtRadius;
    const double shake = elapsed < kHoldSeconds ? 10.0 : 0.0;
    const double jx = std::sin(time * 83.0) * shake;
    const double jy = std::cos(time * 97.0) * shake;

    // A corrupted flower with dead eyes: FaceHasCorruption paints the body the
    // same red as the wash and the card's button, and the crossed eyes are the
    // most unsettling face the renderer draws. No skin, no petals, no kit --
    // this is the trap's face, not the player's own flower.
    RemoteEntity flower;
    flower.kind = net::EntityKind::Player;
    flower.faceFlags = static_cast<std::uint8_t>(FaceHasCorruption | FaceDeadEyes);
    flower.radius = kBodyArtRadius;

    canvas.save();
    canvas.translate(static_cast<float>(w * 0.5 + jx), static_cast<float>(h * 0.5 + jy));
    canvas.scale(static_cast<float>(fit * grow), static_cast<float>(fit * grow));
    canvas.setGlobalAlpha(static_cast<float>(alpha));
    renderer_.drawFlowerBody(canvas, flower, time);
    canvas.setGlobalAlpha(1.0f);
    canvas.restore();
}

void App::beginGlitch() {
    glitchStartSeconds_ = timeSeconds_;
    glitchRecovers_ = menus_.settings().glitched;
    web::playGlitchSound(kGlitchTearSeconds, kGlitchBsodSeconds, kGlitchCrashHoldSeconds);
    // The crash swallows the whole screen, and nobody clicks for it: the
    // desktop build switches at once, and a page does if the player's
    // last key or click is still fresh (see Window::requestFullscreen).
    window_.requestFullscreen();
    if (!menus_.settings().glitched) {
        menus_.settings().glitched = true;
        menus_.settings().save(settingsPath());
    }
}

void App::drawGlitch(Canvas& canvas, double time) {
    if (!glitchActive()) return;

    const double w = canvas.width();
    const double h = canvas.height();
    const double unit = std::min(w, h);
    const double elapsed = time - glitchStartSeconds_;

    // Two acts: the frozen screen tearing apart in corruption symbols, then a
    // Windows blue screen filling to 100% -- which holds there for
    // kGlitchCrashHoldSeconds before the client really does crash on it
    // (crashGlitched, after this frame is presented) -- or, on an install that
    // has crashed before, "restarts" into the corrupted desktop
    // (recoverFromGlitch). Nothing
    // here reads input, and the client does not update behind it.
    constexpr double kTearSeconds = kGlitchTearSeconds;
    constexpr double kBsodSeconds = kGlitchBsodSeconds;   // 0% -> 100% over this
    const double bsod = elapsed - kTearSeconds;

    // Cheap time-seeded noise in [0,1): the tear re-rolls every frame without
    // any RNG state leaking into a draw path.
    const auto noise = [](double a) {
        const double s = std::sin(a * 12.9898) * 43758.5453;
        return s - std::floor(s);
    };

    if (elapsed < kTearSeconds) {
        // ACT ONE -- the frozen world beneath tears apart. The re-roll clock
        // steps ~16x a second so the glitch stutters rather than smears.
        const double tick = std::floor(time * 16.0);
        for (int i = 0; i < 26; ++i) {
            if (noise(tick * 1.7 + i * 3.1) < 0.5) continue;
            const double y = noise(tick + i * 7.3) * h;
            const double barH = 2.0 + noise(tick * 2.0 + i) * 28.0;
            const double dx = (noise(tick * 3.0 + i * 5.0) - 0.5) * 90.0;
            const std::uint32_t tint =
                (i % 3 == 0) ? 0x00FFFFu : (i % 3 == 1) ? 0xFF00FFu : 0xFFFFFFu;
            setFill(canvas, tint, 0.10 + noise(tick + i) * 0.22);
            canvas.fillRect(static_cast<float>(dx), static_cast<float>(y),
                            static_cast<float>(w), static_cast<float>(barH));
        }
        // Darkening toward the blue screen it is about to cut to.
        setFill(canvas, 0x000000u, std::clamp(elapsed / kTearSeconds, 0.0, 1.0) * 0.6);
        canvas.fillRect(0, 0, static_cast<float>(w), static_cast<float>(h));

        // Random corruption symbols strewn across the whole screen in place of
        // any words -- re-rolled on the stutter clock so they jump, flicker and
        // split like a signal coming apart. No message, just noise that got in.
        static const char* const kGlyphs[] = {
            "!", "@", "#", "$", "%", "&", "*", "?", "/", "\\", "|", "=", "+", "<", ">",
            "~", "^", "0", "1", "7", "9", "X", "Z", "§", "¥", "¿",
            "†", "‡", "Ø", "¤", "∆", "◆", "▲",
            "■", "●", "█", "▓", "▒", "░", "╳",
            "╬", "¬",
        };
        constexpr int kGlyphCount = static_cast<int>(sizeof(kGlyphs) / sizeof(kGlyphs[0]));
        for (int i = 0; i < 60; ++i) {
            if (noise(tick * 1.3 + i * 2.7) < 0.32) continue;   // some blink out
            const double gx = noise(tick + i * 1.9) * w;
            const double gy = noise(tick * 1.1 + i * 2.3) * h;
            const int gi = static_cast<int>(noise(i * 5.7 + tick * 0.5) * kGlyphCount) % kGlyphCount;
            const std::uint32_t tint = (i % 4 == 0)   ? 0x00FFFFu
                                       : (i % 4 == 1) ? 0xFF00FFu
                                       : (i % 4 == 2) ? 0xFF1744u
                                                      : 0xFFFFFFu;
            TextStyle g;
            g.size = unit * (0.028 + noise(tick + i * 4.1) * 0.085);
            g.align = Align::Centre;
            g.strokeWidth = 0.0;
            // A third split into red/cyan ghosts for the broken-signal look.
            if (noise(tick * 2.0 + i) > 0.6) {
                g.fill = 0xFF1744u;
                text(canvas, kGlyphs[gi], gx - 4.0, gy, g);
                g.fill = 0x18FFFFu;
                text(canvas, kGlyphs[gi], gx + 4.0, gy, g);
            }
            g.fill = tint;
            text(canvas, kGlyphs[gi], gx, gy, g);
        }
        return;
    }

    // ACT TWO -- the real Windows stop screen, filling to 100% and holding there
    // until the client really crashes on it; in a page it is the frame the
    // abort leaves on screen, and on a second crash the last before the
    // corrupted desktop boots. The Windows-10 blue, the face, the QR square and
    // the stop code are drawn the way the genuine screen draws them, in the
    // genuine FONT: the text here is handed to the canvas raw -- setFont +
    // fillText -- rather than through the game's own stroked-outline text(), so
    // the web build sets it in Segoe UI off the player's own machine, exactly
    // as a true bluescreen would. The desktop build (tests, screenshots) has no
    // Segoe UI and falls back to its one bundled face, which is all it needs.
    const double pct = std::clamp(bsod / kBsodSeconds, 0.0, 1.0);

    setFill(canvas, 0x0078D7u);
    canvas.fillRect(0, 0, static_cast<float>(w), static_cast<float>(h));

    // Integer pixel size for the CSS font shorthand. The desktop font parser
    // reads the FIRST digit in the string as the size, so a weight is carried
    // in the family name ("Segoe UI Light"), never as a leading number.
    const auto px = [](double v) { return std::to_string(static_cast<int>(std::lround(v))); };
    const std::string kLight = "px \"Segoe UI Light\", \"Segoe UI\", Tahoma, Geneva, sans-serif";
    const std::string kReg = "px \"Segoe UI\", Tahoma, Geneva, sans-serif";
    const auto winText = [&](const std::string& s, double x, double y, const std::string& font,
                             std::uint32_t rgb) {
        canvas.setFont(font);
        setFill(canvas, rgb);
        canvas.fillText(s, static_cast<float>(x), static_cast<float>(y));
    };

    // Where the stop screen's QR square sits, the player themselves looks back:
    // their own flower -- its eyes and where they were looking, its antennae,
    // third eye or whatever else it wears -- painted by the world's face code,
    // not a picture of one, with the body in blue so the whole of it is black
    // and blue. The body is the one the last snapshot left in the view, which
    // the frozen client still holds; before any has landed, the plain face.
    const auto drawPlayer = [&](double qx, double qy, double size) {
        static const RemoteEntity kPlainFlower = [] {
            RemoteEntity e;
            e.kind = net::EntityKind::Player;
            return e;
        }();
        const auto& entities = net_.view().entities();
        const auto found = entities.find(net_.view().self().netId);
        const RemoteEntity& flower = found != entities.end() ? found->second : kPlainFlower;

        // The face's rim is radius 26.5 in its own art space; it is fitted to
        // the square the QR code would fill.
        constexpr double kRimArtRadius = 26.5;
        const double scale = size * 0.5 / kRimArtRadius;
        canvas.save();
        canvas.translate(static_cast<float>(qx + size * 0.5), static_cast<float>(qy + size * 0.5));
        canvas.scale(static_cast<float>(scale), static_cast<float>(scale));
        renderer_.drawFlowerInColor(canvas, flower, kGlitchFlowerBlue, time);
        canvas.restore();
    };

    // The words are Windows 10's own, line for line as it breaks them on a
    // 1080p screen, and in its sizes: the face, then the paragraph and the
    // percentage in the same light text, then the code square with the small
    // support lines beside it. Everything is laid out from the top down in
    // `unit`s, so the block keeps its proportions on any window shape.
    canvas.save();
    canvas.setTextAlign("left");
    canvas.setTextBaseline("top");
    const double leftX = w * 0.10;
    const double faceTop = h * 0.15;

    winText(":)", leftX, faceTop, px(unit * 0.15) + kLight, 0xFFFFFFu);

    const std::string body = px(unit * 0.028) + kLight;
    const double lh = unit * 0.040;
    const double paraTop = faceTop + unit * 0.24;
    winText("Your PC ran into a problem and needs to restart. We're just", leftX, paraTop, body,
            0xFFFFFFu);
    winText("collecting some error info, and then we'll restart for you.", leftX, paraTop + lh,
            body, 0xFFFFFFu);

    const double pctTop = paraTop + lh * 3.0;
    winText(std::to_string(static_cast<int>(pct * 100.0)) + "% complete", leftX, pctTop, body,
            0xFFFFFFu);

    const double codeSize = unit * 0.10;
    const double codeTop = pctTop + lh * 2.2;
    drawPlayer(leftX, codeTop, codeSize);

    const double sx = leftX + codeSize + unit * 0.022;
    const std::string small = px(unit * 0.0155) + kReg;
    const double slh = unit * 0.024;
    winText("For more information about this issue and possible fixes, visit "
            "https://www.windows.com/stopcode",
            sx, codeTop, small, 0xFFFFFFu);
    winText("If you call a support person, give them this info:", sx, codeTop + slh * 2.4, small,
            0xFFFFFFu);
    winText("Stop code: CRITICAL_PROCESS_DIED", sx, codeTop + slh * 3.4, small, 0xFFFFFFu);
    canvas.restore();
}

bool App::glitchEndDue(double time) const {
    return glitchActive() &&
           time - glitchStartSeconds_ >=
               kGlitchTearSeconds + kGlitchBsodSeconds + kGlitchCrashHoldSeconds;
}

void App::crashGlitched() {
    // A real crash, but one that loses nothing a player would miss. The
    // settings (florr.exe ever after) and the session token go to disk first.
    // The socket is closed by hand so the server reaps the body through its
    // ordinary disconnect path at once: a dead process's socket closes itself,
    // but a page outlives its aborted runtime, and an open one would go on
    // streaming the world into a client that will never read it again.
    persist();
    net_.disconnect();
    // The power going, which the page plays out after the runtime has died:
    // the sound is scheduled whole, and the page's audio outlives the wasm.
    web::playSound(web::Sound::PowerCut);
    if (crashHook_) crashHook_();
#ifdef __EMSCRIPTEN__
    // Stopped first, so nothing calls back into the aborted runtime, and the
    // canvas keeps the stop screen this frame just presented.
    emscripten_cancel_main_loop();
    flix_glitch_abort();
#endif
    // The desktop: SIGABRT, and the OS's own "quit unexpectedly" report.
    std::abort();
}

void App::recoverFromGlitch() {
    glitchStartSeconds_ = -1.0;
    glitchRecovers_ = false;
    // The "restart" sounds like one: the power goes, and the desktop's own
    // startup comes up out of the black after it.
    web::playSound(web::Sound::PowerCut);
    // The flower is taken off the server the way Leave takes it, so the title
    // is what is waiting behind the desktop. Only from the world: a socket
    // that dropped during the crash has already put the client on the screen
    // that deals with that.
    if (screen_ == Screen::Playing || screen_ == Screen::Dead) leaveToTitle();
    // The documents are the account's own; the flower's name stands in on a
    // build that never signed one in.
    const std::string& account = net_.profile().username;
    xpDesktop_.open(timeSeconds_, account.empty() ? playerName_ : account);
}

void App::drawXpDesktop(Canvas& canvas) {
    if (!xpDesktop_.active()) return;
    ui::XpPainters painters;
    // Tiled, as XP tiles a small wallpaper: hel's ground, and over it the
    // field of dead flowers lv2 lays across its own floor.
    painters.wallpaper = [this](Canvas& c, double w, double h) {
        setFill(c, 0x8F3838u);
        c.fillRect(0, 0, static_cast<float>(w), static_cast<float>(h));
        for (const SvgDocument* art : {titleBackground("hel"), stagedArt("dead_florr_tiled.svg")}) {
            if (art == nullptr) continue;
            const double tileW = art->width() > 0 ? art->width() : 256.0;
            const double tileH = art->height() > 0 ? art->height() : 256.0;
            for (double y = 0; y < h; y += tileH) {
                for (double x = 0; x < w; x += tileW) {
                    // A pixel oversized, so no seam shows between tiles.
                    if (!drawCachedArt(c, *art, x, y, tileW + 1, tileH + 1)) {
                        art->renderFitted(c, static_cast<float>(x), static_cast<float>(y),
                                          static_cast<float>(tileW + 1),
                                          static_cast<float>(tileH + 1),
                                          static_cast<float>(timeSeconds_));
                    }
                }
            }
        }
    };
    painters.flower = [this](Canvas& c, double x, double y, double radius, bool dead) {
        // The body's rim is radius 26.5 in its own art space.
        constexpr double kRimArtRadius = 26.5;
        c.save();
        c.translate(static_cast<float>(x), static_cast<float>(y));
        c.scale(static_cast<float>(radius / kRimArtRadius), static_cast<float>(radius / kRimArtRadius));
        if (dead) {
            // A corpse's face, crossed eyes and the frown, as the wallpaper's
            // dead flowers wear it.
            renderer_.drawDeadFlower(c, 0, 5.0, timeSeconds_);
        } else {
            RemoteEntity flower;
            flower.kind = net::EntityKind::Player;
            flower.radius = kRimArtRadius;
            renderer_.drawFlowerBody(c, flower, timeSeconds_);
        }
        c.restore();
    };
    xpDesktop_.draw(canvas, timeSeconds_, painters);
}

void App::leaveXpDesktop() {
    // "Returning you to your normal computer": the game exits, the ordinary
    // way. step() reports the end after this frame, and run() -- or the page's
    // loop -- calls shutdown(), which saves the settings and the session, so
    // the next launch signs straight back in. The desktop stays up for that
    // last frame, so nothing else flashes on the way out. M28's word on what
    // happened waits for the next launch's title screen.
    menus_.settings().glitchAnnouncement = true;
    // Played out by the page after its loop has stopped, as the crash's is.
    web::playSound(web::Sound::DesktopShutdown);
    // Closed by hand, as the crash closes it: the server reaps the session at
    // once, and a page, which outlives its stopped loop, is not left holding
    // a socket nothing reads.
    net_.disconnect();
    running_ = false;
#ifdef __EMSCRIPTEN__
    // A page cannot close itself; it gives the screen back instead.
    emscripten_exit_fullscreen();
#endif
}

void App::tallyRunLoot() {
    // While this client steers somebody else's flower, the self id is THAT
    // flower's, and what it picks up goes into its own player's bag.
    if (net_.controllingFlower()) return;
    const std::uint32_t self = net_.view().self().netId;
    if (self == 0) return;
    for (const ViewEvent& event : net_.view().events()) {
        // Somebody else's pickup is not the viewer's loot, and a drop pays out
        // one copy to each eligible flower -- so only the viewer's own cue
        // counts, and each one is exactly one item.
        if (event.kind != net::EventKind::PickedUp || event.otherNetId != self) continue;
        const auto petal = static_cast<std::uint16_t>(event.amount);
        if (petal >= content().petalCount()) continue;
        const Rarity rarity = clampRarity(static_cast<int>(event.flag));
        const auto found = std::find_if(runLoot_.begin(), runLoot_.end(), [&](const RunLoot& l) {
            return l.petalIndex == petal && l.rarity == rarity;
        });
        if (found == runLoot_.end()) {
            runLoot_.push_back({petal, rarity, 1});
        } else if (found->count < UINT32_MAX) {
            ++found->count;
        }
    }
}

namespace {

/// The "Collected this run" panel, measured off the same screenshot as the
/// card: a 4-wide grid of 50-unit tiles on a 60 pitch, 30 in from either side
/// of a panel black at 0.55, its corner where the minimap's is. A short last
/// row is centred, as the screenshot centres its two.
constexpr int kRunLootColumns = 4;
constexpr double kRunLootTile = 50.0;
constexpr double kRunLootPitch = 60.0;
constexpr double kRunLootSide = 30.0;
constexpr double kRunLootMargin = 10.0;
constexpr double kRunLootTitleY = 33.0;   ///< middle of the title, from the panel top
constexpr double kRunLootTitleSize = 24.0;
constexpr double kRunLootGridY = 65.0;    ///< top of the first row, from the panel top
constexpr double kRunLootBottom = 20.0;
constexpr double kRunLootRadius = 6.0;
constexpr double kRunLootShade = 0.55;
/// The panel stops growing before it reaches the loadout bar. Rows past that
/// are dropped from the end, which is the commonest tier.
constexpr double kRunLootFloor = 260.0;

} // namespace

void App::drawRunLoot(Canvas& canvas, double time) {
    if (runLoot_.empty()) return;

    // Rarest first, then by name -- the screenshot's order.
    std::vector<RunLoot> shown = runLoot_;
    std::sort(shown.begin(), shown.end(), [](const RunLoot& a, const RunLoot& b) {
        if (a.rarity != b.rarity) return rarityIndex(a.rarity) > rarityIndex(b.rarity);
        const std::string& an = content().petal(a.petalIndex).name;
        const std::string& bn = content().petal(b.petalIndex).name;
        if (an != bn) return an < bn;
        return a.petalIndex < b.petalIndex;
    });

    const double width = kRunLootSide * 2.0 + kRunLootTile * kRunLootColumns +
                         (kRunLootPitch - kRunLootTile) * (kRunLootColumns - 1);
    const double left = canvas.width() - kRunLootMargin - width;
    const double top = kRunLootMargin;
    const int fitRows = std::max(
        1, static_cast<int>((canvas.height() - kRunLootFloor - top - kRunLootGridY +
                             (kRunLootPitch - kRunLootTile)) / kRunLootPitch));
    const int wantRows = (static_cast<int>(shown.size()) + kRunLootColumns - 1) / kRunLootColumns;
    const int rows = std::min(wantRows, fitRows);
    shown.resize(std::min(shown.size(), static_cast<std::size_t>(rows * kRunLootColumns)));
    const double height = kRunLootGridY + rows * kRunLootPitch -
                          (kRunLootPitch - kRunLootTile) + kRunLootBottom;

    // Fades with the card's slide, so it leaves with it on Close.
    canvas.setGlobalAlpha(static_cast<float>(std::clamp(deathCardSlide_, 0.0, 1.0)));
    setFill(canvas, 0x000000u, kRunLootShade);
    canvas.beginPath();
    canvas.roundRect(static_cast<float>(left), static_cast<float>(top),
                     static_cast<float>(width), static_cast<float>(height),
                     static_cast<float>(kRunLootRadius));
    canvas.fill();

    TextStyle title;
    title.size = kRunLootTitleSize;
    title.align = Align::Centre;
    text(canvas, "Collected this run", left + width * 0.5, top + kRunLootTitleY, title);

    for (int row = 0; row < rows; ++row) {
        const int first = row * kRunLootColumns;
        const int inRow = std::min(kRunLootColumns, static_cast<int>(shown.size()) - first);
        const double rowWidth = kRunLootTile * inRow + (kRunLootPitch - kRunLootTile) * (inRow - 1);
        const double rowLeft = left + (width - rowWidth) * 0.5;
        const double rowTop = top + kRunLootGridY + row * kRunLootPitch;
        for (int i = 0; i < inRow; ++i) {
            const RunLoot& loot = shown[static_cast<std::size_t>(first + i)];
            ItemTile tile;
            tile.petalIndex = loot.petalIndex;
            tile.rarity = loot.rarity;
            if (loot.count > 1) tile.badge = "x" + stackCountText(loot.count);
            tile.timeSeconds = time;
            drawItemTile(canvas, sprites_,
                         Rect{rowLeft + i * kRunLootPitch, rowTop, kRunLootTile, kRunLootTile},
                         tile);
        }
    }
    canvas.setGlobalAlpha(1.0f);
}

void App::drawDisconnectBanner(Canvas& canvas) {
    // A strip across the very top, with the world and the HUD still drawing
    // underneath it. A dropped socket does not take the game off the screen.
    setFill(canvas, 0xC81E1Eu, 0.85);
    canvas.fillRect(0, 0, static_cast<float>(canvas.width()), 38.0f);

    TextStyle style;
    style.size = 16;
    style.align = Align::Centre;
    style.strokeWidth = 0;
    // Only promises the redial when there is one: a socket that was hung up
    // on deliberately is not coming back by itself.
    text(canvas,
         net_.reconnecting() ? "Disconnected from server. Reconnecting..."
                             : "Disconnected from server.",
         canvas.width() * 0.5, 19.0, style);
}

bool App::connectionLost() const {
    // Both halves of a redial: the wait between attempts reports Failed, an
    // attempt in flight reports Connecting. Testing only the first blinked
    // the banner out for the length of every try.
    return net_.status() == NetClient::Status::Failed || net_.reconnecting();
}

} // namespace flix
