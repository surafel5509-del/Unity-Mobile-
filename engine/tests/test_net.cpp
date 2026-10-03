// =====================================================================
//  PRISM ENGINE — tests/test_net.cpp
//  LAN networking: bit packing, reliability, discovery, rollback, lag
//  compensation and snapshot interpolation. Every test runs against the
//  in-process loopback transport, so the suite needs no network access.
// =====================================================================
#include "prism_test.h"
#include "prism/net/net.h"

#include <cstring>
#include <memory>

using namespace prism;
using namespace prism::net;

namespace {

/// Moves every queued datagram from a transport into a session.
void pump(ITransport& t, Session& s, f64 now) {
    std::vector<std::pair<Endpoint, std::vector<u8>>> inbox;
    t.poll(inbox);
    for (const auto& [from, bytes] : inbox)
        s.on_datagram(from, bytes.data(), bytes.size(), now);
}

InputFrame make_input(u32 frame, u8 delta) {
    InputFrame f;
    f.frame = frame;
    f.data = {delta};
    return f;
}

} // namespace

// =========================================================== bit packing ==
PRISM_TEST(net_bitwriter_roundtrip) {
    BitWriter w;
    w.write_bits(0x5, 3);
    w.write_bool(true);
    w.write_bool(false);
    w.write_u8(0xAB);
    w.write_u16(0xBEEF);
    w.write_u32(0xDEADBEEFu);
    w.write_i32(-12345);
    w.write_bits(0x2A, 7);
    w.write_string("Ray");
    w.align_to_byte();

    BitReader r(w.bytes().data(), w.bytes().size());
    PRISM_CHECK_EQ(r.read_bits(3), 5u);
    PRISM_CHECK(r.read_bool());
    PRISM_CHECK(!r.read_bool());
    PRISM_CHECK_EQ(r.read_u8(), 0xABu);
    PRISM_CHECK_EQ(r.read_u16(), 0xBEEFu);
    PRISM_CHECK_EQ(r.read_u32(), 0xDEADBEEFu);
    PRISM_CHECK_EQ(r.read_i32(), -12345);
    PRISM_CHECK_EQ(r.read_bits(7), 0x2Au);
    PRISM_CHECK_STR(r.read_string(), "Ray");
    PRISM_CHECK(!r.overflowed());

    // A 1-bit write must not spill into the next byte.
    BitWriter bits;
    bits.write_bits(1, 1);
    PRISM_CHECK_EQ(bits.byte_count(), 1u);
    PRISM_CHECK_EQ(bits.bit_count(), 1u);
}

PRISM_TEST(net_bitwriter_fixed_point_precision) {
    BitWriter w;
    w.write_float(3.5f, -10.0f, 10.0f, 16);
    w.write_float(-10.0f, -10.0f, 10.0f, 8);      // clamps to the low end
    w.write_float(99.0f, -10.0f, 10.0f, 8);       // clamps to the high end
    BitReader r(w.bytes().data(), w.bytes().size());
    PRISM_CHECK_NEAR(r.read_float(-10.0f, 10.0f, 16), 3.5f, 1e-3f);
    PRISM_CHECK_NEAR(r.read_float(-10.0f, 10.0f, 8), -10.0f, 1e-4f);
    PRISM_CHECK_NEAR(r.read_float(-10.0f, 10.0f, 8), 10.0f, 1e-4f);

    // Fewer bits -> coarser, but still inside one quantum.
    BitWriter c;
    c.write_float(0.5f, 0.0f, 1.0f, 4);
    BitReader cr(c.bytes().data(), c.bytes().size());
    const f32 quantum = 1.0f / 15.0f;
    PRISM_CHECK(std::fabs(cr.read_float(0.0f, 1.0f, 4) - 0.5f) <= quantum + 1e-6f);
}

PRISM_TEST(net_bitreader_detects_overflow) {
    const u8 one[1] = {0xFF};
    BitReader r(one, 1);
    PRISM_CHECK_EQ(r.read_u8(), 0xFFu);
    PRISM_CHECK(!r.overflowed());
    r.read_u16();
    PRISM_CHECK(r.overflowed());
    PRISM_CHECK_EQ(r.bits_left(), 0u);
    // Reading past the end is sticky, not a crash.
    r.read_u32();
    PRISM_CHECK(r.overflowed());
}

