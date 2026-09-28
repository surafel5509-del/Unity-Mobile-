// =====================================================================
//  PRISM ENGINE — net/net.h
//  LAN-only multiplayer. No cloud, no matchmaking, no accounts, no
//  internet permission in the default APK. Two devices on the same
//  Wi-Fi network / hotspot / Wi-Fi Direct group, or Bluetooth, and
//  nothing else.
//
//  Layers:
//    BitWriter/BitReader  — bit-packed serialisation (mobile bandwidth)
//    Packet               — sequenced datagram with a 32-bit ack bitmask
//    ITransport           — Loopback (tests/editor) and UDP (device LAN)
//    ReliableChannel      — retransmission, RTT + loss estimation
//    Session              — host/client roles, peer table, discovery
//    Rollback             — input delay, prediction, rollback + resimulate
//    LagCompensation      — historical transform sampling
//    SnapshotStream       — entity snapshot interpolation/extrapolation
// =====================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::net {

// -------------------------------------------------------- bit streams ------
class BitWriter {
public:
    void write_bits(u32 value, i32 bits);
    void write_bool(bool v) { write_bits(v ? 1u : 0u, 1); }
    void write_u8(u8 v)  { write_bits(v, 8); }
    void write_u16(u16 v){ write_bits(v, 16); }
    void write_u32(u32 v){ write_bits(v, 32); }
    void write_i32(i32 v){ write_bits(static_cast<u32>(v), 32); }
    /// Fixed-point float in [lo,hi] with `bits` of precision.
    void write_float(f32 v, f32 lo, f32 hi, i32 bits);
    void write_string(const std::string& s);      // u16 length + raw bytes
    void write_bytes(const u8* data, std::size_t n);
    void align_to_byte();

    [[nodiscard]] const std::vector<u8>& bytes() const { return buffer_; }
    [[nodiscard]] std::size_t bit_count() const { return bit_pos_; }
    [[nodiscard]] std::size_t byte_count() const { return buffer_.size(); }
private:
    std::vector<u8> buffer_;
    std::size_t bit_pos_ = 0;
};

class BitReader {
public:
    BitReader() = default;
    BitReader(const u8* data, std::size_t len) : data_(data), bits_(len * 8) {}
    u32 read_bits(i32 bits);
    bool read_bool() { return read_bits(1) != 0; }
    u8  read_u8()  { return static_cast<u8>(read_bits(8)); }
    u16 read_u16() { return static_cast<u16>(read_bits(16)); }
    u32 read_u32() { return read_bits(32); }
    i32 read_i32() { return static_cast<i32>(read_bits(32)); }
    f32 read_float(f32 lo, f32 hi, i32 bits);
    std::string read_string();
    std::vector<u8> read_bytes(std::size_t n);
    void align_to_byte();

    [[nodiscard]] bool overflowed() const { return overflow_; }
    [[nodiscard]] std::size_t bits_left() const { return bits_ > bit_pos_ ? bits_ - bit_pos_ : 0; }
private:
    const u8* data_ = nullptr;
    std::size_t bits_ = 0, bit_pos_ = 0;
    bool overflow_ = false;
};

// ------------------------------------------------------------- packet ------
enum class PacketType : u8 {
    Unreliable = 0,
    Reliable,
    Ack,
    DiscoveryProbe,
    DiscoveryAnnounce,
    JoinRequest,
    JoinAccept,
    JoinReject,
    Leave,
    Snapshot,
    Input,
    Heartbeat,
};
const char* packet_type_name(PacketType t);

struct PacketHeader {
    u32 protocol_id = 0x5052534Du;   // "PRSM"
    u8  type = 0;
    u16 seq = 0;
    u16 ack = 0;
    u32 ack_bits = 0;                // bit i = ack - (i+1) received
    u32 peer_id = 0;
    u32 frame = 0;
};

inline constexpr u32 kProtocolId = 0x5052534Du;
inline constexpr std::size_t kMaxPacketBytes = 1200;   // stays under typical MTU

struct Packet {
    PacketHeader header;
    std::vector<u8> payload;
    f64 sent_at = 0;
    bool acked = false;
    i32 sends = 1;
};

[[nodiscard]] std::vector<u8> serialise(const Packet& p);
[[nodiscard]] bool deserialise(const u8* data, std::size_t len, Packet& out);

/// Rolling ack bitmask helpers.
[[nodiscard]] bool sequence_greater(u16 a, u16 b);   // wrap-safe comparison
void ack_update(u16& ack, u32& ack_bits, u16 received);

