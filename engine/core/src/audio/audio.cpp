// PRISM ENGINE — audio/audio.cpp
#include "prism/audio/audio.h"
#include "prism/core/hash.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace prism::audio {

// ------------------------------------------------------------- envelope ----
f32 Envelope::value(f32 t) const {
    if (attack > kEpsilon && t < attack) return t / attack;
    if (decay > kEpsilon && t < attack + decay) {
        const f32 k = (t - attack) / decay;
        return math::lerpf(1.0f, sustain, math::clampf(k, 0.0f, 1.0f));
    }
    return sustain;
}

f32 Envelope::release_value(f32 t, f32 start_level) const {
    if (release <= kEpsilon) return 0.0f;
    const f32 k = math::clampf(t / release, 0.0f, 1.0f);
    return start_level * (1.0f - k);
}

// ------------------------------------------------------------- WAV I/O -----
namespace {
u32 fourcc(const char* s) {
    return static_cast<u32>(static_cast<u8>(s[0])) | (static_cast<u32>(static_cast<u8>(s[1])) << 8) |
           (static_cast<u32>(static_cast<u8>(s[2])) << 16) | (static_cast<u32>(static_cast<u8>(s[3])) << 24);
}
u32 rd_u32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
u16 rd_u16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
void wr_u32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x)); v.push_back(static_cast<u8>(x >> 8));
    v.push_back(static_cast<u8>(x >> 16)); v.push_back(static_cast<u8>(x >> 24));
}
void wr_u16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x)); v.push_back(static_cast<u8>(x >> 8));
}
void wr_str(std::vector<u8>& v, const char* s) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>(s[i]));
}
} // namespace

WavInfo wav_inspect(const u8* data, std::size_t len) {
    WavInfo info;
    if (len < 12) { info.error = "file too small for a RIFF header"; return info; }
    if (rd_u32(data) != fourcc("RIFF")) { info.error = "missing RIFF tag"; return info; }
    if (rd_u32(data + 8) != fourcc("WAVE")) { info.error = "missing WAVE tag"; return info; }

    std::size_t pos = 12;
    bool have_fmt = false;
    while (pos + 8 <= len) {
        const u32 id = rd_u32(data + pos);
        const u32 size = rd_u32(data + pos + 4);
        const std::size_t body = pos + 8;
        if (body + size > len) { info.error = "chunk overruns end of file"; return info; }
        if (id == fourcc("fmt ")) {
            if (size < 16) { info.error = "fmt chunk too small"; return info; }
            const u16 tag = rd_u16(data + body);
            info.channels = static_cast<i32>(rd_u16(data + body + 2));
            info.sample_rate = static_cast<i32>(rd_u32(data + body + 4));
            info.bits_per_sample = static_cast<i32>(rd_u16(data + body + 14));
            if (tag == 1) {
                info.format = info.bits_per_sample == 8 ? SampleFormat::PCM_U8
                            : info.bits_per_sample == 16 ? SampleFormat::PCM_S16
                            : info.bits_per_sample == 24 ? SampleFormat::PCM_S24
                            : info.bits_per_sample == 32 ? SampleFormat::PCM_S32
                                                         : SampleFormat::PCM_S16;
            } else if (tag == 3) {
                info.format = SampleFormat::Float32;
            } else {
                info.error = "unsupported WAV format tag " + std::to_string(tag) +
                             " (PRISM decodes PCM and IEEE float only)";
                return info;
            }
            have_fmt = true;
        } else if (id == fourcc("data")) {
            info.data_offset = body;
            info.data_size = size;
        }
        // chunks are word-aligned
        pos = body + size + (size & 1u);
    }
    if (!have_fmt) { info.error = "no fmt chunk"; return info; }
    if (info.data_size == 0) { info.error = "no data chunk"; return info; }
    if (info.channels < 1 || info.channels > 8) { info.error = "unsupported channel count"; return info; }
    if (info.sample_rate < 4000 || info.sample_rate > 384000) { info.error = "implausible sample rate"; return info; }
    info.valid = true;
    return info;
}

