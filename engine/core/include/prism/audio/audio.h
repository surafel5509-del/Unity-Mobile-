// =====================================================================
//  PRISM ENGINE — audio/audio.h
//  Fully offline audio: decoder, mixer, buses, 3D spatialisation,
//  Doppler, reverb and a procedural synth. Nothing is streamed from a
//  network; clips come from APK assets.
//
//  Signal path:
//    Clip -> Voice(ADSR, pitch, filter) -> Bus(sfx/music/voice)
//         -> Master -> Reverb send -> Device ring buffer
//  3D: per-voice distance attenuation + Doppler + constant-power pan,
//  computed on the audio thread from the state the game thread publishes.
// =====================================================================
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::audio {

inline constexpr f32 kReferenceDistance = 1.0f;
inline constexpr u32 kMaxVoices = 64;

enum class SampleFormat : u8 { PCM_U8, PCM_S16, PCM_S24, PCM_S32, Float32 };
enum class AttenuationModel : u8 { None, InverseDistance, InverseDistanceClamped,
                                 LinearDistance, LinearDistanceClamped, ExponentialDistance };

// ------------------------------------------------------------- clip --------
/// Decoded, interleaved float PCM in [-1,1].
struct Clip {
    std::string name;
    i32 sample_rate = 44100;
    i32 channels = 2;
    std::vector<f32> samples;      // interleaved
    bool loop = false;

    [[nodiscard]] std::size_t frame_count() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
    [[nodiscard]] f32 duration_seconds() const {
        return sample_rate > 0 ? static_cast<f32>(frame_count()) / static_cast<f32>(sample_rate) : 0.0f;
    }
};

struct WavInfo {
    bool valid = false;
    i32 sample_rate = 0;
    i32 channels = 0;
    i32 bits_per_sample = 0;
    SampleFormat format = SampleFormat::PCM_S16;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;
    std::string error;
};

/// Parses a RIFF/WAVE header. Reports the exact reason on failure.
[[nodiscard]] WavInfo wav_inspect(const u8* data, std::size_t len);
/// Decodes PCM 8/16/24/32 and IEEE float WAV into a Clip.
[[nodiscard]] bool wav_decode(const u8* data, std::size_t len, Clip& out, std::string* error = nullptr);
/// Encodes a Clip back to 16-bit PCM WAV (used by the editor's preview export).
[[nodiscard]] std::vector<u8> wav_encode(const Clip& clip);

// ------------------------------------------------------ ring buffer --------
/// Single-producer / single-consumer lock-free ring buffer. The game thread
/// pushes parameter updates; the audio thread pops them. No mutex, no
/// priority inversion on Android's audio callback.
template <typename T, std::size_t Capacity>
class RingBuffer {
public:
    bool push(const T& v) {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) % Capacity;
        if (next == read_.load(std::memory_order_acquire)) return false;   // full
        slots_[w] = v;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& out) {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;      // empty
        out = slots_[r];
        read_.store((r + 1) % Capacity, std::memory_order_release);
        return true;
    }
    void drain(std::vector<T>& out) { T v; while (pop(v)) out.push_back(v); }
    [[nodiscard]] std::size_t size() const {
        const std::size_t w = write_.load(std::memory_order_acquire);
        const std::size_t r = read_.load(std::memory_order_acquire);
        return (w + Capacity - r) % Capacity;
    }
    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] static std::size_t capacity() { return Capacity - 1; }
private:
    T slots_[Capacity]{};
    std::atomic<std::size_t> write_{0}, read_{0};
};

// ------------------------------------------------------------ voice --------
struct Envelope {
    f32 attack = 0.005f, decay = 0.08f, sustain = 0.7f, release = 0.25f;
    /// Attack/decay/sustain level, `t` seconds after note-on.
    [[nodiscard]] f32 value(f32 t) const;
    /// Linear fade from `start_level` to 0 over `release`, `t` seconds after note-off.
    [[nodiscard]] f32 release_value(f32 t, f32 start_level) const;
    [[nodiscard]] bool release_finished(f32 t) const { return t >= release; }
};

