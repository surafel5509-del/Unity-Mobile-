// PRISM ENGINE — audio tests: WAV I/O, envelopes, 3D attenuation, Doppler,
// panning, the lock-free ring buffer, the mixer and the procedural synth.
#include "prism_test.h"
#include "prism/audio/audio.h"
#include <cmath>
#include <memory>

using namespace prism;
using namespace prism::audio;

namespace {
std::shared_ptr<Clip> make_tone(f32 hz, f32 seconds, f32 gain = 0.6f, i32 rate = 44100) {
    auto c = std::make_shared<Clip>(synth::tone(hz, seconds, rate, gain));
    c->name = "tone_" + std::to_string(static_cast<int>(hz));
    return c;
}
i32 zero_crossings(const std::vector<f32>& s) {
    i32 n = 0;
    for (std::size_t i = 1; i < s.size(); ++i)
        if ((s[i - 1] < 0.0f && s[i] >= 0.0f) || (s[i - 1] > 0.0f && s[i] <= 0.0f)) ++n;
    return n;
}
} // namespace

PRISM_TEST(audio_envelope_adsr_shape) {
    Envelope e;
    e.attack = 0.1f; e.decay = 0.2f; e.sustain = 0.5f; e.release = 0.3f;
    PRISM_CHECK_NEAR(e.value(0.0f), 0.0f, 1e-5);
    PRISM_CHECK_NEAR(e.value(0.05f), 0.5f, 1e-5);      // mid-attack
    PRISM_CHECK_NEAR(e.value(0.1f), 1.0f, 1e-5);       // attack peak
    PRISM_CHECK_NEAR(e.value(0.3f), 0.5f, 1e-5);       // end of decay = sustain
    PRISM_CHECK_NEAR(e.value(10.0f), 0.5f, 1e-5);      // steady sustain
    // release fades from the captured level to zero
    PRISM_CHECK_NEAR(e.release_value(0.0f, 0.5f), 0.5f, 1e-5);
    PRISM_CHECK_NEAR(e.release_value(0.15f, 0.5f), 0.25f, 1e-5);
    PRISM_CHECK_NEAR(e.release_value(0.3f, 0.5f), 0.0f, 1e-5);
    PRISM_CHECK(e.release_finished(0.3f));
    PRISM_CHECK(!e.release_finished(0.2f));
    // zero-length release cuts immediately rather than dividing by zero
    Envelope instant; instant.release = 0.0f;
    PRISM_CHECK_NEAR(instant.release_value(0.0f, 1.0f), 0.0f, 1e-6);
}

PRISM_TEST(audio_wav_roundtrip_and_rejection) {
    Clip src = synth::tone(440.0f, 0.05f, 44100, 0.5f);
    src.channels = 1;
    auto bytes = wav_encode(src);
    PRISM_CHECK(bytes.size() > 44);

    WavInfo info = wav_inspect(bytes.data(), bytes.size());
    PRISM_CHECK(info.valid);
    PRISM_CHECK_STR(info.error, "");
    PRISM_CHECK_EQ(info.sample_rate, 44100);
    PRISM_CHECK_EQ(info.channels, 1);
    PRISM_CHECK_EQ(info.bits_per_sample, 16);
    PRISM_CHECK(info.format == SampleFormat::PCM_S16);

    Clip back;
    std::string err;
    PRISM_CHECK(wav_decode(bytes.data(), bytes.size(), back, &err));
    PRISM_CHECK_STR(err, "");
    PRISM_CHECK_EQ(static_cast<int>(back.frame_count()), static_cast<int>(src.frame_count()));
    // 16-bit quantisation: max error is half an LSB
    f32 worst = 0;
    for (std::size_t i = 0; i < src.samples.size(); ++i)
        worst = std::max(worst, std::fabs(back.samples[i] - src.samples[i]));
    PRISM_CHECK(worst < 2.0f / 32768.0f);

    // Rejects: not a RIFF file, truncated file, bad format tag
    const u8 junk[64] = {0};
    PRISM_CHECK(!wav_inspect(junk, sizeof(junk)).valid);
    PRISM_CHECK(!wav_inspect(bytes.data(), 8).valid);
    PRISM_CHECK(!wav_inspect(bytes.data(), bytes.size() / 2).valid);
    auto mutated = bytes;
    mutated[8] = 'X';   // destroy the WAVE tag
    WavInfo bad = wav_inspect(mutated.data(), mutated.size());
    PRISM_CHECK(!bad.valid);
    PRISM_CHECK(bad.error.find("WAVE") != std::string::npos);
    Clip none;
    PRISM_CHECK(!wav_decode(mutated.data(), mutated.size(), none));
}

