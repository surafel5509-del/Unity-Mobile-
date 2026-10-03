// =====================================================================
//  PRISM ENGINE — net/net.cpp
//  Bit packing, reliable-over-unreliable, LAN discovery, rollback and
//  snapshot interpolation. Everything here is host-testable without a
//  network interface, which is how the test suite exercises it.
// =====================================================================
#include "prism/net/net.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__linux__) || defined(__APPLE__)
#  define PRISM_HAS_UDP 1
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#else
#  define PRISM_HAS_UDP 0
#endif

namespace prism::net {

namespace {
f64 steady_now() {
    using namespace std::chrono;
    return duration_cast<duration<f64>>(steady_clock::now().time_since_epoch()).count();
}

constexpr i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

// ============================================================ bit streams ==
void BitWriter::write_bits(u32 value, i32 bits) {
    bits = clampi(bits, 0, 32);
    if (bits < 32) value &= (1u << bits) - 1u;           // 32 needs no mask
    while (bits > 0) {
        if ((bit_pos_ & 7u) == 0) buffer_.push_back(0);
        const std::size_t byte = bit_pos_ >> 3;
        const i32 bit = static_cast<i32>(bit_pos_ & 7u);
        const i32 space = 8 - bit;
        const i32 take = space < bits ? space : bits;
        const u32 chunk = (value >> (bits - take)) & ((1u << take) - 1u);
        buffer_[byte] = static_cast<u8>(buffer_[byte] | static_cast<u8>(chunk << (space - take)));
        bits -= take;
        bit_pos_ += static_cast<std::size_t>(take);
    }
}

u32 BitReader::read_bits(i32 bits) {
    bits = clampi(bits, 0, 32);
    u32 value = 0;
    while (bits > 0) {
        if (bit_pos_ >= bits_) { overflow_ = true; return value; }
        const std::size_t byte = bit_pos_ >> 3;
        const i32 bit = static_cast<i32>(bit_pos_ & 7u);
        const i32 avail = 8 - bit;
        const i32 take = avail < bits ? avail : bits;
        const u32 chunk = (data_[byte] >> (avail - take)) & ((1u << take) - 1u);
        value = (value << take) | chunk;
        bits -= take;
        bit_pos_ += static_cast<std::size_t>(take);
    }
    return value;
}

void BitWriter::write_float(f32 v, f32 lo, f32 hi, i32 bits) {
    const f32 range = hi - lo;
    const f32 t = range <= 0.0f ? 0.0f : math::clampf((v - lo) / range, 0.0f, 1.0f);
    const u32 maxv = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    write_bits(static_cast<u32>(std::lround(t * static_cast<f32>(maxv))), bits);
}

f32 BitReader::read_float(f32 lo, f32 hi, i32 bits) {
    const u32 maxv = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    const f32 t = maxv == 0 ? 0.0f : static_cast<f32>(read_bits(bits)) / static_cast<f32>(maxv);
    return lo + t * (hi - lo);
}

void BitWriter::write_bytes(const u8* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) write_u8(data[i]);
}

std::vector<u8> BitReader::read_bytes(std::size_t n) {
    std::vector<u8> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back(read_u8());
    return out;
}

void BitWriter::write_string(const std::string& s) {
    const u16 len = static_cast<u16>(s.size() > 65535 ? 65535 : s.size());
    write_u16(len);
    write_bytes(reinterpret_cast<const u8*>(s.data()), len);
}

std::string BitReader::read_string() {
    const u16 len = read_u16();
    if (overflow_ || len > bits_left() / 8) { overflow_ = true; return {}; }
    std::string s;
    s.resize(len);
    for (u16 i = 0; i < len; ++i) s[i] = static_cast<char>(read_u8());
    return s;
}

void BitWriter::align_to_byte() {
    if ((bit_pos_ & 7u) == 0) return;
    bit_pos_ = (bit_pos_ + 7u) & ~static_cast<std::size_t>(7u);
    while (buffer_.size() * 8 < bit_pos_) buffer_.push_back(0);
}

void BitReader::align_to_byte() {
    if ((bit_pos_ & 7u) == 0) return;
    bit_pos_ = (bit_pos_ + 7u) & ~static_cast<std::size_t>(7u);
    if (bit_pos_ > bits_) { bit_pos_ = bits_; overflow_ = true; }
}

// ================================================================ packets ==
const char* packet_type_name(PacketType t) {
    switch (t) {
        case PacketType::Unreliable: return "unreliable";
        case PacketType::Reliable: return "reliable";
        case PacketType::Ack: return "ack";
        case PacketType::DiscoveryProbe: return "discovery_probe";
        case PacketType::DiscoveryAnnounce: return "discovery_announce";
        case PacketType::JoinRequest: return "join_request";
        case PacketType::JoinAccept: return "join_accept";
        case PacketType::JoinReject: return "join_reject";
        case PacketType::Leave: return "leave";
        case PacketType::Snapshot: return "snapshot";
        case PacketType::Input: return "input";
        case PacketType::Heartbeat: return "heartbeat";
    }
    return "unknown";
}

std::vector<u8> serialise(const Packet& p) {
    BitWriter w;
    w.write_u32(p.header.protocol_id);
    w.write_u8(p.header.type);
    w.write_u16(p.header.seq);
    w.write_u16(p.header.ack);
    w.write_u32(p.header.ack_bits);
    w.write_u32(p.header.peer_id);
    w.write_u32(p.header.frame);
    w.align_to_byte();
    w.write_u16(static_cast<u16>(p.payload.size()));
    w.write_bytes(p.payload.data(), p.payload.size());
    w.align_to_byte();
    return w.bytes();
}

bool deserialise(const u8* data, std::size_t len, Packet& out) {
    if (!data || len < 23) return false;
    BitReader r(data, len);
    out.header.protocol_id = r.read_u32();
    if (out.header.protocol_id != kProtocolId) return false;
    out.header.type = r.read_u8();
    out.header.seq = r.read_u16();
    out.header.ack = r.read_u16();
    out.header.ack_bits = r.read_u32();
    out.header.peer_id = r.read_u32();
    out.header.frame = r.read_u32();
    r.align_to_byte();
    const u16 plen = r.read_u16();
    if (r.overflowed() || static_cast<std::size_t>(plen) > r.bits_left() / 8) return false;
    out.payload = r.read_bytes(plen);
    return !r.overflowed();
}

bool sequence_greater(u16 a, u16 b) {
    const u16 diff = static_cast<u16>(a - b);
    return diff != 0 && diff < 32768;
}

void ack_update(u16& ack, u32& ack_bits, u16 received) {
    if (received == ack) { ack_bits |= 0; return; }
    if (sequence_greater(received, ack)) {
        const u16 shift = static_cast<u16>(received - ack);
        if (shift == 1) ack_bits = (ack_bits << 1) | 1u;
        else if (shift <= 32) ack_bits = (shift == 32) ? 0u : (ack_bits << (shift - 1));
        else ack_bits = 0;
        ack = received;
    } else {
        const u16 diff = static_cast<u16>(ack - received);
        if (diff > 0 && diff <= 32) ack_bits |= (1u << (diff - 1));
    }
}

// =============================================================== endpoint ==
std::string Endpoint::to_string() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                  (ip >> 24) & 0xFFu, (ip >> 16) & 0xFFu, (ip >> 8) & 0xFFu, ip & 0xFFu, port);
    return buf;
}