// ------------------------------------------------------------ transport ----
struct Endpoint {
    u32 ip = 0;
    u16 port = 0;
    [[nodiscard]] std::string to_string() const;
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
};

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool open(u16 port) = 0;
    virtual void close() = 0;
    virtual bool send(const Endpoint& to, const u8* data, std::size_t len) = 0;
    /// Drains the receive queue. Returns the number of datagrams delivered.
    virtual std::size_t poll(std::vector<std::pair<Endpoint, std::vector<u8>>>& out) = 0;
    virtual bool broadcast(const u8* data, std::size_t len) = 0;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual u16 port() const = 0;
    [[nodiscard]] u64 bytes_sent() const { return sent_; }
    [[nodiscard]] u64 bytes_received() const { return received_; }
    [[nodiscard]] u64 packets_sent() const { return packets_sent_; }
    [[nodiscard]] u64 packets_received() const { return packets_received_; }
protected:
    u64 sent_ = 0, received_ = 0, packets_sent_ = 0, packets_received_ = 0;
};

/// In-process transport used by the host test suite and the editor's
/// "play in editor" mode. Two endpoints can be wired together directly.
class LoopbackTransport : public ITransport {
public:
    bool open(u16 port) override;
    void close() override;
    bool send(const Endpoint& to, const u8* data, std::size_t len) override;
    std::size_t poll(std::vector<std::pair<Endpoint, std::vector<u8>>>& out) override;
    bool broadcast(const u8* data, std::size_t len) override;
    [[nodiscard]] std::string name() const override { return "loopback"; }
    [[nodiscard]] u16 port() const override { return port_; }

    /// Registry shared by every LoopbackTransport in the process.
    static void register_transport(LoopbackTransport* t);
    static void unregister_transport(LoopbackTransport* t);
    /// Optional artificial latency / loss for testing lag compensation.
    void set_simulation(f32 latency_seconds, f32 loss_probability, u32 seed = 1);

private:
    u16 port_ = 0;
    std::deque<std::pair<f64, std::pair<Endpoint, std::vector<u8>>>> inbox_;
    f32 latency_ = 0.0f, loss_ = 0.0f;
    u32 seed_ = 1;
};

/// UDP over IPv4 for real LAN play (Wi-Fi, hotspot, Wi-Fi Direct, wired).
/// Compiled on POSIX targets only; the Android build is POSIX.
class UdpTransport : public ITransport {
public:
    ~UdpTransport() override;
    bool open(u16 port) override;
    void close() override;
    bool send(const Endpoint& to, const u8* data, std::size_t len) override;
    std::size_t poll(std::vector<std::pair<Endpoint, std::vector<u8>>>& out) override;
    bool broadcast(const u8* data, std::size_t len) override;
    [[nodiscard]] std::string name() const override { return "udp"; }
    [[nodiscard]] u16 port() const override { return port_; }
    [[nodiscard]] bool enabled() const;
private:
    int fd_ = -1;
    u16 port_ = 0;
};

// ------------------------------------------------------ link statistics ----
struct LinkStats {
    f32 rtt_ms = 0;
    f32 rtt_variance_ms = 0;
    f32 loss = 0.0f;               // 0..1
    u32 sent = 0, acked = 0, lost = 0, received = 0, duplicates = 0;
    /// Retransmission timeout: RTT + 4*variance, clamped for mobile jitter.
    [[nodiscard]] f32 rto_ms() const {
        f32 v = rtt_ms + 4.0f * rtt_variance_ms;
        return v < 30.0f ? 30.0f : (v > 1000.0f ? 1000.0f : v);
    }
};

/// Reliability on top of an unreliable datagram transport: sequence numbers,
/// a 32-entry ack bitmask, exponential-backoff retransmission and an
/// RFC 6298 style RTT estimator.
class ReliableChannel {
public:
    struct Config {
        u32 protocol_id = kProtocolId;
        f32 resend_interval_ms = 100.0f;
        i32 max_resends = 8;
        std::size_t pending_limit = 512;
    };

    ReliableChannel();
    explicit ReliableChannel(const Config& c);

    /// Queue a reliable packet. Returns its sequence number.
    u16 send_reliable(PacketType type, std::vector<u8> payload, u32 peer_id = 0, u32 frame = 0);
    /// Fire-and-forget: no retransmission, still sequenced for ack tracking.
    u16 send_unreliable(PacketType type, std::vector<u8> payload, u32 peer_id = 0, u32 frame = 0);

    /// Called when any packet arrives from the remote peer.
    void on_packet_received(u16 seq, u16 ack, u32 ack_bits, bool reliable, f64 now);
    /// Produces the packets that need to go out now (new + due retransmits).
    std::vector<Packet> flush(f64 now);
    /// Drops anything the peer has acknowledged.
    void process_acks(u16 ack, u32 ack_bits, f64 now);