bool wav_decode(const u8* data, std::size_t len, Clip& out, std::string* error) {
    WavInfo info = wav_inspect(data, len);
    if (!info.valid) { if (error) *error = info.error; return false; }

    const i32 bytes_per_sample = info.bits_per_sample / 8;
    const std::size_t frame_bytes = static_cast<std::size_t>(bytes_per_sample) * info.channels;
    if (frame_bytes == 0) { if (error) *error = "zero-byte audio frame"; return false; }
    const std::size_t frames = std::min<std::size_t>(info.data_size / frame_bytes,
                                                     (len - info.data_offset) / frame_bytes);
    out.sample_rate = info.sample_rate;
    out.channels = info.channels;
    out.samples.resize(frames * static_cast<std::size_t>(info.channels));

    const u8* src = data + info.data_offset;
    std::size_t o = 0;
    for (std::size_t f = 0; f < frames; ++f) {
        for (i32 c = 0; c < info.channels; ++c) {
            const u8* p = src + f * frame_bytes + static_cast<std::size_t>(c) * bytes_per_sample;
            f32 v = 0.0f;
            switch (info.format) {
                case SampleFormat::PCM_U8:
                    v = (static_cast<i32>(p[0]) - 128) / 128.0f;
                    break;
                case SampleFormat::PCM_S16:
                    v = static_cast<i16>(static_cast<u16>(p[0] | (p[1] << 8))) / 32768.0f;
                    break;
                case SampleFormat::PCM_S24: {
                    i32 raw = static_cast<i32>(p[0]) | (static_cast<i32>(p[1]) << 8) |
                              (static_cast<i32>(p[2]) << 16);
                    if (raw & 0x800000) raw |= ~0xFFFFFF;    // sign-extend
                    v = static_cast<f32>(raw) / 8388608.0f;
                    break;
                }
                case SampleFormat::PCM_S32: {
                    i32 raw = static_cast<i32>(rd_u32(p));
                    v = static_cast<f32>(raw) / 2147483648.0f;
                    break;
                }
                case SampleFormat::Float32: {
                    u32 bits = rd_u32(p);
                    std::memcpy(&v, &bits, sizeof(v));
                    break;
                }
            }
            out.samples[o++] = math::clampf(v, -1.0f, 1.0f);
        }
    }
    if (error) error->clear();
    return true;
}

std::vector<u8> wav_encode(const Clip& clip) {
    std::vector<u8> out;
    const u32 channels = static_cast<u32>(std::max(1, clip.channels));
    const u32 rate = static_cast<u32>(std::max(1, clip.sample_rate));
    const u32 data_size = static_cast<u32>(clip.samples.size() * 2);

    wr_str(out, "RIFF");
    wr_u32(out, 36 + data_size);
    wr_str(out, "WAVE");
    wr_str(out, "fmt ");
    wr_u32(out, 16);
    wr_u16(out, 1);                       // PCM
    wr_u16(out, static_cast<u16>(channels));
    wr_u32(out, rate);
    wr_u32(out, rate * channels * 2);     // byte rate
    wr_u16(out, static_cast<u16>(channels * 2));
    wr_u16(out, 16);
    wr_str(out, "data");
    wr_u32(out, data_size);
    for (f32 s : clip.samples) {
        i32 q = static_cast<i32>(std::lround(math::clampf(s, -1.0f, 1.0f) * 32767.0f));
        wr_u16(out, static_cast<u16>(static_cast<i16>(q)));
    }
    return out;
}