PRISM_TEST(audio_attenuation_models) {
    // Inverse-distance-clamped saturates at both ends.
    PRISM_CHECK_NEAR(attenuation(AttenuationModel::InverseDistanceClamped, 0.5f, 1.0f, 20.0f), 1.0f, 1e-5);
    PRISM_CHECK_NEAR(attenuation(AttenuationModel::InverseDistanceClamped, 2.0f, 1.0f, 20.0f), 0.5f, 1e-5);
    PRISM_CHECK_NEAR(attenuation(AttenuationModel::InverseDistanceClamped, 100.0f, 1.0f, 20.0f), 0.05f, 1e-5);
    // Monotonically decreasing across the useful range
    f32 prev = 2.0f;
    for (f32 d = 1.0f; d <= 20.0f; d += 1.0f) {
        f32 a = attenuation(AttenuationModel::InverseDistanceClamped, d, 1.0f, 20.0f);
        PRISM_CHECK(a <= prev + 1e-6f);
        prev = a;
    }
    // Linear-clamped reaches exactly zero at max distance
    PRISM_CHECK_NEAR(attenuation(AttenuationModel::LinearDistanceClamped, 20.0f, 1.0f, 20.0f), 0.0f, 1e-5);
    PRISM_CHECK_NEAR(attenuation(AttenuationModel::LinearDistanceClamped, 10.5f, 1.0f, 20.0f), 0.5f, 1e-5);
    PRISM_CHECK_NEAR(attenuation(AttenuationModel::None, 1234.0f, 1.0f, 20.0f), 1.0f, 1e-6);
    // Every model must be finite and non-negative
    for (auto m : {AttenuationModel::None, AttenuationModel::InverseDistance,
                   AttenuationModel::InverseDistanceClamped, AttenuationModel::LinearDistance,
                   AttenuationModel::LinearDistanceClamped, AttenuationModel::ExponentialDistance}) {
        for (f32 d = 0.0f; d <= 40.0f; d += 2.5f) {
            f32 a = attenuation(m, d, 1.0f, 30.0f);
            PRISM_CHECK(std::isfinite(a) && a >= 0.0f);
        }
    }
}

PRISM_TEST(audio_doppler_shift) {
    const math::Vec3 lp(0, 0, 0), lv(0, 0, 0);
    const math::Vec3 sp(0, 0, -10);
    // stationary
    PRISM_CHECK_NEAR(doppler_factor(lp, lv, sp, math::Vec3(0, 0, 0), 1.0f), 1.0f, 1e-5);
    // source moving toward the listener (velocity along +z, listener at origin)
    f32 approach = doppler_factor(lp, lv, sp, math::Vec3(0, 0, 100), 1.0f);
    f32 recede = doppler_factor(lp, lv, sp, math::Vec3(0, 0, -100), 1.0f);
    PRISM_CHECK(approach > 1.0f);
    PRISM_CHECK(recede < 1.0f);
    // closed form for a 100 m/s approach at c = 343
    PRISM_CHECK_NEAR(approach, 343.0f / (343.0f - 100.0f), 1e-3);
    PRISM_CHECK_NEAR(recede, 343.0f / (343.0f + 100.0f), 1e-3);
    // doppler_level 0 disables the effect entirely
    PRISM_CHECK_NEAR(doppler_factor(lp, lv, sp, math::Vec3(0, 0, 300), 0.0f), 1.0f, 1e-6);
    // the result is always clamped to a sane musical range
    f32 extreme = doppler_factor(lp, lv, sp, math::Vec3(0, 0, 100000), 1.0f);
    PRISM_CHECK(extreme <= 4.0f && extreme >= 0.25f);
}