struct OnePoleFilter {
    f32 coefficient = 0.5f;      // 0 = fully closed, 1 = bypass
    f32 state = 0.0f;
    f32 process(f32 in) {
        state += coefficient * (in - state);
        return state;
    }
    void reset() { state = 0.0f; }
};

struct VoiceParams {
    u32 id = 0;
    u32 clip_hash = 0;
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    f32 pan = 0.0f;              // -1 left .. +1 right
    bool loop = false;
    bool spatial = false;
    math::Vec3 position;
    f32 min_distance = kReferenceDistance;
    f32 max_distance = 50.0f;
    AttenuationModel model = AttenuationModel::InverseDistanceClamped;
    f32 doppler_level = 1.0f;
    f32 reverb_send = 0.0f;
    u32 bus = 0;                 // index into the bus list; 0 = "master"
    Envelope envelope;
    f32 lowpass = 1.0f;          // 0..1 (muffled when occluded)
};

enum class VoiceState : u8 { Stopped, Attacking, Sustained, Releasing, Done };

struct Voice {
    VoiceParams params;
    VoiceState state = VoiceState::Stopped;
    f32 cursor = 0.0f;           // fractional sample position
    f32 age = 0.0f;
    f32 release_age = 0.0f;
    f32 gain = 0.0f;
    f32 gain_at_release = 0.0f;  // envelope level captured at note-off
    f32 distance = 0.0f;
    f32 doppler = 1.0f;
    math::Vec3 velocity;
    OnePoleFilter filter_l, filter_r;
    f32 last_l = 0.0f, last_r = 0.0f;   // for zipper-noise smoothing
};

// -------------------------------------------------------- attenuation ------
[[nodiscard]] f32 attenuation(AttenuationModel model, f32 distance, f32 min_d, f32 max_d);
/// Doppler shift factor from relative radial velocity. `speed_of_sound`
/// defaults to 343 m/s; mobile games usually exaggerate it.
[[nodiscard]] f32 doppler_factor(math::Vec3 listener_pos, math::Vec3 listener_vel,
                                 math::Vec3 source_pos, math::Vec3 source_vel,
                                 f32 doppler_level, f32 speed_of_sound = 343.0f);
/// Constant-power stereo pan from a normalised -1..1 panner value.
void pan_gains(f32 pan, f32& left, f32& right);

// ------------------------------------------------------------- bus ---------
struct Bus {
    std::string name;
    f32 volume = 1.0f;
    bool muted = false;
    u32 parent = kInvalidId;
    f32 reverb_send = 0.0f;
    [[nodiscard]] f32 effective_volume() const { return muted ? 0.0f : volume; }
};

// ------------------------------------------------------------ reverb -------
/// Schroeder reverb: 4 parallel comb filters into 2 series all-pass filters.
/// Small, deterministic and cheap enough for a mid-range phone.
class Reverb {
public:
    struct Preset {
        std::string name = "room";
        f32 room_size = 0.7f;
        f32 damping = 0.4f;
        f32 wet = 0.25f;
        f32 dry = 0.9f;
        i32 sample_rate = 44100;
    };
    static Preset preset_room();
    static Preset preset_hall();
    static Preset preset_cave();

    void configure(Preset p);
    void reset();
    f32 process(f32 in);
    [[nodiscard]] const Preset& preset() const { return preset_; }

private:
    Preset preset_;
    std::vector<f32> comb_[4];
    std::vector<f32> ap_[2];
    std::size_t comb_pos_[4] = {0,0,0,0};
    std::size_t ap_pos_[2] = {0,0};
    std::size_t comb_len_[4] = {0,0,0,0};
    std::size_t ap_len_[2] = {0,0};
    f32 comb_fb_[4] = {0,0,0,0};
    f32 comb_lpf_[4] = {0,0,0,0};   // one-pole state per comb (NOT per all-pass)
};

// ------------------------------------------------------- listener ----------
struct Listener {
    math::Vec3 position;
    math::Vec3 velocity;
    math::Vec3 forward{0, 0, -1};
    math::Vec3 up{0, 1, 0};
    f32 gain = 1.0f;
};

