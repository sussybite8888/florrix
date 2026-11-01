#include "client/web/audio.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace flix::web {

#ifdef __EMSCRIPTEN__

// Every EM_JS body below reaches the page as a single line -- the preprocessor
// joins it -- so each statement ends in its own semicolon: there is no newline
// left for JS to insert one at.

// Installs Module.flixAudio: the one AudioContext the page plays through, the
// buffers the sounds reuse, and the few building blocks every sound below is
// made of. Listened for on the window in the capture phase, so the canvas's
// own handlers (which consume most input) cannot swallow the gesture first.
EM_JS(void, flix_audio_install, (), {
    if (Module.flixAudio) return;
    const Context = window.AudioContext || window.webkitAudioContext;
    const audio = {
        context: null,
        // Noise buffers, by how many samples each value is held for.
        noises: {},
        // What the glitch crash is playing through, so the PowerCut that ends
        // it can cut whatever is left dead.
        glitch: null
    };
    Module.flixAudio = audio;

    // A hard saturating curve: what turns clean saws into a rasp.
    audio.clip = new Float32Array(1024);
    for (let i = 0; i < 1024; ++i) {
        const x = (i * 2) / 1023 - 1;
        audio.clip[i] = (61 * x) / (1 + 60 * Math.abs(x));
    }

    // Runs `make(ctx, at)` to schedule a sound starting at `at` -- now, on the
    // context's clock -- or not at all while the context is still asleep: a
    // suspended context's clock is stopped, so anything scheduled on it would
    // go off whenever it next woke, seconds late, over whatever the screen
    // had moved on to.
    audio.play = (make) => {
        const ctx = audio.context;
        if (ctx && ctx.state === 'running') make(ctx, ctx.currentTime + 0.005);
    };

    // Two seconds of noise, made once per kind and looped: white for `hold` 1,
    // or each value held `hold` samples, for the crunch of a lower sample rate.
    audio.noise = (ctx, hold) => {
        if (!audio.noises[hold]) {
            const length = Math.floor(ctx.sampleRate * 2);
            const buffer = ctx.createBuffer(1, length, ctx.sampleRate);
            const samples = buffer.getChannelData(0);
            let value = 0;
            for (let i = 0; i < length; ++i) {
                if (i % hold === 0) value = Math.random() * 2 - 1;
                samples[i] = value;
            }
            audio.noises[hold] = buffer;
        }
        return audio.noises[hold];
    };

    // A fresh way out to the speakers for one sound: a gain at `level` into a
    // fast, hard compressor, so layers stacked on the same instant hit hard
    // without clipping. Let go once the sound is over, at `until` on the
    // context's clock.
    audio.output = (ctx, level, until) => {
        const limiter = ctx.createDynamicsCompressor();
        limiter.threshold.value = -12;
        limiter.knee.value = 3;
        limiter.ratio.value = 12;
        limiter.attack.value = 0.001;
        limiter.release.value = 0.2;
        limiter.connect(ctx.destination);
        const gain = ctx.createGain();
        gain.gain.value = level;
        gain.connect(limiter);
        setTimeout(() => limiter.disconnect(), (until - ctx.currentTime) * 1000 + 250);
        return gain;
    };

    // How loud a tone or burst is over its life, from `o`: up to `o.peak` over
    // `o.attack` from `o.at`, then dying away by `o.at + o.length` -- or, when
    // `o.flat`, held at the peak and gated off at the end, the way a digital
    // glitch starts and stops.
    audio.shape = (param, o) => {
        if (o.flat) {
            param.setValueAtTime(o.peak, o.at);
            param.setValueAtTime(o.peak, o.at + o.length - 0.003);
            param.linearRampToValueAtTime(0, o.at + o.length);
        } else {
            param.setValueAtTime(0.0001, o.at);
            param.exponentialRampToValueAtTime(o.peak, o.at + (o.attack || 0.002));
            param.exponentialRampToValueAtTime(0.0001, o.at + o.length);
        }
    };

    // One tone into `into`: an `o.type` wave at `o.freq`, gliding to `o.to`
    // over its length when given one, shaped as audio.shape says.
    audio.tone = (ctx, into, o) => {
        const osc = ctx.createOscillator();
        osc.type = o.type;
        osc.frequency.setValueAtTime(o.freq, o.at);
        if (o.to) osc.frequency.exponentialRampToValueAtTime(o.to, o.at + o.length);
        const gain = ctx.createGain();
        audio.shape(gain.gain, o);
        osc.connect(gain);
        gain.connect(into);
        osc.start(o.at);
        osc.stop(o.at + o.length + 0.02);
    };

    // One burst of noise into `into`: the `o.hold` kind (see audio.noise)
    // through an `o.filter` at `o.freq`, shaped as audio.shape says. Started
    // somewhere random in the loop, so no two bursts are the same noise.
    audio.burst = (ctx, into, o) => {
        const source = ctx.createBufferSource();
        source.buffer = audio.noise(ctx, o.hold || 1);
        source.loop = true;
        const filter = ctx.createBiquadFilter();
        filter.type = o.filter;
        filter.frequency.value = o.freq;
        filter.Q.value = o.q || 0.7;
        const gain = ctx.createGain();
        audio.shape(gain.gain, o);
        source.connect(filter);
        filter.connect(gain);
        gain.connect(into);
        source.start(o.at, Math.random() * 1.5);
        source.stop(o.at + o.length + 0.02);
    };

    // Paper balled up and dropped in the bin: a run of dry crackles, loud and
    // thinning, and the soft thump of it landing.
    audio.crumple = (ctx, into, at) => {
        let t = at;
        for (let i = 0; i < 14; ++i) {
            audio.burst(ctx, into, {
                filter: 'bandpass', freq: 1500 + Math.random() * 3500, q: 1 + Math.random() * 2,
                at: t, length: 0.01 + Math.random() * 0.03,
                peak: (0.25 + Math.random() * 0.45) * (1 - i / 18)
            });
            t += 0.006 + Math.random() * 0.028;
        }
        audio.tone(ctx, into, { type: 'sine', freq: 170, to: 70, at: t + 0.03, length: 0.12, peak: 0.3 });
    };

    // The desktop's chord, Eb with an added ninth, into `into` from `at` until
    // `until`: each note a triangle with a quiet sine an octave over it, a few
    // cents apart, so it shimmers. A slow wow and a fast flutter, `wobble`
    // cents deep, run through every voice like a tape that is not well. The
    // oscillators come back, for the caller to bend.
    audio.chord = (ctx, into, at, until, wobble) => {
        const voices = [];
        const wow = ctx.createOscillator();
        wow.frequency.value = 0.55;
        const wowDepth = ctx.createGain();
        wowDepth.gain.value = wobble;
        wow.connect(wowDepth);
        const flutter = ctx.createOscillator();
        flutter.frequency.value = 6.5;
        const flutterDepth = ctx.createGain();
        flutterDepth.gain.value = wobble * 0.25;
        flutter.connect(flutterDepth);
        for (const note of [155.56, 233.08, 311.13, 349.23, 392.0, 466.16]) {
            for (const [type, ratio, level] of [['triangle', 1, 0.11], ['sine', 2, 0.035]]) {
                const osc = ctx.createOscillator();
                osc.type = type;
                osc.frequency.value = note * ratio;
                osc.detune.value = (Math.random() - 0.5) * 12;
                wowDepth.connect(osc.detune);
                flutterDepth.connect(osc.detune);
                const gain = ctx.createGain();
                gain.gain.value = level;
                osc.connect(gain);
                gain.connect(into);
                osc.start(at);
                osc.stop(until);
                voices.push(osc);
            }
        }
        for (const lfo of [wow, flutter]) {
            lfo.start(at);
            lfo.stop(until);
        }
        return voices;
    };

    if (!Context) return;
    const wake = () => {
        try {
            if (!audio.context) audio.context = new Context();
            if (audio.context.state !== 'running') {
                const resumed = audio.context.resume();
                if (resumed && resumed.catch) resumed.catch(() => {});
            }
        } catch (e) {
            // A browser that refuses even inside a gesture just stays silent.
        }
    };
    for (const type of ['pointerdown', 'pointerup', 'touchend', 'keydown']) {
        window.addEventListener(type, wake, true);
    }
});