PRISM_TEST(audio_constant_power_panning) {
    for (f32 p = -1.0f; p <= 1.0f; p += 0.1f) {
        f32 l, r;
        pan_gains(p, l, r);
        PRISM_CHECK_NEAR(l * l + r * r, 1.0f, 1e-4);   // constant power
        PRISM_CHECK(l >= 0.0f && r >= 0.0f);
    }
    f32 l, r;
    pan_gains(-1.0f, l, r);
    PRISM_CHECK_NEAR(l, 1.0f, 1e-5);
    PRISM_CHECK_NEAR(r, 0.0f, 1e-5);
    pan_gains(1.0f, l, r);
    PRISM_CHECK_NEAR(l, 0.0f, 1e-5);
    PRISM_CHECK_NEAR(r, 1.0f, 1e-5);
    pan_gains(0.0f, l, r);
    PRISM_CHECK_NEAR(l, r, 1e-5);
    // out-of-range input is clamped, not wrapped
    pan_gains(99.0f, l, r);
    PRISM_CHECK_NEAR(r, 1.0f, 1e-5);
}

PRISM_TEST(audio_ring_buffer_is_lockfree_spsc) {
    RingBuffer<int, 8> rb;                     // usable capacity is 7
    PRISM_CHECK(rb.empty());
    PRISM_CHECK_EQ(static_cast<int>(rb.capacity()), 7);
    for (int i = 1; i <= 7; ++i) PRISM_CHECK(rb.push(i));
    PRISM_CHECK(!rb.push(99));                 // full
    PRISM_CHECK_EQ(static_cast<int>(rb.size()), 7);
    int v = 0;
    for (int i = 1; i <= 7; ++i) { PRISM_CHECK(rb.pop(v)); PRISM_CHECK_EQ(v, i); }
    PRISM_CHECK(!rb.pop(v));                   // empty
    // wrap-around still preserves order
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 5; ++i) PRISM_CHECK(rb.push(round * 10 + i));
        for (int i = 0; i < 5; ++i) { PRISM_CHECK(rb.pop(v)); PRISM_CHECK_EQ(v, round * 10 + i); }
    }
    rb.push(1); rb.push(2); rb.push(3);
    std::vector<int> drained;
    rb.drain(drained);
    PRISM_CHECK_EQ(static_cast<int>(drained.size()), 3);
    PRISM_CHECK(rb.empty());
}

PRISM_TEST(audio_mixer_plays_and_finishes_voices) {
    Mixer::Config cfg;
    cfg.sample_rate = 44100;
    cfg.frame_size = 512;
    cfg.enable_reverb = false;
    Mixer m(cfg);
    PRISM_CHECK_EQ(static_cast<int>(m.bus_count()), 4);   // master/music/sfx/voice

    const u32 hash = m.register_clip(make_tone(440.0f, 0.05f));
    PRISM_CHECK(hash != kInvalidId);
    PRISM_CHECK(m.clip(hash) != nullptr);
    PRISM_CHECK(m.register_clip(nullptr) == kInvalidId);
    PRISM_CHECK(m.play(kInvalidId, VoiceParams{}) == kInvalidId);   // unknown clip

    VoiceParams p;
    p.volume = 0.8f;
    p.envelope.attack = 0.001f;
    p.envelope.decay = 0.01f;
    p.envelope.sustain = 1.0f;
    p.envelope.release = 0.01f;
    const u32 vid = m.play(hash, p);
    PRISM_CHECK(vid != kInvalidId);
    PRISM_CHECK_EQ(static_cast<int>(m.active_voice_count()), 1);

    // 0.05 s at 44.1 kHz = 2205 frames; mix until the voice ends
    int guard = 0;
    f32 peak = 0.0f;
    while (m.active_voice_count() > 0 && guard++ < 64) {
        auto buf = m.mix_frames(512);
        for (f32 s : buf) peak = std::max(peak, std::fabs(s));
    }
    PRISM_CHECK(peak > 0.05f);                 // actually produced audio
    PRISM_CHECK(peak <= 1.0f);                 // never clipped past full scale
    PRISM_CHECK(m.stats().voices_finished >= 1);
    PRISM_CHECK(m.stats().frames_mixed > 0);

    // stop_all clears everything
    m.play(hash, p);
    m.play(hash, p);
    PRISM_CHECK(m.active_voice_count() >= 2);
    m.stop_all();
    PRISM_CHECK_EQ(static_cast<int>(m.active_voice_count()), 0);
}