// ========================================================== loopback xport ==
namespace {
std::vector<LoopbackTransport*>& loopback_registry() {
    static std::vector<LoopbackTransport*> r;
    return r;
}
} // namespace

void LoopbackTransport::register_transport(LoopbackTransport* t) {
    auto& r = loopback_registry();
    if (std::find(r.begin(), r.end(), t) == r.end()) r.push_back(t);
}

void LoopbackTransport::unregister_transport(LoopbackTransport* t) {
    auto& r = loopback_registry();
    r.erase(std::remove(r.begin(), r.end(), t), r.end());
}

bool LoopbackTransport::open(u16 port) {
    if (port == 0) {                     // ephemeral, like bind(0) on a socket
        static u16 next_ephemeral = 40000;
        port = next_ephemeral++;
    }
    port_ = port;
    inbox_.clear();
    register_transport(this);
    return true;
}

void LoopbackTransport::close() {
    unregister_transport(this);
    inbox_.clear();
    port_ = 0;
}

void LoopbackTransport::set_simulation(f32 latency_seconds, f32 loss_probability, u32 seed) {
    latency_ = latency_seconds;
    loss_ = loss_probability;
    seed_ = seed ? seed : 1;
}

bool LoopbackTransport::send(const Endpoint& to, const u8* data, std::size_t len) {
    if (!port_) return false;
    // Deterministic PRNG so loss simulation is reproducible in tests.
    seed_ = seed_ * 1664525u + 1013904223u;
    if (loss_ > 0.0f && (static_cast<f32>(seed_ >> 8 & 0xFFFFFF) / 16777216.0f) < loss_) {
        packets_sent_++;
        sent_ += len;
        return true;                                   // silently dropped
    }
    for (auto* t : loopback_registry()) {
        if (t == this || t->port_ != to.port) continue;
        t->inbox_.emplace_back(steady_now() + latency_,
                               std::make_pair(Endpoint{0x7F000001u, port_},
                                              std::vector<u8>(data, data + len)));
    }
    packets_sent_++;
    sent_ += len;
    return true;
}