    [[nodiscard]] u16 local_sequence() const { return local_seq_; }
    [[nodiscard]] u16 remote_ack() const { return remote_ack_; }
    [[nodiscard]] u32 remote_ack_bits() const { return remote_ack_bits_; }
    [[nodiscard]] std::size_t pending() const { return pending_.size(); }
    [[nodiscard]] const LinkStats& stats() const { return stats_; }
    [[nodiscard]] const Config& config() const { return cfg_; }
    void reset();

private:
    Config cfg_;
    u16 local_seq_ = 0;
    u16 remote_ack_ = 0;
    u32 remote_ack_bits_ = 0;
    bool have_remote_ = false;
    std::vector<Packet> pending_;
    LinkStats stats_;
};

// ----------------------------------------------------------- session -------
enum class Role : u8 { Offline, Host, Client };
const char* role_name(Role r);

struct Peer {
    u32 id = 0;
    std::string name;
    Endpoint endpoint;
    bool connected = false;
    f64 last_seen = 0;
    LinkStats link;
};

/// LAN discovery: the host broadcasts an announce, clients probe. Both are
/// plain UDP broadcasts on the local subnet, so no internet is involved.
struct DiscoveryAnnounce {
    std::string session_name = "PRISM Game";
    std::string game_id = "dev.prismengine.demo";
    u16 port = 0;
    u8  players = 0;
    u8  max_players = 4;
    u32 protocol_version = 1;
    std::string region_hint = "lan";
    [[nodiscard]] std::vector<u8> encode() const;
    [[nodiscard]] static bool decode(const u8* data, std::size_t len, DiscoveryAnnounce& out);
};

class Session {
public:
    struct Config {
        std::string session_name = "PRISM Game";
        std::string game_id = "dev.prismengine.demo";
        u8 max_players = 4;
        u16 port = 27015;
        f64 heartbeat_interval = 1.0;
        f64 timeout = 6.0;
        u32 protocol_version = 1;
    };

    Session();
    explicit Session(const Config& c);

    bool host(std::shared_ptr<ITransport> t);
    bool join(std::shared_ptr<ITransport> t, const Endpoint& host_ep, const std::string& player_name);
    void leave();
    /// One network frame: send heartbeats/announces, reap timeouts.
    void update(f64 now);
    /// Feed a datagram in from the transport.
    void on_datagram(const Endpoint& from, const u8* data, std::size_t len, f64 now);

    [[nodiscard]] Role role() const { return role_; }
    [[nodiscard]] u32 local_id() const { return local_id_; }
    [[nodiscard]] const std::vector<Peer>& peers() const { return peers_; }
    [[nodiscard]] const Peer* peer(u32 id) const;
    [[nodiscard]] std::size_t player_count() const;
    [[nodiscard]] const Config& config() const { return cfg_; }
    [[nodiscard]] const std::vector<DiscoveryAnnounce>& discovered() const { return discovered_; }
    void clear_discovered() { discovered_.clear(); }
    /// Invoked for every gameplay packet delivered to the game layer.
    std::function<void(u32 from_peer, const Packet&)> on_packet;
    std::function<void(u32 peer_id, bool joined)> on_peer_changed;

private:
    void send_to(const Endpoint& ep, PacketType type, const std::vector<u8>& payload);
    u32 peer_id_for(const Endpoint& ep) const;

    Config cfg_;
    std::shared_ptr<ITransport> transport_;
    Role role_ = Role::Offline;
    u32 local_id_ = 0;
    u32 next_peer_ = 1;
    std::vector<Peer> peers_;
    std::vector<DiscoveryAnnounce> discovered_;
    f64 last_heartbeat_ = 0;
};

// ------------------------------------------------------------ rollback -----
/// Fixed-timestep input frame. Games subclass `encode`/`decode` for their own
/// controls; the ring buffer stores the packed bytes so rollback is cheap.
struct InputFrame {
    u32 frame = 0;
    std::vector<u8> data;
    bool predicted = false;
    [[nodiscard]] u32 checksum() const;
};

struct RollbackConfig {
    i32 input_delay = 2;         // frames of delay before an input is applied
    i32 max_rollback = 8;        // how far back we are willing to resimulate
    i32 ring_size = 64;          // must be >= max_rollback + input_delay + slack
};

/// GGPO-style rollback. The game supplies `simulate(frame, inputs)` and
/// `save_state`/`load_state`; the netcode decides when to roll back.
class Rollback {
public:
    using SimulateFn = std::function<void(u32 frame, const std::vector<InputFrame>& inputs)>;
    using SaveFn = std::function<std::vector<u8>()>;
    using LoadFn = std::function<void(const std::vector<u8>&)>;

    explicit Rollback(const RollbackConfig& c = RollbackConfig{});