EM_JS(void, flix_audio_jumpscare, (double lunge, double hold, double fade), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, start) => {
        const peak = start + Math.min(lunge, hold);   // the face fills the screen
        const release = start + hold;                 // the picture starts to fade
        const end = release + fade;                   // and is gone
        const tail = end + 0.25;                      // the scream rings a beat past it
        const silent = 0.0001;                        // exponential ramps cannot reach 0
        const master = audio.output(ctx, 0.8, tail);

        // The shape the slam and the shriek follow, as the picture does: there
        // at once, settling to `level` by the time the face fills the screen,
        // held while it shakes, then gone with the fade by `until`.
        const envelope = (param, attack, level, until) => {
            param.setValueAtTime(silent, start);
            param.exponentialRampToValueAtTime(attack, start + 0.004);
            param.exponentialRampToValueAtTime(level, peak);
            param.setValueAtTime(level, release);
            param.exponentialRampToValueAtTime(silent, until);
        };

        // The thud: a sine dropping from a knock to a sub-bass boom, the
        // impact of the face hitting the glass. Gone well before the fade.
        audio.tone(ctx, master, { type: 'sine', freq: 150, to: 36, at: start, attack: 0.001, length: 0.6, peak: 1.0 });

        // The slam: a burst of noise through a band that sweeps down as it
        // fades, bright on the hit and dull as it lets go.
        const hiss = ctx.createBufferSource();
        hiss.buffer = audio.noise(ctx, 1);
        hiss.loop = true;
        const band = ctx.createBiquadFilter();
        band.type = 'bandpass';
        band.Q.value = 0.9;
        band.frequency.setValueAtTime(3200, start);
        band.frequency.exponentialRampToValueAtTime(1400, peak);
        band.frequency.setValueAtTime(1400, release);
        band.frequency.exponentialRampToValueAtTime(450, end);
        const hissGain = ctx.createGain();
        envelope(hissGain.gain, 0.9, 0.35, end);
        hiss.connect(band);
        band.connect(hissGain);
        hissGain.connect(master);
        hiss.start(start);
        hiss.stop(end);

        // The shriek: saws a semitone apart in two octaves, which no ear hears
        // as a chord, warbled fast enough to rasp and driven through the
        // clipper. The pitch climbs as the face rushes in, the way a thing
        // coming at you does, holds while it stares, and sags away under the
        // fade.
        const crush = ctx.createWaveShaper();
        crush.curve = audio.clip;
        crush.oversample = '4x';
        const thin = ctx.createBiquadFilter();
        thin.type = 'highpass';
        thin.frequency.value = 500;
        const shriekGain = ctx.createGain();
        envelope(shriekGain.gain, 0.45, 0.32, tail);
        crush.connect(thin);
        thin.connect(shriekGain);
        shriekGain.connect(master);

        const warble = ctx.createOscillator();
        warble.frequency.value = 31;
        const warbleDepth = ctx.createGain();
        warbleDepth.gain.value = 70;   // cents either way
        warble.connect(warbleDepth);
        warble.start(start);
        warble.stop(tail);

        for (const pitch of [622, 659, 932, 988, 1397]) {
            const voice = ctx.createOscillator();
            voice.type = 'sawtooth';
            voice.frequency.setValueAtTime(pitch * 0.55, start);
            voice.frequency.exponentialRampToValueAtTime(pitch, peak);
            voice.frequency.setValueAtTime(pitch, release);
            voice.frequency.exponentialRampToValueAtTime(pitch * 0.5, tail);
            // A few cents apart each, so the five beat against one another.
            voice.detune.value = (Math.random() - 0.5) * 30;
            warbleDepth.connect(voice.detune);
            const voiceGain = ctx.createGain();
            voiceGain.gain.value = 0.3;
            voice.connect(voiceGain);
            voiceGain.connect(crush);
            voice.start(start);
            voice.stop(tail);
        }
    });
});