// ---------------------------------------------------------- attenuation ----
f32 attenuation(AttenuationModel model, f32 distance, f32 min_d, f32 max_d) {
    const f32 d = std::max(0.0f, distance);
    const f32 lo = std::max(kEpsilon, min_d);
    const f32 hi = std::max(lo + kEpsilon, max_d);
    switch (model) {
        case AttenuationModel::None:
            return 1.0f;
        case AttenuationModel::InverseDistance:
            return lo / std::max(d, kEpsilon);
        case AttenuationModel::InverseDistanceClamped:
            return lo / math::clampf(d, lo, hi);
        case AttenuationModel::LinearDistance:
            // Unclamped: OpenAL lets this go negative past max distance, but a
            // negative gain inverts phase, so floor it at silence.
            return std::max(0.0f, 1.0f - (d - lo) / (hi - lo));
        case AttenuationModel::LinearDistanceClamped:
            return 1.0f - (math::clampf(d, lo, hi) - lo) / (hi - lo);
        case AttenuationModel::ExponentialDistance:
            return std::pow(std::max(d, kEpsilon) / lo, -1.0f);
    }
    return 1.0f;
}

f32 doppler_factor(math::Vec3 lp, math::Vec3 lv, math::Vec3 sp, math::Vec3 sv,
                   f32 doppler_level, f32 speed_of_sound) {
    if (doppler_level <= kEpsilon) return 1.0f;
    const math::Vec3 to_listener = (lp - sp);
    const f32 dist = to_listener.length();
    if (dist < kEpsilon) return 1.0f;
    const math::Vec3 axis = to_listener / dist;
    // Positive radial velocity = approaching = higher pitch.
    const f32 v_source = sv.dot(axis);
    const f32 v_listener = lv.dot(axis);
    const f32 c = std::max(1.0f, speed_of_sound);
    const f32 vs = math::clampf(v_source * doppler_level, -0.9f * c, 0.9f * c);
    const f32 vl = math::clampf(v_listener * doppler_level, -0.9f * c, 0.9f * c);
    const f32 f = (c - vl) / (c - vs);
    return math::clampf(f, 0.25f, 4.0f);
}

void pan_gains(f32 pan, f32& left, f32& right) {
    // Constant-power law: keeps perceived loudness stable across the field.
    const f32 p = math::clampf(pan, -1.0f, 1.0f);
    const f32 angle = (p + 1.0f) * 0.25f * math::Pi;   // 0 .. pi/2
    left = std::cos(angle);
    right = std::sin(angle);
}

// ------------------------------------------------------------- reverb ------
Reverb::Preset Reverb::preset_room() { return Preset{"room", 0.62f, 0.35f, 0.20f, 0.95f, 44100}; }
Reverb::Preset Reverb::preset_hall() { return Preset{"hall", 0.88f, 0.55f, 0.35f, 0.90f, 44100}; }
Reverb::Preset Reverb::preset_cave() { return Preset{"cave", 0.95f, 0.20f, 0.45f, 0.85f, 44100}; }

void Reverb::configure(Preset p) {
    preset_ = p;
    const i32 sr = std::max(8000, p.sample_rate);
    // Classic Schroeder delay lengths, in milliseconds.
    const f32 comb_ms[4] = {29.7f, 37.1f, 41.1f, 43.7f};
    const f32 ap_ms[2]   = {5.0f, 1.7f};
    for (int i = 0; i < 4; ++i) {
        comb_len_[i] = static_cast<std::size_t>(std::max(1.0f, comb_ms[i] * sr / 1000.0f));
        comb_[i].assign(comb_len_[i], 0.0f);
        comb_pos_[i] = 0;
        comb_fb_[i] = math::clampf(p.room_size, 0.0f, 0.98f);
        comb_lpf_[i] = 0.0f;
    }
    for (int i = 0; i < 2; ++i) {
        ap_len_[i] = static_cast<std::size_t>(std::max(1.0f, ap_ms[i] * sr / 1000.0f));
        ap_[i].assign(ap_len_[i], 0.0f);
        ap_pos_[i] = 0;
    }
}

void Reverb::reset() {
    for (int i = 0; i < 4; ++i) {
        std::fill(comb_[i].begin(), comb_[i].end(), 0.0f);
        comb_pos_[i] = 0; comb_lpf_[i] = 0.0f;
    }
    for (int i = 0; i < 2; ++i) { std::fill(ap_[i].begin(), ap_[i].end(), 0.0f); ap_pos_[i] = 0; }
}