    void set_local_id(u32 id) { local_id_ = id; }
    /// Register a remote peer. Seeding `last_received_` at frame 0 makes the
    /// peer take part in prediction immediately, so a client that has not sent
    /// anything yet is *predicted* rather than silently ignored.
    void add_remote(u32 id) {
        if (remotes_.find(id) == remotes_.end()) { remotes_[id] = 0; last_received_[id] = 0; }
    }
    void set_callbacks(SimulateFn s, SaveFn sv, LoadFn l) { simulate_ = std::move(s); save_ = std::move(sv); load_ = std::move(l); }

    void add_local_input(const InputFrame& in);
    void add_remote_input(u32 peer, const InputFrame& in);
    /// Advance the simulation, rolling back if a late input invalidated history.
    void advance(f64 now);

    [[nodiscard]] u32 current_frame() const { return frame_; }
    [[nodiscard]] i32 frames_behind(u32 peer) const;
    [[nodiscard]] bool has_all_inputs(u32 frame) const;
    [[nodiscard]] u64 rollback_count() const { return rollbacks_; }
    [[nodiscard]] u64 predicted_frames() const { return predicted_; }
    [[nodiscard]] i32 input_delay() const { return cfg_.input_delay; }
    [[nodiscard]] const RollbackConfig& config() const { return cfg_; }
    /// Inputs a client should transmit to the host.
    std::vector<InputFrame> inputs_to_send(u32 peer);
    /// Per-frame world checksums, compared between peers to detect desync.
    void record_checksum(u32 frame, u32 checksum);
    [[nodiscard]] bool verify_checksums(const std::vector<std::pair<u32,u32>>& remote) const;
    [[nodiscard]] const std::unordered_map<u32,u32>& checksums() const { return checksums_; }

private:
    void run_frame(u32 frame);
    void prune_states();

    RollbackConfig cfg_;
    u32 local_id_ = 0;
    u32 frame_ = 0;
    u64 rollbacks_ = 0, predicted_ = 0;
    std::unordered_map<u32, u32> remotes_;                                  // peer -> last input frame
    std::unordered_map<u32, std::unordered_map<u32, InputFrame>> inputs_;   // frame -> peer -> input
    std::unordered_map<u32, u32> last_received_;                            // peer -> frame
    std::unordered_map<u32, u32> sent_to_;                                  // peer -> highest frame sent
    std::unordered_map<u32, u32> checksums_;                                // frame -> world checksum
    std::unordered_map<u32, std::vector<u8>> states_;                       // frame -> saved state
    std::unordered_map<u32, std::unordered_set<u32>> predicted_flags_;      // frame -> predicted peers
    SimulateFn simulate_; SaveFn save_; LoadFn load_;
};

// ------------------------------------------------------ lag compensation ---
/// Keeps a short history of entity transforms so the host can rewind to the
/// moment a client claims it fired. Bounded memory, constant-time lookup.
class LagCompensation {
public:
    struct Sample { f64 time = 0; math::Vec3 position; math::Vec3 velocity; };

    void record(u32 entity, f64 time, math::Vec3 pos, math::Vec3 vel);
    /// Interpolated state at `time`; falls back to clamping at the ends.
    [[nodiscard]] bool sample(u32 entity, f64 time, math::Vec3& pos, math::Vec3& vel) const;
    /// How far back in seconds the caller is allowed to rewind.
    void set_max_rewind(f64 seconds) { max_rewind_ = seconds; }
    [[nodiscard]] f64 max_rewind() const { return max_rewind_; }
    void prune(f64 now);
    void clear();
    [[nodiscard]] std::size_t tracked_entities() const { return history_.size(); }
    [[nodiscard]] std::size_t samples(u32 entity) const;

private:
    std::unordered_map<u32, std::deque<Sample>> history_;
    f64 max_rewind_ = 0.25;
};

// ----------------------------------------------------- snapshot stream -----
struct Snapshot {
    u32 frame = 0;
    f64 time = 0;
    struct Entity { u32 id = 0; math::Vec3 position; math::Vec3 velocity; math::Vec3 rotation; };
    std::vector<Entity> entities;
};

/// Client-side interpolation with a small extrapolation allowance so entities
/// keep moving during brief packet gaps instead of freezing.
class SnapshotStream {
public:
    void set_interpolation_delay(f64 seconds) { delay_ = seconds; }
    void set_extrapolation_limit(f64 seconds) { extrapolate_limit_ = seconds; }
    void push(Snapshot s);
    /// State to render at wall-clock `now`.
    [[nodiscard]] Snapshot sample(f64 now) const;
    [[nodiscard]] std::size_t buffered() const { return buffer_.size(); }
    [[nodiscard]] f64 interpolation_delay() const { return delay_; }
    void clear() { buffer_.clear(); }

private:
    std::deque<Snapshot> buffer_;
    f64 delay_ = 0.1;
    f64 extrapolate_limit_ = 0.25;
};

} // namespace prism::net