std::size_t LoopbackTransport::poll(std::vector<std::pair<Endpoint, std::vector<u8>>>& out) {
    const f64 now = steady_now();
    std::size_t n = 0;
    while (!inbox_.empty() && inbox_.front().first <= now) {
        out.emplace_back(inbox_.front().second);
        received_ += inbox_.front().second.second.size();
        inbox_.pop_front();
        ++n;
        packets_received_++;
    }
    return n;
}

bool LoopbackTransport::broadcast(const u8* data, std::size_t len) {
    for (auto* t : loopback_registry()) {
        if (t == this) continue;
        t->inbox_.emplace_back(steady_now(),
                               std::make_pair(Endpoint{0x7F000001u, port_},
                                              std::vector<u8>(data, data + len)));
    }
    packets_sent_++;
    sent_ += len;
    return true;
}

// =============================================================== udp xport ==
UdpTransport::~UdpTransport() { close(); }

bool UdpTransport::enabled() const { return PRISM_HAS_UDP != 0; }

bool UdpTransport::open(u16 port) {
#if PRISM_HAS_UDP
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    const int yes = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(fd_); fd_ = -1; return false; }
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    socklen_t alen = sizeof(addr);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &alen) == 0) port_ = ntohs(addr.sin_port);
    else port_ = port;
    return true;
#else
    (void)port;
    return false;
#endif
}

void UdpTransport::close() {
#if PRISM_HAS_UDP
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
#endif
    port_ = 0;
}

bool UdpTransport::send(const Endpoint& to, const u8* data, std::size_t len) {
#if PRISM_HAS_UDP
    if (fd_ < 0 || !data) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(to.port);
    addr.sin_addr.s_addr = htonl(to.ip);
    const ssize_t n = ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (n < 0) return false;
    packets_sent_++;
    sent_ += static_cast<u64>(n);
    return true;
#else
    (void)to; (void)data; (void)len;
    return false;
#endif
}

std::size_t UdpTransport::poll(std::vector<std::pair<Endpoint, std::vector<u8>>>& out) {
#if PRISM_HAS_UDP
    if (fd_ < 0) return 0;
    std::size_t count = 0;
    u8 buf[kMaxPacketBytes];
    for (int i = 0; i < 64; ++i) {
        sockaddr_in from{};
        socklen_t flen = sizeof(from);
        const ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &flen);
        if (n <= 0) break;
        Endpoint ep;
        ep.ip = ntohl(from.sin_addr.s_addr);
        ep.port = ntohs(from.sin_port);
        out.emplace_back(ep, std::vector<u8>(buf, buf + n));
        received_ += static_cast<u64>(n);
        packets_received_++;
        ++count;
    }
    return count;
#else
    (void)out;
    return 0;
#endif
}

bool UdpTransport::broadcast(const u8* data, std::size_t len) {
#if PRISM_HAS_UDP
    if (fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_ ? port_ : 27015);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    const ssize_t n = ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (n < 0) return false;
    packets_sent_++;
    sent_ += static_cast<u64>(n);
    return true;
#else
    (void)data; (void)len;
    return false;
#endif
}

// ========================================================= reliable channel ==
ReliableChannel::ReliableChannel() : ReliableChannel(Config{}) {}
ReliableChannel::ReliableChannel(const Config& c) : cfg_(c) {}

u16 ReliableChannel::send_reliable(PacketType type, std::vector<u8> payload, u32 peer_id, u32 frame) {
    Packet p;
    p.header.type = static_cast<u8>(type);
    p.header.seq = local_seq_++;
    p.header.peer_id = peer_id;
    p.header.frame = frame;
    p.payload = std::move(payload);
    p.sent_at = -1.0;                    // -1 = never transmitted
    if (pending_.size() >= cfg_.pending_limit) {
        // Never grow without bound on a stalled link; drop the oldest.
        pending_.erase(pending_.begin());
        stats_.lost++;
    }
    pending_.push_back(std::move(p));
    stats_.sent++;
    return pending_.back().header.seq;
}