f32 Reverb::process(f32 in) {
    if (comb_len_[0] == 0) configure(preset_);
    const f32 damp = math::clampf(preset_.damping, 0.0f, 1.0f);
    f32 sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        auto& buf = comb_[i];
        const std::size_t pos = comb_pos_[i];
        const f32 delayed = buf[pos];
        // one-pole low-pass in the feedback loop = high-frequency decay
        comb_lpf_[i] = delayed * (1.0f - damp) + comb_lpf_[i] * damp;
        buf[pos] = in + comb_lpf_[i] * comb_fb_[i];
        comb_pos_[i] = (pos + 1) % comb_len_[i];
        sum += delayed;
    }
    f32 out = sum * 0.25f;
    const f32 ap_g = 0.5f;
    for (int i = 0; i < 2; ++i) {
        auto& buf = ap_[i];
        const std::size_t pos = ap_pos_[i];
        const f32 delayed = buf[pos];
        buf[pos] = out + delayed * ap_g;
        out = delayed - out * ap_g;
        ap_pos_[i] = (pos + 1) % ap_len_[i];
    }
    return out * preset_.wet + in * preset_.dry;
}

// --------------------------------------------------------------- mixer -----
Mixer::Mixer() : Mixer(Config{}) {}

Mixer::Mixer(const Config& cfg) : cfg_(cfg) {
    voices_.resize(kMaxVoices);
    reverb_buffer_.assign(static_cast<std::size_t>(std::max(1, cfg.frame_size)), 0.0f);
    Reverb::Preset p = Reverb::preset_room();
    p.sample_rate = cfg.sample_rate;
    reverb_.configure(p);
    create_bus("master");
    create_bus("music", 0, 0.8f);
    create_bus("sfx", 0, 1.0f);
    create_bus("voice", 0, 1.0f);
}

u32 Mixer::register_clip(std::shared_ptr<Clip> clip) {
    if (!clip) return kInvalidId;
    if (clip->name.empty()) clip->name = "clip_" + std::to_string(clips_.size());
    const u32 hash = crypto::fnv1a_32(clip->name);
    for (auto& kv : clips_) if (kv.first == hash) { kv.second = clip; return hash; }
    clips_.emplace_back(hash, std::move(clip));
    ++stats_.clips_loaded;
    return hash;
}

std::shared_ptr<Clip> Mixer::clip(u32 hash) const {
    for (const auto& kv : clips_) if (kv.first == hash) return kv.second;
    return nullptr;
}

u32 Mixer::create_bus(const std::string& name, u32 parent, f32 volume) {
    Bus b;
    b.name = name;
    b.volume = volume;
    b.parent = parent;
    const u32 id = static_cast<u32>(buses_.size());
    buses_.push_back(b);
    next_bus_id_ = id + 1;
    return id;
}

Bus* Mixer::bus(u32 id) {
    return id < buses_.size() ? &buses_[id] : nullptr;
}

f32 Mixer::bus_gain(u32 id) const {
    f32 gain = 1.0f;
    u32 cur = id;
    for (int guard = 0; cur < buses_.size() && guard < 16; ++guard) {
        const Bus& b = buses_[cur];
        gain *= b.effective_volume();
        if (b.parent == kInvalidId || b.parent == cur) break;
        cur = b.parent;
    }
    return gain;
}

u32 Mixer::steal_voice() {
    // Take the quietest voice; ties go to the oldest.
    std::size_t best = 0;
    f32 best_level = std::numeric_limits<f32>::max();
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        const Voice& v = voices_[i];
        if (v.state == VoiceState::Stopped || v.state == VoiceState::Done) return static_cast<u32>(i);
        const f32 level = v.gain * v.params.volume;
        if (level < best_level) { best_level = level; best = i; }
    }
    ++stats_.voices_stolen;
    return static_cast<u32>(best);
}