PRISM_TEST(audio_mixer_steals_voices_when_saturated) {
    Mixer m;
    const u32 hash = m.register_clip(make_tone(220.0f, 5.0f));
    VoiceParams p;
    p.loop = true;
    p.envelope.sustain = 1.0f;
    for (int i = 0; i < static_cast<int>(kMaxVoices) + 12; ++i) m.play(hash, p);
    PRISM_CHECK(m.active_voice_count() <= static_cast<u32>(kMaxVoices));
    PRISM_CHECK(m.stats().voices_stolen >= 12);
    PRISM_CHECK(m.voices().size() == static_cast<std::size_t>(kMaxVoices));
}

PRISM_TEST(audio_bus_hierarchy_and_mute) {
    Mixer m;
    // buses created by the ctor: 0=master(1.0) 1=music(0.8) 2=sfx(1.0) 3=voice(1.0)
    PRISM_CHECK_NEAR(m.bus_gain(1), 0.8f, 1e-5);
    const u32 ambient = m.create_bus("ambient", 1 /* music */, 0.5f);
    PRISM_CHECK_NEAR(m.bus_gain(ambient), 0.4f, 1e-5);          // 1.0 * 0.8 * 0.5
    m.bus(1)->volume = 0.25f;
    PRISM_CHECK_NEAR(m.bus_gain(ambient), 0.125f, 1e-5);
    m.bus(0)->muted = true;
    PRISM_CHECK_NEAR(m.bus_gain(ambient), 0.0f, 1e-6);          // muting master silences all
    m.bus(0)->muted = false;
    PRISM_CHECK(m.bus(9999) == nullptr);
    PRISM_CHECK_NEAR(m.bus_gain(9999), 1.0f, 1e-6);
}

PRISM_TEST(audio_spatial_voice_attenuates_with_distance) {
    Mixer::Config cfg; cfg.enable_reverb = false;
    Mixer m(cfg);
    const u32 hash = m.register_clip(make_tone(440.0f, 2.0f));

    Listener l; l.position = math::Vec3(0, 0, 0); l.forward = math::Vec3(0, 0, -1); l.up = math::Vec3(0, 1, 0);
    m.set_listener(l);

    auto rms = [&](math::Vec3 pos) {
        m.stop_all();
        VoiceParams p;
        p.spatial = true;
        p.position = pos;
        p.min_distance = 1.0f;
        p.max_distance = 50.0f;
        p.model = AttenuationModel::InverseDistanceClamped;
        p.envelope.attack = 0.0f; p.envelope.decay = 0.0f; p.envelope.sustain = 1.0f;
        m.play(hash, p);
        auto buf = m.mix_frames(256);
        f64 acc = 0;
        for (f32 s : buf) acc += static_cast<f64>(s) * s;
        return std::sqrt(acc / std::max<std::size_t>(1, buf.size()));
    };
    const f64 near_rms = rms(math::Vec3(0, 0, -1));
    const f64 mid_rms = rms(math::Vec3(0, 0, -10));
    const f64 far_rms = rms(math::Vec3(0, 0, -50));
    PRISM_CHECK(near_rms > mid_rms);
    PRISM_CHECK(mid_rms > far_rms);
    // A source hard right must be louder in the right channel than the left.
    m.stop_all();
    VoiceParams p;
    p.spatial = true; p.position = math::Vec3(5, 0, -5);
    p.min_distance = 1.0f; p.max_distance = 50.0f;
    p.envelope.attack = 0.0f; p.envelope.decay = 0.0f; p.envelope.sustain = 1.0f;
    m.play(hash, p);
    auto buf = m.mix_frames(64);
    f64 left = 0, right = 0;
    for (std::size_t i = 0; i + 1 < buf.size(); i += 2) {
        left += std::fabs(buf[i]); right += std::fabs(buf[i + 1]);
    }
    PRISM_CHECK(right > left);
}