EM_JS(void, flix_audio_glitch, (double tear, double bsod, double hold), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, start) => {
        const cut = start + tear;   // the blue screen cuts in
        const full = cut + bsod;    // 100% complete
        const end = full + hold;    // the crash, or the restart
        // Kept, so the PowerCut that ends the crash cuts whatever is left at
        // that moment; the stops below are only the backstop.
        const out = audio.output(ctx, 0.8, end + 0.2);
        audio.glitch = out;

        // ACT ONE -- the tear. Under it a low electrical buzz, clipped, that
        // grows as the screen comes apart and goes with it.
        const buzz = ctx.createGain();
        buzz.gain.setValueAtTime(0.03, start);
        buzz.gain.linearRampToValueAtTime(0.2, cut - 0.004);
        buzz.gain.linearRampToValueAtTime(0, cut);
        const buzzTone = ctx.createBiquadFilter();
        buzzTone.type = 'lowpass';
        buzzTone.frequency.value = 1400;
        const buzzClip = ctx.createWaveShaper();
        buzzClip.curve = audio.clip;
        buzzClip.connect(buzzTone);
        buzzTone.connect(buzz);
        buzz.connect(out);
        for (const [type, freq] of [['sawtooth', 55], ['square', 58.3]]) {
            const osc = ctx.createOscillator();
            osc.type = type;
            osc.frequency.value = freq;
            const drive = ctx.createGain();
            drive.gain.value = 0.6;
            osc.connect(drive);
            drive.connect(buzzClip);
            osc.start(start);
            osc.stop(cut + 0.01);
        }

        // Over it the stutter, on the picture's own re-roll clock: sixteen
        // ticks a second, doubling for the last moment before the cut. Each
        // tick is a fresh roll of what gets through -- crushed noise, a data
        // blip, both, or nothing -- more of it and louder as the tear goes on.
        const holds = [1, 3, 8, 24];
        let t = start;
        while (t < cut - 0.008) {
            const p = (t - start) / tear;
            const tick = cut - t < 0.3 ? 1 / 32 : 1 / 16;
            const length = Math.max(0.008, Math.min(tick, cut - t) * (0.45 + Math.random() * 0.55));
            if (Math.random() < 0.35 + 0.55 * p) {
                audio.burst(ctx, out, {
                    hold: holds[Math.floor(Math.random() * holds.length)],
                    filter: 'bandpass', freq: 250 * Math.pow(20, Math.random()),
                    q: 0.7 + Math.random() * 4,
                    at: t, length: length, peak: 0.2 + Math.random() * 0.5 * (0.4 + p), flat: true
                });
            }
            if (Math.random() < 0.15 + 0.35 * p) {
                const freq = 150 * Math.pow(20, Math.random());
                audio.tone(ctx, out, {
                    type: 'square', freq: freq, to: freq * (Math.random() < 0.5 ? 0.5 : 2),
                    at: t, length: length, peak: 0.05 + 0.1 * p, flat: true
                });
            }
            t += tick;
        }
        // The cut: one click, and then nothing.
        audio.burst(ctx, out, { filter: 'highpass', freq: 1200, at: cut, length: 0.015, peak: 0.6 });

        // ACT TWO -- the stop screen. A beat of dead silence, then the hum of
        // a machine that is still on and the hard disk ticking as it
        // "collects some error info", until 100%; then only the hum, waiting.
        const hum = ctx.createGain();
        hum.gain.value = 0.0001;
        hum.gain.setValueAtTime(0.0001, cut + 0.6);
        hum.gain.exponentialRampToValueAtTime(1, cut + 2.2);
        hum.connect(out);
        for (const [freq, level] of [[60, 0.06], [120, 0.04], [180, 0.025], [300, 0.01]]) {
            const osc = ctx.createOscillator();
            osc.frequency.value = freq;
            const gain = ctx.createGain();
            gain.gain.value = level;
            osc.connect(gain);
            gain.connect(hum);
            osc.start(cut + 0.6);
            osc.stop(end + 0.1);
        }
        // The disk: seeks in clusters of a few ticks, each with the faint
        // thunk of the head arm under it, a fraction of a second apart.
        let seek = cut + 1.0;
        while (seek < full) {
            const clicks = 2 + Math.floor(Math.random() * 5);
            for (let i = 0; i < clicks && seek < full; ++i) {
                audio.burst(ctx, out, {
                    filter: 'highpass', freq: 2500, at: seek, length: 0.006,
                    peak: 0.1 + Math.random() * 0.12
                });
                audio.tone(ctx, out, { type: 'sine', freq: 190, at: seek, attack: 0.001, length: 0.025, peak: 0.07 });
                seek += 0.025 + Math.random() * 0.05;
            }
            seek += 0.12 + Math.random() * 0.5;
        }
    });
});