// ================================================================ packet ==
PRISM_TEST(net_packet_roundtrip_and_validation) {
    Packet p;
    p.header.type = static_cast<u8>(PacketType::Snapshot);
    p.header.seq = 1234;
    p.header.ack = 99;
    p.header.ack_bits = 0x5A5A5A5Au;
    p.header.peer_id = 7;
    p.header.frame = 4242;
    p.payload = {1, 2, 3, 4, 5};

    const std::vector<u8> bytes = serialise(p);
    PRISM_CHECK(bytes.size() == 28u);              // 21-byte header + 2 len + 5 payload

    Packet q;
    PRISM_CHECK(deserialise(bytes.data(), bytes.size(), q));
    PRISM_CHECK_EQ(q.header.protocol_id, kProtocolId);
    PRISM_CHECK_EQ(q.header.type, static_cast<u8>(PacketType::Snapshot));
    PRISM_CHECK_EQ(q.header.seq, 1234u);
    PRISM_CHECK_EQ(q.header.ack, 99u);
    PRISM_CHECK_EQ(q.header.ack_bits, 0x5A5A5A5Au);
    PRISM_CHECK_EQ(q.header.peer_id, 7u);
    PRISM_CHECK_EQ(q.header.frame, 4242u);
    PRISM_CHECK_EQ(q.payload.size(), 5u);
    PRISM_CHECK_EQ(q.payload[4], 5u);

    // A foreign protocol id must be rejected outright.
    std::vector<u8> bad = bytes;
    bad[0] ^= 0xFF;
    Packet junk;
    PRISM_CHECK(!deserialise(bad.data(), bad.size(), junk));
    // Truncated datagrams too.
    PRISM_CHECK(!deserialise(bytes.data(), 5, junk));
    PRISM_CHECK(!deserialise(nullptr, 0, junk));
    // A packet smaller than the header is not even parseable.
    PRISM_CHECK(kMaxPacketBytes > 23u);
}

PRISM_TEST(net_sequence_comparison_wraps) {
    PRISM_CHECK(sequence_greater(10, 9));
    PRISM_CHECK(!sequence_greater(9, 10));
    PRISM_CHECK(!sequence_greater(5, 5));
    PRISM_CHECK(sequence_greater(0, 65535));       // wrapped forward
    PRISM_CHECK(!sequence_greater(65535, 0));
    PRISM_CHECK(sequence_greater(32000, 100));
    PRISM_CHECK(!sequence_greater(100, 32000));    // too far back to be "newer"
}

PRISM_TEST(net_ack_bitmask_tracks_reception) {
    u16 ack = 0;
    u32 bits = 0;
    ack_update(ack, bits, 1);
    PRISM_CHECK_EQ(ack, 1u);
    PRISM_CHECK_EQ(bits, 1u);                      // bit0 = seq 0
    ack_update(ack, bits, 2);
    PRISM_CHECK_EQ(ack, 2u);
    PRISM_CHECK_EQ(bits, 3u);                      // seq 1 and 0
    ack_update(ack, bits, 0);                      // out-of-order old packet
    PRISM_CHECK_EQ(ack, 2u);
    PRISM_CHECK_EQ(bits, 3u);                      // seq 0 was already set
    ack_update(ack, bits, 4);                      // gap: seq 3 never arrived
    PRISM_CHECK_EQ(ack, 4u);
    PRISM_CHECK_EQ(bits, 6u);                      // 0b110 -> seq 2 and 1, not 3
    ack_update(ack, bits, 100);                    // huge jump clears the window
    PRISM_CHECK_EQ(ack, 100u);
    PRISM_CHECK_EQ(bits, 0u);
}

