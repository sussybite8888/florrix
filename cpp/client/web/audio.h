#pragma once

// Sound for the browser builds, through the Web Audio API.
//
// Nothing here loads a file: every sound is synthesized in the page from
// oscillators and noise as it plays, so there are no assets to embed, fetch or
// fold into the content hash. A native build is silent for now -- every call
// below is a no-op there -- until the desktop gets an audio backend of its own.
//
// A browser keeps a page quiet until the player has pressed something, and an
// AudioContext made before then starts suspended. So the context is not made
// on demand when a sound is due (by then it is too late to unlock it); it is
// made, or woken, by the player's own keys and clicks, and a sound asked for
// while it is still asleep is dropped rather than queued to go off late.
//
// Every sound is scheduled whole the moment it is asked for, so one that is
// still playing when the runtime aborts -- the glitch crash -- plays out
// regardless: the page's audio outlives the wasm.

#include <cstdint>

namespace flix::web {

/// The one-shot sounds: everything but the two that are timed to a sequence
/// (playJumpscareSound, playGlitchSound).
enum class Sound : std::uint8_t {
    /// The power going: what the glitch crash ends on, whether the client
    /// really crashes or "restarts" into the corrupted desktop. Cuts whatever
    /// the glitch is still playing.
    PowerCut,
    /// The corrupted desktop fading in: a startup chord that warbles, sags
    /// and twice drops out like a scratched disc.
    DesktopStartup,
    /// The hunt is over and the game exits: the chord winding down.
    DesktopShutdown,
    /// A tray balloon popping up.
    Balloon,
    /// An error box: "Access is denied", the Restrictions box.
    Error,
    /// A warning or information box.
    Notice,
    /// A box asking Yes or No.
    Question,
    /// corruption.exe's own application error: the error, skipping like a
    /// broken record.
    CorruptionError,
    /// Explorer opening a folder or moving to another.
    Navigate,
    /// Something thrown away: paper balled up and dropped in the bin.
    Recycle,
    /// A corruption.exe thrown away: the crumple, and the thing inside it
    /// shrieking down to nothing.
    Purge,
    /// The last corruption.exe is gone: one clean chord.
    AllClear,
};

/// Arms the page's audio: listens for the player's keys, clicks and taps, and
/// on the first one creates the AudioContext; any later one wakes it again if
/// the browser has suspended it since (iOS does when the tab is backgrounded).
/// Safe to call more than once. A no-op on a native build, or in a browser
/// with no Web Audio.
void installAudio();

/// Plays `sound` once, now. Dropped if the audio has not been unlocked yet.
void playSound(Sound sound);

/// The jumpscare trap's scream: a thud, a burst of noise and a clipped,
/// dissonant shriek, shaped to the picture it plays under. It hits the instant
/// the face lunges in, climbs in pitch over `lungeSeconds` as the face rushes
/// the screen, holds full until `holdSeconds` (which includes the lunge), then
/// sags and dies away over `fadeSeconds` as the picture fades to the death
/// card. Dropped if the audio has not been unlocked yet.
void playJumpscareSound(double lungeSeconds, double holdSeconds, double fadeSeconds);

/// The glitch crash's soundtrack, laid to its two acts. Over `tearSeconds`
/// the screen tearing apart: a growing electrical buzz under crushed noise
/// and data blips re-rolled sixteen times a second, as the picture's glitch
/// is, thicker as it goes, then a click and dead silence as the blue screen
/// cuts in. Over the `bsodSeconds` it fills to 100%, a machine still on: a
/// faint mains hum and the hard disk ticking as it "collects error info";
/// then for `holdSeconds` only the hum, until Sound::PowerCut ends it.
/// Dropped if the audio has not been unlocked yet.
void playGlitchSound(double tearSeconds, double bsodSeconds, double holdSeconds);

} // namespace flix::web