EM_JS(void, flix_audio_power_cut, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // Whatever the glitch is still playing goes with the power.
        if (audio.glitch) {
            audio.glitch.gain.setTargetAtTime(0, at, 0.003);
            audio.glitch = null;
        }
        const out = audio.output(ctx, 0.9, at + 1.4);
        // The speakers' pop, and the thump of everything stopping at once.
        audio.burst(ctx, out, { filter: 'lowpass', freq: 3000, q: 0.5, at: at, length: 0.04, peak: 0.8 });
        audio.tone(ctx, out, { type: 'sine', freq: 110, to: 30, at: at, length: 0.3, peak: 0.9 });
        // The hum winding down to nothing as the fans do, and the screen's
        // last high whine collapsing.
        for (const [freq, level] of [[60, 0.14], [120, 0.09], [180, 0.05]]) {
            audio.tone(ctx, out, {
                type: 'sine', freq: freq, to: freq * 0.25, at: at, attack: 0.01, length: 1.3, peak: level
            });
        }
        audio.tone(ctx, out, { type: 'sine', freq: 9000, to: 300, at: at, length: 0.18, peak: 0.05 });
    });
});

EM_JS(void, flix_audio_desktop_startup, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        const until = at + 3.4;
        const out = audio.output(ctx, 0.8, until);

        // A breath of noise rising into the chord, as a machine spins up.
        const air = ctx.createBufferSource();
        air.buffer = audio.noise(ctx, 1);
        air.loop = true;
        const airBand = ctx.createBiquadFilter();
        airBand.type = 'bandpass';
        airBand.Q.value = 1.2;
        airBand.frequency.setValueAtTime(300, at);
        airBand.frequency.exponentialRampToValueAtTime(3500, at + 1.0);
        const airGain = ctx.createGain();
        airGain.gain.setValueAtTime(0.0001, at);
        airGain.gain.exponentialRampToValueAtTime(0.12, at + 0.8);
        airGain.gain.exponentialRampToValueAtTime(0.0001, at + 1.4);
        air.connect(airBand);
        airBand.connect(airGain);
        airGain.connect(out);
        air.start(at);
        air.stop(at + 1.45);

        // The chord swelling out of nothing -- and twice, a few frames apart,
        // dropping out like a scratched disc, before it sags flat and fades.
        const tone = ctx.createBiquadFilter();
        tone.type = 'lowpass';
        tone.frequency.value = 2400;
        const swell = ctx.createGain();
        swell.gain.setValueAtTime(0.0001, at);
        swell.gain.exponentialRampToValueAtTime(1, at + 0.9);
        swell.gain.setValueAtTime(0.0001, at + 1.55);
        swell.gain.setValueAtTime(1, at + 1.61);
        swell.gain.setValueAtTime(0.0001, at + 1.68);
        swell.gain.setValueAtTime(1, at + 1.72);
        swell.gain.exponentialRampToValueAtTime(0.0001, until);
        tone.connect(swell);
        swell.connect(out);
        for (const voice of audio.chord(ctx, tone, at, until, 20)) {
            const freq = voice.frequency.value;
            voice.frequency.setValueAtTime(freq, at + 1.75);
            voice.frequency.exponentialRampToValueAtTime(freq * 0.94, until);
        }
    });
});