// ------------------------------------------------------------ mixer --------
struct MixerStats {
    u32 active_voices = 0, voices_started = 0, voices_finished = 0, voices_stolen = 0;
    u64 frames_mixed = 0;
    u32 clips_loaded = 0;
    i32 underruns = 0;
    f64 mix_ms = 0;
    f32 peak_amplitude = 0.0f;
};

class Mixer {
public:
    struct Config {
        i32 sample_rate = 44100;
        i32 frame_size = 512;         // frames per mix call (Android low-latency ~96..1024)
        i32 channels = 2;
        f32 master_volume = 1.0f;
        bool enable_reverb = true;
        f32 speed_of_sound = 343.0f;
        f32 doppler_level = 1.0f;
    };

    Mixer();
    explicit Mixer(const Config& cfg);

    // ---- clip registry (offline: everything is registered from APK assets) --
    u32 register_clip(std::shared_ptr<Clip> clip);
    [[nodiscard]] std::shared_ptr<Clip> clip(u32 hash) const;
    [[nodiscard]] std::size_t clip_count() const { return clips_.size(); }

    // ---- buses -------------------------------------------------------------
    u32 create_bus(const std::string& name, u32 parent = kInvalidId, f32 volume = 1.0f);
    Bus* bus(u32 id);
    [[nodiscard]] f32 bus_gain(u32 id) const;      // product up the parent chain
    [[nodiscard]] std::size_t bus_count() const { return buses_.size(); }

    // ---- voices ------------------------------------------------------------
    /// Plays a clip. Steals the quietest voice if the pool is exhausted.
    u32 play(u32 clip_hash, const VoiceParams& p, u32 bus_id = kInvalidId);
    void stop(u32 voice_id, bool immediate = false);
    void stop_all();
    [[nodiscard]] u32 active_voice_count() const;
    void set_listener(const Listener& l) { listener_ = l; }
    [[nodiscard]] const Listener& listener() const { return listener_; }

    // ---- mixing ------------------------------------------------------------
    /// Mixes `frame_size` frames of interleaved output. Returns frames written.
    std::size_t mix(f32* out, std::size_t frames);
    /// Convenience: mix into a std::vector.
    [[nodiscard]] std::vector<f32> mix_frames(i32 frames);

    [[nodiscard]] const MixerStats& stats() const { return stats_; }
    [[nodiscard]] const Config& config() const { return cfg_; }
    void set_master_volume(f32 v) { cfg_.master_volume = math::clampf(v, 0.0f, 4.0f); }
    [[nodiscard]] const std::vector<Voice>& voices() const { return voices_; }

private:
    void update_spatial(Voice& v);
    f32 read_sample(const Clip& c, f32 cursor, i32 channel) const;
    u32 steal_voice();

    Config cfg_;
    Listener listener_;
    std::vector<std::pair<u32, std::shared_ptr<Clip>>> clips_;
    std::vector<Bus> buses_;
    std::vector<Voice> voices_;
    std::vector<f32> reverb_buffer_;
    Reverb reverb_;
    MixerStats stats_;
    u32 next_voice_id_ = 1;
    u32 next_bus_id_ = 1;
};

// ------------------------------------------------------- procedural --------
/// Offline SFX generation — lets a game ship sound without any asset file.
namespace synth {
[[nodiscard]] Clip tone(f32 hz, f32 seconds, i32 sample_rate = 44100, f32 gain = 0.5f);
[[nodiscard]] Clip sweep(f32 hz_start, f32 hz_end, f32 seconds, i32 sample_rate = 44100, f32 gain = 0.5f);
[[nodiscard]] Clip noise(f32 seconds, u32 seed = 12345, i32 sample_rate = 44100, f32 gain = 0.3f);
[[nodiscard]] Clip chord(const std::vector<f32>& hz, f32 seconds, i32 sample_rate = 44100, f32 gain = 0.3f);
/// Percussive hit: exponential-decay noise burst with a pitch drop.
[[nodiscard]] Clip impact(f32 hz, f32 seconds, i32 sample_rate = 44100, f32 gain = 0.6f);
} // namespace synth

} // namespace prism::audio