u16 ReliableChannel::send_unreliable(PacketType type, std::vector<u8> payload, u32 peer_id, u32 frame) {
    const u16 seq = local_seq_++;
    (void)type; (void)payload; (void)peer_id; (void)frame;
    stats_.sent++;
    return seq;
}

std::vector<Packet> ReliableChannel::flush(f64 now) {
    std::vector<Packet> out;
    const f64 rto = static_cast<f64>(stats_.rto_ms());
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->acked) { it = pending_.erase(it); continue; }
        const bool due = it->sent_at < 0.0 || (now - it->sent_at) * 1000.0 >= rto;
        if (due) {
            if (it->sends > cfg_.max_resends) {
                stats_.lost++;
                it = pending_.erase(it);
                continue;
            }
            Packet p = *it;
            p.header.protocol_id = cfg_.protocol_id;
            p.header.ack = remote_ack_;
            p.header.ack_bits = remote_ack_bits_;
            p.sent_at = now;
            it->sent_at = now;
            it->sends++;
            out.push_back(std::move(p));
        }
        ++it;
    }
    return out;
}

void ReliableChannel::process_acks(u16 ack, u32 ack_bits, f64 now) {
    const f64 now_ms = now * 1000.0;
    for (auto it = pending_.begin(); it != pending_.end();) {
        Packet& p = *it;
        bool hit = p.header.seq == ack;
        if (!hit) {
            const u16 diff = static_cast<u16>(ack - p.header.seq);
            hit = diff > 0 && diff <= 32 && (ack_bits & (1u << (diff - 1))) != 0;
        }
        if (hit) {
            // Drop it here rather than in flush(), so pending() always reports
            // what the peer has not acknowledged yet.
            stats_.acked++;
            if (p.sent_at > 0.0) {
                const f32 sample = static_cast<f32>(now_ms - p.sent_at * 1000.0);
                if (sample > 0.0f) {
                    if (stats_.rtt_ms <= 0.0f) {
                        stats_.rtt_ms = sample;
                        stats_.rtt_variance_ms = sample * 0.5f;
                    } else {
                        // RFC 6298 smoothed RTT / deviation.
                        stats_.rtt_variance_ms = 0.75f * stats_.rtt_variance_ms +
                                                 0.25f * std::fabs(stats_.rtt_ms - sample);
                        stats_.rtt_ms = 0.875f * stats_.rtt_ms + 0.125f * sample;
                    }
                }
            }
            it = pending_.erase(it);
            continue;
        }
        ++it;
    }
}

void ReliableChannel::on_packet_received(u16 seq, u16 ack, u32 ack_bits, bool reliable, f64 now) {
    (void)reliable;
    stats_.received++;
    if (have_remote_ && !sequence_greater(seq, remote_ack_)) {
        // Seen at or before our ack window: duplicate or reordered.
        const u16 diff = static_cast<u16>(remote_ack_ - seq);
        if (diff > 0 && diff <= 32 && (remote_ack_bits_ & (1u << (diff - 1)))) stats_.duplicates++;
    }
    if (!have_remote_) { remote_ack_ = seq; remote_ack_bits_ = 0; have_remote_ = true; }
    else ack_update(remote_ack_, remote_ack_bits_, seq);
    process_acks(ack, ack_bits, now);
}

void ReliableChannel::reset() {
    local_seq_ = 0; remote_ack_ = 0; remote_ack_bits_ = 0; have_remote_ = false;
    pending_.clear();
    stats_ = LinkStats{};
}

// ================================================================ session ==
const char* role_name(Role r) {
    switch (r) { case Role::Host: return "host"; case Role::Client: return "client"; default: return "offline"; }
}

std::vector<u8> DiscoveryAnnounce::encode() const {
    BitWriter w;
    w.write_string(session_name);
    w.write_string(game_id);
    w.write_u16(port);
    w.write_u8(players);
    w.write_u8(max_players);
    w.write_u32(protocol_version);
    w.write_string(region_hint);
    w.align_to_byte();
    return w.bytes();
}

