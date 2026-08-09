// Simple standalone test for PacketClock timestamp normalization.
// Compile with: g++ -std=c++17 -I../include test_packet_clock.cpp ../src/util/PacketClock.cpp ../src/util/Config.cpp -lavformat -lavcodec -lavutil -lyaml-cpp -o test_packet_clock

#include "streamer/PacketClock.h"

#include <cassert>
#include <cstdio>

extern "C" {
#include <libavutil/avutil.h>
}

int main() {
    std::printf("Testing PacketClock edge cases...\n");

    // Construct a minimal fake AVStream with time_base 1/1000 (milliseconds).
    AVStream fakeStream{};
    fakeStream.time_base = {1, 1000};

    streamer::PacketClock clock(&fakeStream, 90000);

    // Test 1: Normal monotonic packet (10ms in 1/1000 timebase = 10 ticks)
    // Should rescale to 90000 * 0.010s = 900 in 90000 timebase.
    AVPacket pkt1{};
    pkt1.pts = 10;  // 10ms in source
    pkt1.dts = 10;
    clock.normalizePacket(&pkt1);
    std::printf("Test 1 (normal): pts=%lld (expected 900), dts=%lld\n",
                static_cast<long long>(pkt1.pts),
                static_cast<long long>(pkt1.dts));
    assert(pkt1.pts == 900 && pkt1.dts == 900);

    // Test 2: Next packet 10ms later (20ms in source = 1800 in output)
    AVPacket pkt2{};
    pkt2.pts = 20;
    pkt2.dts = 20;
    clock.normalizePacket(&pkt2);
    std::printf("Test 2 (monotonic increase): pts=%lld (expected 1800), dts=%lld\n",
                static_cast<long long>(pkt2.pts),
                static_cast<long long>(pkt2.dts));
    assert(pkt2.pts == 1800 && pkt2.dts == 1800);

    // Test 3: Backward PTS (jitter: 15ms, less than previous 20ms)
    // Should be clamped to lastPts + 1 = 1801.
    AVPacket pkt3{};
    pkt3.pts = 15;  // 15ms in source (backward from 20)
    pkt3.dts = 15;
    clock.normalizePacket(&pkt3);
    std::printf("Test 3 (backward/jitter): pts=%lld (expected 1801 after clamp), dts=%lld\n",
                static_cast<long long>(pkt3.pts),
                static_cast<long long>(pkt3.dts));
    assert(pkt3.pts == 1801 && pkt3.dts == 1801);

    // Test 4: Packet with no PTS (AV_NOPTS_VALUE = -0x8000000000000000LL)
    AVPacket pkt4{};
    pkt4.pts = AV_NOPTS_VALUE;
    pkt4.dts = 30;  // DTS is valid
    clock.normalizePacket(&pkt4);
    std::printf("Test 4 (no PTS): pts=%lld (expected AV_NOPTS_VALUE), dts=%lld (expected 2700)\n",
                static_cast<long long>(pkt4.pts),
                static_cast<long long>(pkt4.dts));
    assert(pkt4.pts == AV_NOPTS_VALUE && pkt4.dts == 2700);

    // Test 5: Large gap in timestamps (simulating network glitch recovery)
    // Source jumps from 30ms to 100ms (rescales to 9000 in output).
    AVPacket pkt5{};
    pkt5.pts = 100;
    pkt5.dts = 100;
    clock.normalizePacket(&pkt5);
    std::printf("Test 5 (large gap): pts=%lld (expected 9000), dts=%lld\n",
                static_cast<long long>(pkt5.pts),
                static_cast<long long>(pkt5.dts));
    assert(pkt5.pts == 9000 && pkt5.dts == 9000);

    std::printf("\nAll PacketClock tests passed!\n");
    return 0;
}