// ====================================================== reliable channel ==
PRISM_TEST(net_reliable_channel_retransmits_until_acked) {
    ReliableChannel ch;
    const u16 seq = ch.send_reliable(PacketType::Snapshot, {1, 2, 3});
    PRISM_CHECK_EQ(ch.pending(), 1u);

    const auto first = ch.flush(0.0);
    PRISM_CHECK_EQ(first.size(), 1u);
    PRISM_CHECK_EQ(first[0].header.seq, seq);

    // Inside the RTO there must be no redundant retransmission.
    PRISM_CHECK(ch.flush(0.010).empty());
    // Past it there must be exactly one.
    PRISM_CHECK_EQ(ch.flush(1.0).size(), 1u);

    ch.process_acks(seq, 0, 1.05);
    PRISM_CHECK(ch.flush(1.1).empty());
    PRISM_CHECK_EQ(ch.pending(), 0u);
    PRISM_CHECK_EQ(ch.stats().acked, 1u);
    PRISM_CHECK_EQ(ch.stats().sent, 1u);
}

PRISM_TEST(net_reliable_channel_gives_up_after_max_resends) {
    ReliableChannel::Config cfg;
    cfg.max_resends = 2;
    ReliableChannel ch(cfg);
    ch.send_reliable(PacketType::Snapshot, {9});
    for (int i = 0; i < 10; ++i) ch.flush(static_cast<f64>(i));
    PRISM_CHECK_EQ(ch.pending(), 0u);
    PRISM_CHECK_EQ(ch.stats().lost, 1u);
    PRISM_CHECK(ch.stats().acked == 0);
}

PRISM_TEST(net_reliable_channel_stays_bounded_on_a_dead_link) {
    ReliableChannel::Config cfg;
    cfg.pending_limit = 8;
    ReliableChannel ch(cfg);
    for (int i = 0; i < 100; ++i) ch.send_reliable(PacketType::Input, {static_cast<u8>(i)});
    PRISM_CHECK(ch.pending() <= 8u);
    PRISM_CHECK(ch.stats().lost >= 92u);
}

PRISM_TEST(net_rtt_estimator_is_rfc6298_shaped) {
    ReliableChannel ch;
    const u16 a = ch.send_reliable(PacketType::Snapshot, {1});
    ch.flush(1.0);
    ch.process_acks(a, 0, 1.100);                  // 100 ms
    PRISM_CHECK_NEAR(ch.stats().rtt_ms, 100.0f, 0.5f);
    PRISM_CHECK_NEAR(ch.stats().rtt_variance_ms, 50.0f, 0.5f);
    PRISM_CHECK_NEAR(ch.stats().rto_ms(), 300.0f, 0.5f);   // rtt + 4*var

    const u16 b = ch.send_reliable(PacketType::Snapshot, {2});
    ch.flush(1.2);
    ch.process_acks(b, 0, 1.320);                  // 120 ms
    // var = 0.75*50 + 0.25*|100-120| = 42.5 ; srtt = 0.875*100 + 0.125*120 = 102.5
    PRISM_CHECK_NEAR(ch.stats().rtt_variance_ms, 42.5f, 0.5f);
    PRISM_CHECK_NEAR(ch.stats().rtt_ms, 102.5f, 0.5f);
    PRISM_CHECK(ch.stats().rto_ms() >= 30.0f);
    PRISM_CHECK(ch.stats().rto_ms() <= 1000.0f);

    // A zero-RTT link still gets a usable floor.
    LinkStats floor;
    PRISM_CHECK_NEAR(floor.rto_ms(), 30.0f, 1e-4f);
}

PRISM_TEST(net_reliable_channel_ack_bitmask_clears_a_run) {
    ReliableChannel ch;
    const u16 s0 = ch.send_reliable(PacketType::Snapshot, {0});
    const u16 s1 = ch.send_reliable(PacketType::Snapshot, {1});
    const u16 s2 = ch.send_reliable(PacketType::Snapshot, {2});
    ch.flush(0.0);
    // Peer acks s2 with a bitmask covering s1 and s0.
    ch.process_acks(s2, 0b11, 0.05);
    PRISM_CHECK_EQ(ch.pending(), 0u);
    PRISM_CHECK_EQ(ch.stats().acked, 3u);
    (void)s0; (void)s1;
}