u32 Mixer::play(u32 clip_hash, const VoiceParams& p, u32 bus_id) {
    if (!clip(clip_hash)) return kInvalidId;
    const u32 slot = steal_voice();
    Voice& v = voices_[slot];
    v = Voice{};
    v.params = p;
    v.params.clip_hash = clip_hash;
    if (v.params.id == 0) v.params.id = next_voice_id_++;
    v.state = VoiceState::Attacking;
    v.age = 0.0f;
    v.gain = 0.0f;
    v.gain_at_release = 0.0f;
    v.params.bus = (bus_id == kInvalidId) ? 0u : bus_id;
    update_spatial(v);
    ++stats_.voices_started;
    return v.params.id;
}

void Mixer::stop(u32 voice_id, bool immediate) {
    for (auto& v : voices_) {
        if (v.params.id != voice_id) continue;
        if (immediate) { v.state = VoiceState::Stopped; ++stats_.voices_finished; }
        else if (v.state != VoiceState::Releasing) {
            v.gain_at_release = v.gain;
            v.state = VoiceState::Releasing;
            v.release_age = 0.0f;
        }
        return;
    }
}

void Mixer::stop_all() {
    for (auto& v : voices_) if (v.state != VoiceState::Stopped) { v.state = VoiceState::Stopped; }
}

u32 Mixer::active_voice_count() const {
    u32 n = 0;
    for (const auto& v : voices_) if (v.state != VoiceState::Stopped && v.state != VoiceState::Done) ++n;
    return n;
}

void Mixer::update_spatial(Voice& v) {
    if (!v.params.spatial) { v.distance = 0.0f; v.doppler = 1.0f; return; }
    const math::Vec3 rel = v.params.position - listener_.position;
    v.distance = rel.length();
    v.doppler = doppler_factor(listener_.position, listener_.velocity,
                               v.params.position, v.velocity,
                               cfg_.doppler_level * v.params.doppler_level, cfg_.speed_of_sound);
    // Pan from the source's angle relative to the listener's facing direction.
    if (v.distance > kEpsilon) {
        math::Vec3 fwd = listener_.forward.normalized();
        math::Vec3 right = fwd.cross(listener_.up).normalized();
        const f32 side = (rel / v.distance).dot(right);
        v.params.pan = math::clampf(side, -1.0f, 1.0f);
    }
}

f32 Mixer::read_sample(const Clip& c, f32 cursor, i32 channel) const {
    const std::size_t frames = c.frame_count();
    if (frames == 0) return 0.0f;
    i64 i0 = static_cast<i64>(std::floor(cursor));
    if (c.loop) {
        const i64 n = static_cast<i64>(frames);
        i0 = ((i0 % n) + n) % n;
    } else if (i0 < 0 || i0 >= static_cast<i64>(frames)) {
        return 0.0f;
    }
    i64 i1 = i0 + 1;
    if (i1 >= static_cast<i64>(frames)) i1 = c.loop ? 0 : i0;
    const f32 frac = cursor - std::floor(cursor);
    const std::size_t stride = static_cast<std::size_t>(std::max(1, c.channels));
    const std::size_t ch = static_cast<std::size_t>(math::clampf(static_cast<f32>(channel), 0.0f,
                                                                 static_cast<f32>(stride - 1)));
    const f32 a = c.samples[static_cast<std::size_t>(i0) * stride + ch];
    const f32 b = c.samples[static_cast<std::size_t>(i1) * stride + ch];
    return a + (b - a) * frac;    // linear interpolation: cheap, no zipper noise
}