EM_JS(void, flix_audio_desktop_shutdown, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // The same chord, winding down an octave and muffling as it goes, the
        // way a machine sounds as it loses power.
        const until = at + 2.4;
        const out = audio.output(ctx, 0.8, until);
        const tone = ctx.createBiquadFilter();
        tone.type = 'lowpass';
        tone.frequency.setValueAtTime(2400, at);
        tone.frequency.exponentialRampToValueAtTime(500, until);
        const swell = ctx.createGain();
        swell.gain.setValueAtTime(0.0001, at);
        swell.gain.exponentialRampToValueAtTime(1, at + 0.12);
        swell.gain.exponentialRampToValueAtTime(0.0001, until);
        tone.connect(swell);
        swell.connect(out);
        for (const voice of audio.chord(ctx, tone, at, until, 30)) {
            const freq = voice.frequency.value;
            voice.frequency.setValueAtTime(freq, at + 0.25);
            voice.frequency.exponentialRampToValueAtTime(freq * 0.5, until);
        }
    });
});

EM_JS(void, flix_audio_balloon, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // Two soft bubbles, the second a fifth over the first.
        const out = audio.output(ctx, 0.6, at + 0.3);
        audio.tone(ctx, out, { type: 'sine', freq: 740, to: 988, at: at, attack: 0.004, length: 0.09, peak: 0.3 });
        audio.tone(ctx, out, {
            type: 'sine', freq: 1109, to: 1480, at: at + 0.075, attack: 0.004, length: 0.16, peak: 0.3
        });
    });
});