bool DiscoveryAnnounce::decode(const u8* data, std::size_t len, DiscoveryAnnounce& out) {
    BitReader r(data, len);
    out.session_name = r.read_string();
    out.game_id = r.read_string();
    out.port = r.read_u16();
    out.players = r.read_u8();
    out.max_players = r.read_u8();
    out.protocol_version = r.read_u32();
    out.region_hint = r.read_string();
    return !r.overflowed();
}

Session::Session() : Session(Config{}) {}
Session::Session(const Config& c) : cfg_(c) {}

bool Session::host(std::shared_ptr<ITransport> t) {
    if (!t) return false;
    transport_ = std::move(t);
    if (!transport_->open(cfg_.port)) return false;
    role_ = Role::Host;
    local_id_ = 0;
    next_peer_ = 1;
    peers_.clear();
    Peer self;
    self.id = 0;
    self.name = cfg_.session_name;
    self.connected = true;
    self.last_seen = 0.0;                // stamped on the first update()
    peers_.push_back(self);
    return true;
}

bool Session::join(std::shared_ptr<ITransport> t, const Endpoint& host_ep, const std::string& player_name) {
    if (!t) return false;
    transport_ = std::move(t);
    if (!transport_->open(0)) return false;
    role_ = Role::Client;
    peers_.clear();
    BitWriter w;
    w.write_string(player_name);
    w.write_u32(cfg_.protocol_version);
    w.align_to_byte();
    send_to(host_ep, PacketType::JoinRequest, w.bytes());
    Peer h;
    h.id = 0;
    h.name = "host";
    h.endpoint = host_ep;
    h.connected = true;
    h.last_seen = 0.0;
    peers_.push_back(h);
    return true;
}

void Session::leave() {
    if (transport_) {
        BitWriter w;
        w.write_u32(local_id_);
        w.align_to_byte();
        for (const auto& p : peers_) if (p.id != local_id_) send_to(p.endpoint, PacketType::Leave, w.bytes());
        transport_->close();
        transport_.reset();
    }
    role_ = Role::Offline;
    peers_.clear();
}

void Session::send_to(const Endpoint& ep, PacketType type, const std::vector<u8>& payload) {
    if (!transport_) return;
    Packet p;
    p.header.protocol_id = kProtocolId;
    p.header.type = static_cast<u8>(type);
    p.header.peer_id = local_id_;
    p.payload = payload;
    const std::vector<u8> bytes = serialise(p);
    transport_->send(ep, bytes.data(), bytes.size());
}

u32 Session::peer_id_for(const Endpoint& ep) const {
    for (const auto& p : peers_) if (p.endpoint == ep) return p.id;
    return 0xFFFFFFFFu;
}

void Session::update(f64 now) {
    if (!transport_ || role_ == Role::Offline) return;
    if (now - last_heartbeat_ >= cfg_.heartbeat_interval) {
        last_heartbeat_ = now;
        if (role_ == Role::Host) {
            DiscoveryAnnounce a;
            a.session_name = cfg_.session_name;
            a.game_id = cfg_.game_id;
            a.port = transport_->port();
            a.players = static_cast<u8>(player_count());
            a.max_players = cfg_.max_players;
            a.protocol_version = cfg_.protocol_version;
            transport_->broadcast(a.encode().data(), a.encode().size());
        }
        BitWriter w;
        w.write_u32(local_id_);
        w.align_to_byte();
        for (const auto& p : peers_) if (p.id != local_id_) send_to(p.endpoint, PacketType::Heartbeat, w.bytes());
    }
    // Reap silent peers (the host is authoritative on membership).
    if (role_ == Role::Host) {
        for (auto it = peers_.begin(); it != peers_.end();) {
            if (it->last_seen == 0.0) it->last_seen = now;
            if (it->id != 0 && now - it->last_seen > cfg_.timeout) {
                const u32 id = it->id;
                it = peers_.erase(it);
                if (on_peer_changed) on_peer_changed(id, false);
            } else ++it;
        }
    }
}