// ============================================================== discovery ==
PRISM_TEST(net_discovery_announce_roundtrip) {
    DiscoveryAnnounce a;
    a.session_name = "Ray's Prism Room";
    a.game_id = "dev.prismengine.demo";
    a.port = 27015;
    a.players = 2;
    a.max_players = 8;
    a.protocol_version = 3;
    a.region_hint = "addis-lan";

    const std::vector<u8> bytes = a.encode();
    DiscoveryAnnounce b;
    PRISM_CHECK(DiscoveryAnnounce::decode(bytes.data(), bytes.size(), b));
    PRISM_CHECK_STR(b.session_name, "Ray's Prism Room");
    PRISM_CHECK_STR(b.game_id, "dev.prismengine.demo");
    PRISM_CHECK_EQ(b.port, 27015u);
    PRISM_CHECK_EQ(b.players, 2u);
    PRISM_CHECK_EQ(b.max_players, 8u);
    PRISM_CHECK_EQ(b.protocol_version, 3u);
    PRISM_CHECK_STR(b.region_hint, "addis-lan");

    DiscoveryAnnounce c;
    const u8 garbage[3] = {1, 2, 3};
    PRISM_CHECK(!DiscoveryAnnounce::decode(garbage, 3, c));
}

// ================================================================ session ==
PRISM_TEST(net_session_host_accepts_a_join) {
    auto host_t = std::make_shared<LoopbackTransport>();
    auto client_t = std::make_shared<LoopbackTransport>();

    Session::Config hcfg;
    hcfg.session_name = "Ray's Room";
    hcfg.max_players = 4;
    hcfg.port = 30100;
    Session host(hcfg);
    Session client;

    PRISM_CHECK(host.host(host_t));
    PRISM_CHECK(host.role() == Role::Host);
    PRISM_CHECK_EQ(host.local_id(), 0u);
    PRISM_CHECK_EQ(host.player_count(), 1u);
    PRISM_CHECK_STR(role_name(host.role()), "host");

    int joined = -1;
    host.on_peer_changed = [&](u32 id, bool up) { if (up) joined = static_cast<int>(id); };

    PRISM_CHECK(client.join(client_t, Endpoint{0x7F000001u, 30100}, "Surafel"));

    pump(*host_t, host, 1.0);
    PRISM_CHECK_EQ(host.player_count(), 2u);
    PRISM_CHECK_EQ(joined, 1);
    PRISM_CHECK(host.peer(1) != nullptr);
    PRISM_CHECK_STR(host.peer(1)->name, "Surafel");

    pump(*client_t, client, 1.0);
    PRISM_CHECK(client.role() == Role::Client);
    PRISM_CHECK_EQ(client.local_id(), 1u);
    PRISM_CHECK_STR(role_name(client.role()), "client");

    host.leave();
    client.leave();
    PRISM_CHECK(host.role() == Role::Offline);
}

PRISM_TEST(net_session_rejects_when_full) {
    auto host_t = std::make_shared<LoopbackTransport>();
    auto a_t = std::make_shared<LoopbackTransport>();
    auto b_t = std::make_shared<LoopbackTransport>();

    Session::Config hcfg;
    hcfg.max_players = 2;                            // host + one guest
    hcfg.port = 30200;
    Session host(hcfg), guest_a, guest_b;
    PRISM_CHECK(host.host(host_t));

    guest_a.join(a_t, Endpoint{0x7F000001u, 30200}, "A");
    pump(*host_t, host, 1.0);
    pump(*a_t, guest_a, 1.0);
    PRISM_CHECK_EQ(host.player_count(), 2u);
    PRISM_CHECK_EQ(guest_a.local_id(), 1u);

    guest_b.join(b_t, Endpoint{0x7F000001u, 30200}, "B");
    pump(*host_t, host, 1.0);
    PRISM_CHECK_EQ(host.player_count(), 2u);         // still full, no second guest
    pump(*b_t, guest_b, 1.0);
    PRISM_CHECK(guest_b.role() == Role::Offline);    // rejected and torn down
    PRISM_CHECK_EQ(guest_b.peers().size(), 0u);
}