EM_JS(void, flix_audio_error, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // One flat blow: two notes a tritone apart over the octave below.
        const out = audio.output(ctx, 0.7, at + 0.6);
        for (const freq of [329.63, 466.16]) {
            audio.tone(ctx, out, { type: 'triangle', freq: freq, at: at, attack: 0.004, length: 0.5, peak: 0.3 });
        }
        audio.tone(ctx, out, { type: 'sine', freq: 164.81, at: at, attack: 0.004, length: 0.5, peak: 0.3 });
    });
});

EM_JS(void, flix_audio_notice, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // A small bell: a sine and two of a bell's off-key partials over it,
        // the higher dying first.
        const out = audio.output(ctx, 0.6, at + 0.8);
        for (const [ratio, level, length] of [[1, 0.5, 0.7], [2.76, 0.14, 0.35], [5.4, 0.05, 0.2]]) {
            audio.tone(ctx, out, { type: 'sine', freq: 880 * ratio, at: at, length: length, peak: level });
        }
    });
});

EM_JS(void, flix_audio_question, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // Two soft notes stepping down a third, as a question trails off.
        const out = audio.output(ctx, 0.6, at + 0.5);
        audio.tone(ctx, out, { type: 'sine', freq: 784, at: at, attack: 0.004, length: 0.25, peak: 0.25 });
        audio.tone(ctx, out, { type: 'sine', freq: 622.25, at: at + 0.12, attack: 0.004, length: 0.3, peak: 0.25 });
    });
});

EM_JS(void, flix_audio_corruption_error, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // The error's blow, dirty and skipping: struck four times in a row,
        // each a semitone lower with a crunch of noise, and the last one
        // dragging down to half its pitch like a record stopping.
        const out = audio.output(ctx, 0.7, at + 1.4);
        const dirt = ctx.createWaveShaper();
        dirt.curve = audio.clip;
        const dirtGain = ctx.createGain();
        dirtGain.gain.value = 0.5;
        dirt.connect(dirtGain);
        dirtGain.connect(out);
        for (let i = 0; i < 4; ++i) {
            const t = at + i * 0.085;
            const drop = Math.pow(2, -i / 12);
            const last = i === 3;
            for (const freq of [329.63, 466.16]) {
                audio.tone(ctx, dirt, {
                    type: 'triangle', freq: freq * drop, to: last ? freq * drop * 0.5 : 0,
                    at: t, attack: 0.003, length: last ? 0.9 : 0.08, peak: 0.4
                });
            }
            audio.burst(ctx, out, {
                hold: 12, filter: 'bandpass', freq: 1800, q: 1, at: t, length: 0.05, peak: 0.3, flat: true
            });
        }
    });
});