void Session::on_datagram(const Endpoint& from, const u8* data, std::size_t len, f64 now) {
    Packet p;
    if (!deserialise(data, len, p)) return;
    const auto type = static_cast<PacketType>(p.header.type);

    switch (type) {
        case PacketType::DiscoveryProbe: {
            if (role_ != Role::Host) return;
            DiscoveryAnnounce a;
            a.session_name = cfg_.session_name;
            a.game_id = cfg_.game_id;
            a.port = transport_ ? transport_->port() : cfg_.port;
            a.players = static_cast<u8>(player_count());
            a.max_players = cfg_.max_players;
            a.protocol_version = cfg_.protocol_version;
            const std::vector<u8> body = a.encode();
            send_to(from, PacketType::DiscoveryAnnounce, body);
            return;
        }
        case PacketType::DiscoveryAnnounce: {
            DiscoveryAnnounce a;
            if (!DiscoveryAnnounce::decode(p.payload.data(), p.payload.size(), a)) return;
            if (a.game_id != cfg_.game_id) return;
            a.port = a.port ? a.port : from.port;
            bool known = false;
            for (auto& d : discovered_) if (d.port == a.port && d.session_name == a.session_name) { known = true; d = a; }
            if (!known) discovered_.push_back(a);
            return;
        }
        case PacketType::JoinRequest: {
            if (role_ != Role::Host) return;
            BitReader r(p.payload.data(), p.payload.size());
            std::string name = r.read_string();
            const u32 version = r.read_u32();
            if (version != cfg_.protocol_version) {
                BitWriter rej;
                rej.write_u32(1);                      // reason: version mismatch
                rej.align_to_byte();
                send_to(from, PacketType::JoinReject, rej.bytes());
                return;
            }
            if (player_count() >= cfg_.max_players) {
                BitWriter rej;
                rej.write_u32(2);                      // reason: full
                rej.align_to_byte();
                send_to(from, PacketType::JoinReject, rej.bytes());
                return;
            }
            const u32 id = next_peer_++;
            Peer peer;
            peer.id = id;
            peer.name = name;
            peer.endpoint = from;
            peer.connected = true;
            peer.last_seen = now;
            peers_.push_back(peer);
            BitWriter acc;
            acc.write_u32(id);
            acc.write_u8(static_cast<u8>(player_count()));
            acc.align_to_byte();
            send_to(from, PacketType::JoinAccept, acc.bytes());
            if (on_peer_changed) on_peer_changed(id, true);
            return;
        }
        case PacketType::JoinAccept: {
            if (role_ != Role::Client) return;
            BitReader r(p.payload.data(), p.payload.size());
            local_id_ = r.read_u32();
            for (auto& peer : peers_) if (peer.id == 0) { peer.endpoint = from; peer.connected = true; }
            if (on_peer_changed) on_peer_changed(0, true);
            return;
        }
        case PacketType::JoinReject: {
            role_ = Role::Offline;
            peers_.clear();
            if (on_peer_changed) on_peer_changed(local_id_, false);
            return;
        }
        case PacketType::Leave: {
            const u32 id = peer_id_for(from);
            if (id == 0xFFFFFFFFu) return;
            peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                                        [id](const Peer& p) { return p.id == id; }), peers_.end());
            if (on_peer_changed) on_peer_changed(id, false);
            return;
        }
        case PacketType::Heartbeat: {
            for (auto& peer : peers_) if (peer.endpoint == from || peer.id == p.header.peer_id) {
                peer.last_seen = now;
                peer.link.received++;
            }
            return;
        }
        default: {
            const u32 id = peer_id_for(from);
            if (on_packet) on_packet(id == 0xFFFFFFFFu ? p.header.peer_id : id, p);
            return;
        }
    }
}

const Peer* Session::peer(u32 id) const {
    for (const auto& p : peers_) if (p.id == id) return &p;
    return nullptr;
}

std::size_t Session::player_count() const { return peers_.size(); }

// =============================================================== rollback ==
u32 InputFrame::checksum() const {
    u32 h = 2166136261u;
    for (u8 b : data) { h ^= b; h *= 16777619u; }
    return h;
}

Rollback::Rollback(const RollbackConfig& c) : cfg_(c) {}

void Rollback::add_local_input(const InputFrame& in) {
    inputs_[in.frame][local_id_] = in;
    last_received_[local_id_] = std::max(last_received_[local_id_], in.frame);
}

void Rollback::add_remote_input(u32 peer, const InputFrame& in) {
    inputs_[in.frame][peer] = in;
    last_received_[peer] = std::max(last_received_[peer], in.frame);
}

bool Rollback::has_all_inputs(u32 frame) const {
    auto fit = inputs_.find(frame);
    if (fit == inputs_.end()) return false;
    for (const auto& [peer, last] : last_received_) {
        (void)last;
        auto it = fit->second.find(peer);
        if (it == fit->second.end() || it->second.predicted) return false;
    }
    return true;
}