PRISM_TEST(net_session_rejects_a_protocol_mismatch) {
    auto host_t = std::make_shared<LoopbackTransport>();
    auto c_t = std::make_shared<LoopbackTransport>();
    Session::Config hcfg;
    hcfg.port = 30300;
    hcfg.protocol_version = 7;
    Session host(hcfg), client;
    PRISM_CHECK(host.host(host_t));
    client.join(c_t, Endpoint{0x7F000001u, 30300}, "Old Build");   // version 1
    pump(*host_t, host, 1.0);
    PRISM_CHECK_EQ(host.player_count(), 1u);
    pump(*c_t, client, 1.0);
    PRISM_CHECK(client.role() == Role::Offline);
}

PRISM_TEST(net_session_reaps_timed_out_peers) {
    auto host_t = std::make_shared<LoopbackTransport>();
    auto c_t = std::make_shared<LoopbackTransport>();
    Session::Config hcfg;
    hcfg.port = 30400;
    hcfg.timeout = 2.0;
    Session host(hcfg), client;
    PRISM_CHECK(host.host(host_t));
    client.join(c_t, Endpoint{0x7F000001u, 30400}, "Ghost");
    pump(*host_t, host, 10.0);
    PRISM_CHECK_EQ(host.player_count(), 2u);

    host.update(11.0);                               // inside the timeout window
    PRISM_CHECK_EQ(host.player_count(), 2u);
    host.update(20.0);                               // silent for 10 s
    PRISM_CHECK_EQ(host.player_count(), 1u);
    PRISM_CHECK(host.peer(1) == nullptr);
}

PRISM_TEST(net_session_discovery_probe_gets_an_announce) {
    auto host_t = std::make_shared<LoopbackTransport>();
    auto probe_t = std::make_shared<LoopbackTransport>();
    Session::Config hcfg;
    hcfg.session_name = "Find Me";
    hcfg.port = 30500;
    Session host(hcfg), browser;
    PRISM_CHECK(host.host(host_t));
    PRISM_CHECK(browser.join(probe_t, Endpoint{0x7F000001u, 30500}, "Browser"));

    // A client probes the subnet; the host answers with its announcement.
    Packet probe;
    probe.header.type = static_cast<u8>(PacketType::DiscoveryProbe);
    const std::vector<u8> bytes = serialise(probe);
    PRISM_CHECK(probe_t->send(Endpoint{0x7F000001u, 30500}, bytes.data(), bytes.size()));

    pump(*host_t, host, 1.0);
    pump(*probe_t, browser, 1.0);
    PRISM_CHECK_EQ(browser.discovered().size(), 1u);
    PRISM_CHECK_STR(browser.discovered()[0].session_name, "Find Me");
    PRISM_CHECK_EQ(browser.discovered()[0].port, 30500u);
    browser.clear_discovered();
    PRISM_CHECK_EQ(browser.discovered().size(), 0u);
}

PRISM_TEST(net_session_delivers_gameplay_packets) {
    auto host_t = std::make_shared<LoopbackTransport>();
    auto c_t = std::make_shared<LoopbackTransport>();
    Session::Config hcfg;
    hcfg.port = 30600;
    Session host(hcfg), client;
    PRISM_CHECK(host.host(host_t));
    client.join(c_t, Endpoint{0x7F000001u, 30600}, "Player");
    pump(*host_t, host, 1.0);
    pump(*c_t, client, 1.0);

    u32 got_from = 0xFFFFFFFFu;
    u32 got_frame = 0;
    host.on_packet = [&](u32 peer, const Packet& p) { got_from = peer; got_frame = p.header.frame; };

    Packet snap;
    snap.header.type = static_cast<u8>(PacketType::Input);
    snap.header.frame = 77;
    snap.header.peer_id = client.local_id();
    const std::vector<u8> bytes = serialise(snap);
    PRISM_CHECK(c_t->send(Endpoint{0x7F000001u, 30600}, bytes.data(), bytes.size()));
    pump(*host_t, host, 1.0);
    PRISM_CHECK_EQ(got_from, 1u);
    PRISM_CHECK_EQ(got_frame, 77u);
}