PRISM_TEST(audio_reverb_produces_a_tail) {
    Reverb rv;
    rv.configure(Reverb::preset_hall());
    rv.reset();
    // impulse
    f32 out0 = rv.process(1.0f);
    PRISM_CHECK(std::isfinite(out0));
    // feed silence and confirm energy persists (the tail)
    f64 tail = 0;
    for (int i = 0; i < 4000; ++i) tail += std::fabs(rv.process(0.0f));
    PRISM_CHECK(tail > 0.01);
    // a bigger room rings longer than a small one
    Reverb small; small.configure(Reverb::preset_room()); small.reset();
    Reverb cave;  cave.configure(Reverb::preset_cave());  cave.reset();
    small.process(1.0f); cave.process(1.0f);
    f64 t_small = 0, t_cave = 0;
    for (int i = 0; i < 8000; ++i) { t_small += std::fabs(small.process(0.0f)); t_cave += std::fabs(cave.process(0.0f)); }
    PRISM_CHECK(t_cave > t_small);
    // output stays bounded
    Reverb r2; r2.configure(Reverb::preset_hall()); r2.reset();
    for (int i = 0; i < 1000; ++i) {
        f32 v = r2.process(std::sin(i * 0.05f));
        PRISM_CHECK(std::isfinite(v) && std::fabs(v) < 10.0f);
    }
    PRISM_CHECK_STR(r2.preset().name, "hall");
}

PRISM_TEST(audio_synth_generates_expected_content) {
    Clip t = synth::tone(440.0f, 0.1f, 44100, 0.5f);
    PRISM_CHECK_EQ(static_cast<int>(t.frame_count()), 4410);
    PRISM_CHECK_NEAR(t.duration_seconds(), 0.1, 1e-4);
    // 440 Hz for 0.1 s = 44 cycles = 88 zero crossings
    const i32 zc = zero_crossings(t.samples);
    PRISM_CHECK(zc >= 86 && zc <= 90);
    f32 peak = 0;
    for (f32 s : t.samples) peak = std::max(peak, std::fabs(s));
    PRISM_CHECK_NEAR(peak, 0.5f, 1e-4);

    Clip sw = synth::sweep(100.0f, 2000.0f, 0.2f, 44100, 0.5f);
    PRISM_CHECK_EQ(static_cast<int>(sw.frame_count()), 8820);
    // an upward sweep crosses zero far more often than a 100 Hz tone would
    PRISM_CHECK(zero_crossings(sw.samples) > 40);

    Clip n = synth::noise(0.01f, 7, 44100, 0.3f);
    f64 mean = 0;
    for (f32 s : n.samples) mean += s;
    mean /= static_cast<f64>(n.samples.size());
    PRISM_CHECK(std::fabs(mean) < 0.05);       // roughly zero-mean

    Clip ch = synth::chord({220.0f, 277.0f, 330.0f}, 0.05f, 44100, 0.6f);
    PRISM_CHECK_EQ(static_cast<int>(ch.channels), 1);
    for (f32 s : ch.samples) PRISM_CHECK(std::fabs(s) <= 1.0f);

    Clip im = synth::impact(120.0f, 0.2f, 44100, 0.6f);
    // an impact must decay: the last 10% is much quieter than the first 10%
    const std::size_t n10 = im.samples.size() / 10;
    f64 head = 0, tail = 0;
    for (std::size_t i = 0; i < n10; ++i) head += std::fabs(im.samples[i]);
    for (std::size_t i = im.samples.size() - n10; i < im.samples.size(); ++i) tail += std::fabs(im.samples[i]);
    PRISM_CHECK(tail < head * 0.5);
}