std::size_t Mixer::mix(f32* out, std::size_t frames) {
    const i32 ch = std::max(1, cfg_.channels);
    std::fill(out, out + frames * static_cast<std::size_t>(ch), 0.0f);
    std::fill(reverb_buffer_.begin(), reverb_buffer_.end(), 0.0f);
    if (reverb_buffer_.size() < frames) reverb_buffer_.resize(frames, 0.0f);

    const f32 dt = 1.0f / static_cast<f32>(std::max(1, cfg_.sample_rate));
    f32 peak = 0.0f;
    u32 active = 0;

    for (auto& v : voices_) {
        if (v.state == VoiceState::Stopped || v.state == VoiceState::Done) continue;
        auto c = clip(v.params.clip_hash);
        if (!c || c->samples.empty()) { v.state = VoiceState::Stopped; continue; }
        ++active;
        update_spatial(v);

        const f32 att = v.params.spatial
            ? attenuation(v.params.model, v.distance, v.params.min_distance, v.params.max_distance)
            : 1.0f;
        const f32 pitch = math::clampf(v.params.pitch * v.doppler, 0.05f, 8.0f);
        const f32 step = pitch * static_cast<f32>(c->sample_rate) / static_cast<f32>(cfg_.sample_rate);
        f32 left_gain, right_gain;
        pan_gains(v.params.pan, left_gain, right_gain);
        const f32 bus = bus_gain(v.params.bus);

        for (std::size_t f = 0; f < frames; ++f) {
            // envelope
            f32 env;
            if (v.state == VoiceState::Releasing) {
                env = v.params.envelope.release_value(v.release_age, v.gain_at_release);
                v.release_age += dt;
                if (v.params.envelope.release_finished(v.release_age)) {
                    v.state = VoiceState::Stopped;
                    ++stats_.voices_finished;
                    break;
                }
            } else {
                env = v.params.envelope.value(v.age);
                v.age += dt;
                if (v.state == VoiceState::Attacking &&
                    v.age >= v.params.envelope.attack + v.params.envelope.decay)
                    v.state = VoiceState::Sustained;
                if (!v.params.loop && !c->loop &&
                    v.cursor >= static_cast<f32>(c->frame_count())) {
                    v.state = VoiceState::Stopped;
                    ++stats_.voices_finished;
                    break;
                }
            }
            v.gain = env * att * v.params.volume * bus;

            const f32 mono = (c->channels == 1)
                ? read_sample(*c, v.cursor, 0)
                : 0.0f;
            f32 l, r;
            if (c->channels == 1) {
                l = mono * left_gain;
                r = mono * right_gain;
            } else {
                l = read_sample(*c, v.cursor, 0);
                r = read_sample(*c, v.cursor, 1);
                // fold a stereo clip's side information into the panner
                const f32 mid = (l + r) * 0.5f, side = (l - r) * 0.5f;
                l = mid * left_gain + side;
                r = mid * right_gain - side;
            }
            l *= v.gain; r *= v.gain;
            // one-pole low-pass per channel (occlusion / distance muffling)
            v.filter_l.coefficient = v.params.lowpass;
            v.filter_r.coefficient = v.params.lowpass;
            l = v.filter_l.process(l);
            r = v.filter_r.process(r);

            if (ch >= 2) { out[f * ch + 0] += l; out[f * ch + 1] += r; }
            else out[f] += (l + r) * 0.5f;

            if (cfg_.enable_reverb && v.params.reverb_send > kEpsilon)
                reverb_buffer_[f] += (l + r) * 0.5f * v.params.reverb_send * bus;

            v.cursor += step;
        }
    }

    if (cfg_.enable_reverb) {
        for (std::size_t f = 0; f < frames; ++f) {
            const f32 wet = reverb_.process(reverb_buffer_[f]);
            if (ch >= 2) { out[f * ch + 0] += wet * 0.5f; out[f * ch + 1] += wet * 0.5f; }
            else out[f] += wet;
        }
    }

    // master gain + soft clip so an overloaded bus degrades instead of wrapping
    const f32 master = cfg_.master_volume * listener_.gain;
    for (std::size_t i = 0; i < frames * static_cast<std::size_t>(ch); ++i) {
        f32 s = out[i] * master;
        s = std::tanh(s);
        out[i] = s;
        const f32 a = std::fabs(s);
        if (a > peak) peak = a;
    }

    stats_.active_voices = active;
    stats_.frames_mixed += frames;
    stats_.peak_amplitude = peak;
    return frames;
}

std::vector<f32> Mixer::mix_frames(i32 frames) {
    const std::size_t n = static_cast<std::size_t>(std::max(1, frames)) * std::max(1, cfg_.channels);
    std::vector<f32> buf(n, 0.0f);
    mix(buf.data(), static_cast<std::size_t>(std::max(1, frames)));
    return buf;
}