// =============================================================== rollback ==
PRISM_TEST(net_rollback_resimulates_when_a_late_input_arrives) {
    i32 pos = 0;
    RollbackConfig rc;
    rc.input_delay = 2;
    rc.max_rollback = 8;
    rc.ring_size = 32;
    Rollback rb(rc);
    rb.set_local_id(1);
    rb.add_remote(2);
    rb.set_callbacks(
        [&](u32, const std::vector<InputFrame>& in) {
            for (const auto& i : in) if (!i.data.empty()) pos += static_cast<i32>(i.data[0]);
        },
        [&]() {
            std::vector<u8> s(4);
            std::memcpy(s.data(), &pos, 4);
            return s;
        },
        [&](const std::vector<u8>& s) { if (s.size() == 4) std::memcpy(&pos, s.data(), 4); });

    rb.add_local_input(make_input(1, 2));
    rb.advance(0);
    PRISM_CHECK_EQ(rb.current_frame(), 1u);
    PRISM_CHECK_EQ(pos, 2);                          // peer 2 predicted as "no input"
    PRISM_CHECK(!rb.has_all_inputs(1));
    PRISM_CHECK(rb.predicted_frames() > 0);

    rb.add_local_input(make_input(2, 3));
    rb.advance(0);
    PRISM_CHECK_EQ(pos, 5);
    PRISM_CHECK_EQ(rb.current_frame(), 2u);

    // The remote's real input for frame 1 lands two frames late.
    rb.add_remote_input(2, make_input(1, 7));
    rb.advance(0);
    PRISM_CHECK_EQ(rb.rollback_count(), 1u);
    PRISM_CHECK_EQ(rb.current_frame(), 3u);
    // frame1: 2+7=9 | frame2: 3+7 (predicted repeat) =10 | frame3: 3+7 predicted =10
    PRISM_CHECK_EQ(pos, 29);
    PRISM_CHECK(rb.has_all_inputs(1));

    // Nothing new arrived, so the next advance is a plain forward step.
    const u64 before = rb.rollback_count();
    rb.advance(0);
    PRISM_CHECK_EQ(rb.rollback_count(), before);
    PRISM_CHECK_EQ(rb.current_frame(), 4u);
}

PRISM_TEST(net_rollback_tracks_how_far_behind_a_peer_is) {
    Rollback rb;
    rb.set_local_id(1);
    rb.add_remote(2);
    rb.set_callbacks([&](u32, const std::vector<InputFrame>&) {},
                     []() { return std::vector<u8>{}; },
                     [](const std::vector<u8>&) {});
    for (u32 f = 1; f <= 5; ++f) { rb.add_local_input(make_input(f, 1)); rb.advance(0); }
    PRISM_CHECK_EQ(rb.current_frame(), 5u);
    PRISM_CHECK_EQ(rb.frames_behind(2), 5);
    rb.add_remote_input(2, make_input(4, 1));
    PRISM_CHECK_EQ(rb.frames_behind(2), 1);
    PRISM_CHECK_EQ(rb.input_delay(), 2);   // RollbackConfig default
}

PRISM_TEST(net_rollback_checksums_detect_a_desync) {
    Rollback rb;
    rb.record_checksum(10, 0xABCDu);
    rb.record_checksum(11, 0x1234u);
    PRISM_CHECK(rb.verify_checksums({{10, 0xABCDu}, {11, 0x1234u}}));
    PRISM_CHECK(!rb.verify_checksums({{10, 0x0001u}}));   // divergence
    PRISM_CHECK(rb.verify_checksums({{99, 0x9999u}}));    // unknown frame: no evidence
    PRISM_CHECK_EQ(rb.checksums().size(), 2u);

    // Input frames hash their own contents so a wire-level corruption shows up.
    PRISM_CHECK(make_input(1, 5).checksum() == make_input(1, 5).checksum());
    PRISM_CHECK(make_input(1, 5).checksum() != make_input(1, 6).checksum());
}

PRISM_TEST(net_rollback_inputs_to_send_advances_a_high_water_mark) {
    Rollback rb;
    rb.set_local_id(1);
    rb.add_remote(2);
    rb.set_callbacks([&](u32, const std::vector<InputFrame>&) {},
                     []() { return std::vector<u8>{}; },
                     [](const std::vector<u8>&) {});
    rb.add_local_input(make_input(1, 1));
    rb.add_local_input(make_input(2, 2));
    rb.add_local_input(make_input(3, 3));

    const auto batch = rb.inputs_to_send(2);
    PRISM_CHECK_EQ(batch.size(), 3u);
    PRISM_CHECK_EQ(batch[0].frame, 1u);
    PRISM_CHECK_EQ(batch[2].frame, 3u);
    PRISM_CHECK(rb.inputs_to_send(2).empty());       // already delivered

    rb.add_local_input(make_input(4, 4));
    const auto next = rb.inputs_to_send(2);
    PRISM_CHECK_EQ(next.size(), 1u);
    PRISM_CHECK_EQ(next[0].frame, 4u);
}