i32 Rollback::frames_behind(u32 peer) const {
    auto it = last_received_.find(peer);
    if (it == last_received_.end()) return static_cast<i32>(frame_);
    return static_cast<i32>(frame_) - static_cast<i32>(it->second);
}

void Rollback::record_checksum(u32 frame, u32 checksum) { checksums_[frame] = checksum; }

bool Rollback::verify_checksums(const std::vector<std::pair<u32, u32>>& remote) const {
    for (const auto& [frame, sum] : remote) {
        auto it = checksums_.find(frame);
        if (it != checksums_.end() && it->second != sum) return false;
    }
    return true;
}

std::vector<InputFrame> Rollback::inputs_to_send(u32 peer) {
    std::vector<InputFrame> out;
    // Collect against a fixed watermark. Advancing it inside the loop makes the
    // result depend on unordered_map iteration order and silently drops frames.
    const u32 high = sent_to_[peer];
    for (const auto& [frame, by_peer] : inputs_) {
        auto it = by_peer.find(local_id_);
        if (it == by_peer.end()) continue;
        if (frame <= high) continue;
        out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(), [](const InputFrame& a, const InputFrame& b) { return a.frame < b.frame; });
    if (!out.empty()) sent_to_[peer] = out.back().frame;
    return out;
}

void Rollback::advance(f64 now) {
    (void)now;
    // 1. Did a real input arrive for a frame we already simulated?
    i32 roll_to = -1;
    for (u32 f = frame_; f > 0; --f) {
        if (frame_ - f > static_cast<u32>(cfg_.max_rollback)) break;
        auto fit = inputs_.find(f);
        if (fit == inputs_.end()) continue;
        for (const auto& [peer, in] : fit->second) {
            if (in.predicted) continue;
            if (predicted_flags_[f].count(peer)) { roll_to = static_cast<i32>(f); break; }
        }
        if (roll_to >= 0) break;
    }

    // 2. Roll back and re-simulate if so.
    if (roll_to >= 0 && load_) {
        auto st = states_.find(static_cast<u32>(roll_to));
        if (st != states_.end()) load_(st->second);
        for (u32 f = static_cast<u32>(roll_to); f <= frame_; ++f) run_frame(f);
        rollbacks_++;
    }

    // 3. Step one frame forward.
    const u32 next = frame_ + 1;
    if (save_) states_[next] = save_();
    run_frame(next);
    frame_ = next;

    // 4. Keep the state ring bounded.
    prune_states();
}

void Rollback::run_frame(u32 frame) {
    if (!simulate_) return;
    std::vector<InputFrame> gathered;
    gathered.reserve(last_received_.size());
    for (const auto& [peer, last] : last_received_) {
        InputFrame in;
        in.frame = frame;
        auto fit = inputs_.find(frame);
        if (fit != inputs_.end()) {
            auto it = fit->second.find(peer);
            if (it != fit->second.end()) in = it->second;
        }
        if (!in.data.empty() && in.frame == frame && !in.predicted) {
            // This frame now runs on authoritative input; forget the prediction
            // so the rollback scan does not keep re-firing for it forever.
            predicted_flags_[frame].erase(peer);
        } else {
            // No input yet: repeat the peer's most recent one and flag it.
            u32 best = 0;
            bool found = false;
            for (const auto& [f, by_peer] : inputs_) {
                if (f > frame) continue;
                auto it = by_peer.find(peer);
                if (it != by_peer.end() && (!found || f > best)) { best = f; in = it->second; found = true; }
            }
            in.frame = frame;
            in.predicted = true;
            predicted_flags_[frame].insert(peer);
        }
        gathered.push_back(in);
    }
    std::sort(gathered.begin(), gathered.end(),
              [](const InputFrame& a, const InputFrame& b) { return a.frame < b.frame; });
    simulate_(frame, gathered);
    if (!has_all_inputs(frame)) predicted_++;
}

void Rollback::prune_states() {
    while (states_.size() > static_cast<std::size_t>(cfg_.ring_size)) {
        u32 oldest = 0xFFFFFFFFu;
        for (const auto& kv : states_) oldest = std::min(oldest, kv.first);
        states_.erase(oldest);
    }
    // Prediction flags are only meaningful inside the rollback window.
    if (!states_.empty() && predicted_flags_.size() > static_cast<std::size_t>(cfg_.ring_size) * 2) {
        u32 oldest = 0xFFFFFFFFu;
        for (const auto& kv : states_) oldest = std::min(oldest, kv.first);
        for (auto it = predicted_flags_.begin(); it != predicted_flags_.end();) {
            if (it->first < oldest) it = predicted_flags_.erase(it);
            else ++it;
        }
    }
}