EM_JS(void, flix_audio_navigate, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // A dry little click.
        const out = audio.output(ctx, 0.7, at + 0.1);
        audio.burst(ctx, out, { filter: 'bandpass', freq: 3500, q: 0.8, at: at, length: 0.012, peak: 1.0 });
        audio.tone(ctx, out, { type: 'sine', freq: 1800, at: at, attack: 0.001, length: 0.02, peak: 0.12 });
    });
});

EM_JS(void, flix_audio_recycle, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        audio.crumple(ctx, audio.output(ctx, 0.7, at + 0.7), at);
    });
});

EM_JS(void, flix_audio_purge, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        const out = audio.output(ctx, 0.8, at + 1.0);
        audio.crumple(ctx, out, at);
        // And the thing inside it dying: the jumpscare's clipped shriek, small
        // and falling away to nothing.
        const dirt = ctx.createWaveShaper();
        dirt.curve = audio.clip;
        const thin = ctx.createBiquadFilter();
        thin.type = 'highpass';
        thin.frequency.value = 300;
        const fall = ctx.createGain();
        fall.gain.setValueAtTime(0.0001, at);
        fall.gain.exponentialRampToValueAtTime(0.3, at + 0.01);
        fall.gain.exponentialRampToValueAtTime(0.0001, at + 0.75);
        dirt.connect(thin);
        thin.connect(fall);
        fall.connect(out);
        for (const pitch of [880, 932, 1318]) {
            const voice = ctx.createOscillator();
            voice.type = 'sawtooth';
            voice.frequency.setValueAtTime(pitch, at);
            voice.frequency.exponentialRampToValueAtTime(pitch * 0.12, at + 0.75);
            const drive = ctx.createGain();
            drive.gain.value = 0.3;
            voice.connect(drive);
            drive.connect(dirt);
            voice.start(at);
            voice.stop(at + 0.8);
        }
    });
});

EM_JS(void, flix_audio_all_clear, (), {
    const audio = Module.flixAudio;
    if (!audio) return;
    audio.play((ctx, at) => {
        // One clean major chord, struck up from the bottom, with no wobble in
        // it at all: whatever was wrong with this computer has gone.
        const out = audio.output(ctx, 0.7, at + 2.0);
        const notes = [523.25, 659.25, 783.99, 1046.5];
        for (let i = 0; i < notes.length; ++i) {
            const t = at + i * 0.09;
            audio.tone(ctx, out, { type: 'triangle', freq: notes[i], at: t, attack: 0.01, length: 1.6, peak: 0.18 });
            audio.tone(ctx, out, { type: 'sine', freq: notes[i] * 2, at: t, attack: 0.01, length: 0.9, peak: 0.05 });
        }
    });
});

void installAudio() { flix_audio_install(); }

void playSound(Sound sound) {
    switch (sound) {
        case Sound::PowerCut: flix_audio_power_cut(); break;
        case Sound::DesktopStartup: flix_audio_desktop_startup(); break;
        case Sound::DesktopShutdown: flix_audio_desktop_shutdown(); break;
        case Sound::Balloon: flix_audio_balloon(); break;
        case Sound::Error: flix_audio_error(); break;
        case Sound::Notice: flix_audio_notice(); break;
        case Sound::Question: flix_audio_question(); break;
        case Sound::CorruptionError: flix_audio_corruption_error(); break;
        case Sound::Navigate: flix_audio_navigate(); break;
        case Sound::Recycle: flix_audio_recycle(); break;
        case Sound::Purge: flix_audio_purge(); break;
        case Sound::AllClear: flix_audio_all_clear(); break;
    }
}

void playJumpscareSound(double lungeSeconds, double holdSeconds, double fadeSeconds) {
    flix_audio_jumpscare(lungeSeconds, holdSeconds, fadeSeconds);
}

void playGlitchSound(double tearSeconds, double bsodSeconds, double holdSeconds) {
    flix_audio_glitch(tearSeconds, bsodSeconds, holdSeconds);
}

#else

void installAudio() {}
void playSound(Sound) {}
void playJumpscareSound(double, double, double) {}
void playGlitchSound(double, double, double) {}

#endif

} // namespace flix::web