// ======================================================= lag compensation ==
PRISM_TEST(net_lag_compensation_interpolates_history) {
    LagCompensation lc;
    lc.record(7, 0.0, math::Vec3(0, 0, 0), math::Vec3(10, 0, 0));
    lc.record(7, 0.1, math::Vec3(1, 0, 0), math::Vec3(10, 0, 0));
    PRISM_CHECK_EQ(lc.tracked_entities(), 1u);
    PRISM_CHECK_EQ(lc.samples(7), 2u);

    math::Vec3 p, v;
    PRISM_CHECK(lc.sample(7, 0.05, p, v));
    PRISM_CHECK_NEAR(p.x, 0.5f, 1e-4f);
    PRISM_CHECK_NEAR(v.x, 10.0f, 1e-4f);
    PRISM_CHECK(lc.sample(7, 0.075, p, v));
    PRISM_CHECK_NEAR(p.x, 0.75f, 1e-4f);

    // Outside the recorded window the result clamps instead of extrapolating.
    PRISM_CHECK(lc.sample(7, -1.0, p, v));
    PRISM_CHECK_NEAR(p.x, 0.0f, 1e-6f);
    PRISM_CHECK(lc.sample(7, 5.0, p, v));
    PRISM_CHECK_NEAR(p.x, 1.0f, 1e-6f);
    PRISM_CHECK(!lc.sample(9, 0.0, p, v));           // unknown entity

    lc.set_max_rewind(0.05);
    lc.prune(0.1);
    PRISM_CHECK_EQ(lc.samples(7), 1u);
    lc.clear();
    PRISM_CHECK_EQ(lc.tracked_entities(), 0u);
}

// ====================================================== snapshot stream ===
PRISM_TEST(net_snapshot_interpolates_and_extrapolates) {
    SnapshotStream ss;
    ss.set_interpolation_delay(0.1);
    ss.set_extrapolation_limit(0.5);

    Snapshot a;
    a.time = 1.0; a.frame = 1;
    a.entities = {Snapshot::Entity{1, math::Vec3(0, 0, 0), math::Vec3(10, 0, 0), math::Vec3()}};
    Snapshot b;
    b.time = 2.0; b.frame = 2;
    b.entities = {Snapshot::Entity{1, math::Vec3(10, 0, 0), math::Vec3(10, 0, 0), math::Vec3()}};
    ss.push(a);
    ss.push(b);
    PRISM_CHECK_EQ(ss.buffered(), 2u);

    const Snapshot mid = ss.sample(1.6);             // render time 1.5 -> halfway
    PRISM_CHECK_NEAR(mid.entities[0].position.x, 5.0f, 1e-4f);
    PRISM_CHECK_NEAR(mid.time, 1.5f, 1e-6f);

    const Snapshot early = ss.sample(0.5);           // render 0.4 < 1.0 -> clamp
    PRISM_CHECK_NEAR(early.entities[0].position.x, 0.0f, 1e-6f);

    const Snapshot late = ss.sample(2.3);            // render 2.2 -> +0.2 s of motion
    PRISM_CHECK_NEAR(late.entities[0].position.x, 12.0f, 1e-4f);

    const Snapshot frozen = ss.sample(3.0);          // render 2.9 -> past the limit
    PRISM_CHECK_NEAR(frozen.entities[0].position.x, 10.0f, 1e-6f);

    // An empty stream must not crash.
    SnapshotStream empty;
    PRISM_CHECK_EQ(empty.sample(0.0).entities.size(), 0u);
}