// ========================================================= lag compensation ==
void LagCompensation::record(u32 entity, f64 time, math::Vec3 pos, math::Vec3 vel) {
    auto& dq = history_[entity];
    dq.push_back(Sample{time, pos, vel});
    while (dq.size() > 512) dq.pop_front();
}

void LagCompensation::prune(f64 now) {
    for (auto it = history_.begin(); it != history_.end();) {
        auto& dq = it->second;
        while (!dq.empty() && now - dq.front().time > max_rewind_) dq.pop_front();
        if (dq.empty()) it = history_.erase(it);
        else ++it;
    }
}

void LagCompensation::clear() { history_.clear(); }

std::size_t LagCompensation::samples(u32 entity) const {
    auto it = history_.find(entity);
    return it == history_.end() ? 0 : it->second.size();
}

bool LagCompensation::sample(u32 entity, f64 time, math::Vec3& pos, math::Vec3& vel) const {
    auto it = history_.find(entity);
    if (it == history_.end() || it->second.empty()) return false;
    const auto& dq = it->second;
    if (time <= dq.front().time) { pos = dq.front().position; vel = dq.front().velocity; return true; }
    if (time >= dq.back().time) { pos = dq.back().position; vel = dq.back().velocity; return true; }
    for (std::size_t i = 1; i < dq.size(); ++i) {
        if (time <= dq[i].time) {
            const f64 span = dq[i].time - dq[i - 1].time;
            const f32 t = span <= 0.0 ? 0.0f : static_cast<f32>((time - dq[i - 1].time) / span);
            pos = dq[i - 1].position.lerp(dq[i].position, t);
            vel = dq[i - 1].velocity.lerp(dq[i].velocity, t);
            return true;
        }
    }
    pos = dq.back().position;
    vel = dq.back().velocity;
    return true;
}

// ========================================================== snapshot stream ==
void SnapshotStream::push(Snapshot s) {
    // Insert in time order; late packets slot in rather than corrupting it.
    auto it = std::lower_bound(buffer_.begin(), buffer_.end(), s.time,
                               [](const Snapshot& a, f64 t) { return a.time < t; });
    buffer_.insert(it, std::move(s));
    while (buffer_.size() > 64) buffer_.pop_front();
}

Snapshot SnapshotStream::sample(f64 now) const {
    if (buffer_.empty()) return {};
    const f64 render_time = now - delay_;
    if (render_time <= buffer_.front().time) return buffer_.front();

    if (render_time >= buffer_.back().time) {
        // Extrapolate the newest snapshot, but only for a bounded window so a
        // stalled peer freezes instead of drifting off to infinity.
        Snapshot out = buffer_.back();
        const f64 dt = render_time - out.time;
        if (dt > extrapolate_limit_) return out;
        const f32 t = static_cast<f32>(dt);
        for (auto& e : out.entities) e.position = e.position + e.velocity * t;
        out.time = render_time;
        return out;
    }
    for (std::size_t i = 1; i < buffer_.size(); ++i) {
        if (render_time <= buffer_[i].time) {
            const Snapshot& a = buffer_[i - 1];
            const Snapshot& b = buffer_[i];
            const f64 span = b.time - a.time;
            const f32 t = span <= 0.0 ? 0.0f : static_cast<f32>((render_time - a.time) / span);
            Snapshot out = b;
            out.time = render_time;
            out.frame = t < 0.5f ? a.frame : b.frame;
            out.entities.clear();
            for (const auto& ea : a.entities) {
                const Snapshot::Entity* eb = nullptr;
                for (const auto& cand : b.entities) if (cand.id == ea.id) { eb = &cand; break; }
                Snapshot::Entity merged = ea;
                if (eb) {
                    merged.position = ea.position.lerp(eb->position, t);
                    merged.velocity = ea.velocity.lerp(eb->velocity, t);
                    merged.rotation = ea.rotation.lerp(eb->rotation, t);
                }
                out.entities.push_back(merged);
            }
            return out;
        }
    }
    return buffer_.back();
}

} // namespace prism::net