// ------------------------------------------------------------ procedural ---
namespace synth {

Clip tone(f32 hz, f32 seconds, i32 sample_rate, f32 gain) {
    Clip c;
    c.name = "tone";
    c.sample_rate = std::max(8000, sample_rate);
    c.channels = 1;
    const std::size_t n = static_cast<std::size_t>(seconds * c.sample_rate);
    c.samples.resize(n);
    const f32 w = math::TwoPi * hz / static_cast<f32>(c.sample_rate);
    for (std::size_t i = 0; i < n; ++i)
        c.samples[i] = std::sin(w * static_cast<f32>(i)) * gain;
    return c;
}

Clip sweep(f32 hz_start, f32 hz_end, f32 seconds, i32 sample_rate, f32 gain) {
    Clip c;
    c.name = "sweep";
    c.sample_rate = std::max(8000, sample_rate);
    c.channels = 1;
    const std::size_t n = static_cast<std::size_t>(seconds * c.sample_rate);
    c.samples.resize(n);
    f32 phase = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const f32 t = n > 1 ? static_cast<f32>(i) / static_cast<f32>(n - 1) : 0.0f;
        const f32 hz = math::lerpf(hz_start, hz_end, t);
        phase += math::TwoPi * hz / static_cast<f32>(c.sample_rate);
        if (phase > math::TwoPi) phase -= math::TwoPi;
        c.samples[i] = std::sin(phase) * gain;
    }
    return c;
}

Clip noise(f32 seconds, u32 seed, i32 sample_rate, f32 gain) {
    Clip c;
    c.name = "noise";
    c.sample_rate = std::max(8000, sample_rate);
    c.channels = 1;
    const std::size_t n = static_cast<std::size_t>(seconds * c.sample_rate);
    c.samples.resize(n);
    u32 s = seed ? seed : 1u;
    for (std::size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        c.samples[i] = (static_cast<f32>(s) / 2147483648.0f - 1.0f) * gain;
    }
    return c;
}

Clip chord(const std::vector<f32>& hz, f32 seconds, i32 sample_rate, f32 gain) {
    Clip c;
    c.name = "chord";
    c.sample_rate = std::max(8000, sample_rate);
    c.channels = 1;
    const std::size_t n = static_cast<std::size_t>(seconds * c.sample_rate);
    c.samples.resize(n);
    if (hz.empty()) return c;
    for (f32 f : hz) {
        const f32 w = math::TwoPi * f / static_cast<f32>(c.sample_rate);
        const f32 amp = gain / static_cast<f32>(hz.size());
        for (std::size_t i = 0; i < n; ++i) c.samples[i] += std::sin(w * static_cast<f32>(i)) * amp;
    }
    for (auto& s : c.samples) s = math::clampf(s, -1.0f, 1.0f);
    return c;
}

Clip impact(f32 hz, f32 seconds, i32 sample_rate, f32 gain) {
    Clip c;
    c.name = "impact";
    c.sample_rate = std::max(8000, sample_rate);
    c.channels = 1;
    const std::size_t n = static_cast<std::size_t>(seconds * c.sample_rate);
    c.samples.resize(n);
    u32 s = 0x1234ABCDu;
    f32 phase = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(c.sample_rate);
        const f32 decay = std::exp(-6.0f * t / std::max(seconds, 1e-3f));
        s = s * 1664525u + 1013904223u;
        const f32 nz = (static_cast<f32>(s) / 2147483648.0f - 1.0f);
        const f32 f = hz * (1.0f + 3.0f * std::exp(-20.0f * t));    // pitch drop
        phase += math::TwoPi * f / static_cast<f32>(c.sample_rate);
        if (phase > math::TwoPi) phase -= math::TwoPi;
        c.samples[i] = (std::sin(phase) * 0.7f + nz * 0.3f) * decay * gain;
    }
    return c;
}
} // namespace synth

} // namespace prism::audio