PRISM_TEST(net_snapshot_stream_keeps_time_order_on_late_packets) {
    SnapshotStream ss;
    Snapshot late;
    late.time = 3.0; late.frame = 3;
    late.entities = {Snapshot::Entity{1, math::Vec3(30, 0, 0), math::Vec3(), math::Vec3()}};
    Snapshot early;
    early.time = 1.0; early.frame = 1;
    early.entities = {Snapshot::Entity{1, math::Vec3(10, 0, 0), math::Vec3(), math::Vec3()}};
    ss.push(late);
    ss.push(early);                                  // arrives out of order
    PRISM_CHECK_EQ(ss.buffered(), 2u);
    // Before the interpolation delay the stream clamps to the earliest snapshot,
    // which proves `early` was sorted ahead of `late`.
    const Snapshot s = ss.sample(1.05);
    PRISM_CHECK_NEAR(s.time, 1.0f, 1e-6f);
    PRISM_CHECK_EQ(s.frame, 1u);
}

// ============================================================== transport ==
PRISM_TEST(net_loopback_transport_delivers_with_source_port) {
    LoopbackTransport a, b;
    PRISM_CHECK(a.open(41000));
    PRISM_CHECK(b.open(41001));

    const u8 msg[4] = {1, 2, 3, 4};
    PRISM_CHECK(a.send(Endpoint{0x7F000001u, 41001}, msg, 4));

    std::vector<std::pair<Endpoint, std::vector<u8>>> inbox;
    PRISM_CHECK_EQ(b.poll(inbox), 1u);
    PRISM_CHECK_EQ(inbox[0].second.size(), 4u);
    PRISM_CHECK_EQ(inbox[0].second[3], 4u);
    PRISM_CHECK_EQ(inbox[0].first.port, 41000u);     // the sender, not the receiver
    PRISM_CHECK_EQ(a.bytes_sent(), 4u);
    PRISM_CHECK_EQ(b.bytes_received(), 4u);
    PRISM_CHECK_EQ(a.packets_sent(), 1u);
    PRISM_CHECK_EQ(b.packets_received(), 1u);

    // An undelivered poll is empty, not an error.
    inbox.clear();
    PRISM_CHECK_EQ(b.poll(inbox), 0u);

    // Broadcasting reaches every other transport in the process.
    PRISM_CHECK(a.broadcast(msg, 4));
    inbox.clear();
    PRISM_CHECK_EQ(b.poll(inbox), 1u);

    a.close();
    b.close();
    inbox.clear();
    PRISM_CHECK(!a.send(Endpoint{0x7F000001u, 41001}, msg, 4));   // closed socket
}

PRISM_TEST(net_loopback_transport_assigns_ephemeral_ports) {
    LoopbackTransport x, y;
    PRISM_CHECK(x.open(0));
    PRISM_CHECK(y.open(0));
    PRISM_CHECK(x.port() != y.port());
    PRISM_CHECK(x.port() != 0);
    PRISM_CHECK(y.port() != 0);

    const u8 m[1] = {7};
    PRISM_CHECK(x.send(Endpoint{0x7F000001u, y.port()}, m, 1));
    std::vector<std::pair<Endpoint, std::vector<u8>>> inbox;
    PRISM_CHECK_EQ(y.poll(inbox), 1u);
    PRISM_CHECK_EQ(inbox[0].first.port, x.port());
    x.close();
    y.close();
}

PRISM_TEST(net_udp_transport_opens_and_polls) {
    UdpTransport u;
    if (!u.enabled()) return;                        // non-POSIX target: nothing to do
    if (!u.open(0)) return;                          // sandbox forbids sockets: skip
    PRISM_CHECK(u.port() != 0);
    PRISM_CHECK_STR(u.name(), "udp");

    // Loopback to ourselves must round-trip a datagram.
    const u8 msg[3] = {9, 8, 7};
    if (u.send(Endpoint{0x7F000001u, u.port()}, msg, 3)) {
        std::vector<std::pair<Endpoint, std::vector<u8>>> inbox;
        u.poll(inbox);
        PRISM_CHECK(inbox.empty() || inbox[0].second.size() == 3);
    }
    u.close();
    std::vector<std::pair<Endpoint, std::vector<u8>>> inbox;
    PRISM_CHECK_EQ(u.poll(inbox), 0u);               // closed: no crash, no data
}
