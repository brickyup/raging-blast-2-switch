// Online versus: delay-based lockstep over UDP (one translation unit includes this, via rb2_hooks.h).
//
// Both machines run the same build on the same disc. The unit of synchronisation is one update of the game's pad
// manager (sub_823C1360 polls XInputGetState for users 0..3 through sub_824903E0): at user 0 a new cycle starts,
// the local pad is sent for cycle N + delay, and the cycle waits until both pads for cycle N are known. The host is
// player 1 (user 0), the joiner player 2 (user 1); users 2 and 3 read as disconnected. The game loads files on its
// own threads, so a load may finish on different cycles on the two machines: completions are counted and released
// to the game only when both sides have reached them (rb2_netplay::LoadDone, from the load-status hook).
//
//   rb2.toml:  rb2_netplay = "host"            (player 1; forward rb2_netplay_port/UDP on the router)
//              rb2_netplay = "join"            (player 2) with rb2_netplay_peer = "<host IP>"
//              rb2_netplay_delay = 3           frames of input delay (raise it on slow connections)
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using rb2_socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__SWITCH__)
#include <switch.h>
#endif
using rb2_socket_t = int;
#endif

REXCVAR_DEFINE_STRING(rb2_netplay, "", "RB2",
                      "Online versus: \"server\" (matchmaking through rb2_netplay_server), \"host\" (player 1, "
                      "direct), \"join\" (player 2, direct), empty = off")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(rb2_netplay_server, "", "RB2",
                      "Online versus server (rb2/server/rb2_relay.py): \"name-or-ip:port\"")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(rb2_netplay_room, "", "RB2",
                      "Online versus server: room code shared with a friend; empty = quick match with anyone waiting")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(rb2_netplay_peer, "", "RB2", "Online versus: IP address of the host (join only)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(rb2_netplay_port, 7777, "RB2", "Online versus: UDP port")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(rb2_netplay_delay, 3, "RB2", "Online versus: frames of input delay (1-7)")
    .range(1, 7)  // a packet repeats the last 8 inputs: the peer can never be further behind than that
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace rb2::netplay {

// XINPUT_GAMEPAD as the game sees it (host byte order here; swapped when written to guest memory).
struct Pad {
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  bool operator==(const Pad&) const = default;
};

// One cycle's contribution of one side: its pad and how many file loads it has seen finish so far.
struct Input {
  Pad pad;
  uint32_t loads = 0;
};

constexpr uint32_t kMagic = 0x50324252;  // bytes "RB2P" on the wire (stored little-endian): the server matches them
constexpr uint32_t kRing = 64;           // cycles kept; must exceed delay + network latency in cycles
constexpr uint32_t kResend = 8;          // inputs repeated per packet, so a lost packet is covered by the next

// Wire format: magic, the cycle of inputs[0], how many inputs follow, the inputs (little-endian, both builds are).
struct Packet {
  uint32_t magic;
  uint32_t first;
  uint32_t count;
  Input inputs[kResend];
};

class Session {
 public:
  bool active() const { return active_; }
  // Loads are gated only once lockstep runs: the boot loads finish before the game first polls the pads.
  bool stepping() const { return active_ && stepping_.load(std::memory_order_relaxed); }

  void Start() {
    const std::string mode = REXCVAR_GET(rb2_netplay);
    if (mode != "host" && mode != "join" && mode != "server") return;
    const bool server = mode == "server";
    host_ = mode == "host";
    delay_ = REXCVAR_GET(rb2_netplay_delay);
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#elif defined(__SWITCH__)
    socketInitializeDefault();  // already up when nxlink logging is on: the error is harmless
#endif
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(uint16_t(host_ ? REXCVAR_GET(rb2_netplay_port) : 0));
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
      REXLOG_ERROR("[netplay] cannot open UDP port {}: online play off", REXCVAR_GET(rb2_netplay_port));
      return;
    }
#if defined(_WIN32)
    u_long nonblocking = 1;
    ioctlsocket(sock_, FIONBIO, &nonblocking);
#else
    fcntl(sock_, F_SETFL, fcntl(sock_, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (server) {
      if (!Resolve(REXCVAR_GET(rb2_netplay_server)) || !Matchmake()) return;
    } else if (!host_) {
      if (!Resolve(REXCVAR_GET(rb2_netplay_peer))) return;
    }
    // The first `delay_` cycles have no real input yet: both sides treat them as neutral and agreed.
    local_known_ = remote_known_ = delay_;
    active_ = true;
    REXLOG_WARN("[netplay] {} as player {}, input delay {} frames", server ? "through the server" : "direct",
                host_ ? 1 : 2, delay_);
  }

  // Called at the start of each pad-manager update with the local controller. Returns the two players' pads for
  // this cycle (after waiting for the peer), in player order.
  std::array<Pad, 2> Step(const Pad& local_pad) {
    // Our input for cycle_ + delay_ carries the load count seen so far; it becomes binding only `delay_` cycles on.
    const uint32_t target = cycle_ + delay_;
    local_[target % kRing] = {local_pad, loads_seen_.load(std::memory_order_relaxed)};
    local_known_ = target + 1;
    const auto start = std::chrono::steady_clock::now();
    auto last_send = start - std::chrono::seconds(1);
    bool warned = false;
    while (remote_known_ <= cycle_) {
      const auto now = std::chrono::steady_clock::now();
      if (now - last_send > std::chrono::milliseconds(4)) {
        Send();
        last_send = now;
      }
      Receive();
      if (!warned && now - start > std::chrono::seconds(3)) {
        REXLOG_WARN("[netplay] waiting for the other player (cycle {})", cycle_);
        warned = true;
      }
      if (remote_known_ <= cycle_) std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    Send();
    const Input& mine = local_[cycle_ % kRing];
    const Input& theirs = remote_[cycle_ % kRing];
    agreed_loads_.store(std::min(mine.loads, theirs.loads), std::memory_order_relaxed);
    stepping_.store(true, std::memory_order_relaxed);
    ++cycle_;
    return host_ ? std::array<Pad, 2>{mine.pad, theirs.pad} : std::array<Pad, 2>{theirs.pad, mine.pad};
  }

  // Load-status hook: `ordinal` is this completion's position among all completions on this machine (1-based).
  // Returns true when both machines have reached it at the same cycle.
  bool LoadReleased(uint32_t ordinal) const { return ordinal <= agreed_loads_.load(std::memory_order_relaxed); }
  uint32_t CountLoad() { return loads_seen_.fetch_add(1, std::memory_order_relaxed) + 1; }

 private:
  // "name-or-ip[:port]" (port defaults to rb2_netplay_port) -> peer_.
  bool Resolve(const std::string& where) {
    std::string name = where;
    std::string port = std::to_string(REXCVAR_GET(rb2_netplay_port));
    if (const auto colon = where.rfind(':'); colon != std::string::npos) {
      name = where.substr(0, colon);
      port = where.substr(colon + 1);
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (name.empty() || getaddrinfo(name.c_str(), port.c_str(), &hints, &found) != 0 || !found) {
      REXLOG_ERROR("[netplay] cannot find '{}': online play off", where);
      return false;
    }
    std::memcpy(&peer_, found->ai_addr, sizeof(peer_));
    freeaddrinfo(found);
    have_peer_ = true;
    return true;
  }

  // Server mode: announce the room until the server reports both players present; it decides who is player 1.
  bool Matchmake() {
    struct {
      char magic[4] = {'R', 'B', '2', 'H'};
      char room[32] = {};
    } hello;
    const std::string room = REXCVAR_GET(rb2_netplay_room);
    std::memcpy(hello.room, room.data(), std::min(room.size(), sizeof(hello.room)));
    REXLOG_WARN("[netplay] looking for a match on {} ({})", REXCVAR_GET(rb2_netplay_server),
                room.empty() ? "quick match" : "room " + room);
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::minutes(5)) {
      sendto(sock_, reinterpret_cast<const char*>(&hello), sizeof(hello), 0, reinterpret_cast<const sockaddr*>(&peer_),
             sizeof(peer_));
      for (int i = 0; i < 50; ++i) {
        uint8_t reply[6];
        const auto n = recvfrom(sock_, reinterpret_cast<char*>(reply), sizeof(reply), 0, nullptr, nullptr);
        if (n == 6 && std::memcmp(reply, "RB2R", 4) == 0 && reply[5]) {
          host_ = reply[4] == 1;
          REXLOG_WARN("[netplay] matched: you are player {}", host_ ? 1 : 2);
          return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    REXLOG_ERROR("[netplay] no match found within 5 minutes: online play off");
    return false;
  }

  void Send() {
    if (!have_peer_) return;
    Packet p{};
    p.magic = kMagic;
    p.count = std::min<uint32_t>(kResend, local_known_);
    p.first = local_known_ - p.count;
    for (uint32_t i = 0; i < p.count; ++i) p.inputs[i] = local_[(p.first + i) % kRing];
    sendto(sock_, reinterpret_cast<const char*>(&p), sizeof(p), 0, reinterpret_cast<const sockaddr*>(&peer_),
           sizeof(peer_));
  }

  void Receive() {
    Packet p;
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    while (true) {
      const auto n = recvfrom(sock_, reinterpret_cast<char*>(&p), sizeof(p), 0, reinterpret_cast<sockaddr*>(&from),
                              &from_len);
      if (n <= 0) return;  // nothing waiting
      if (n != int(sizeof(p)) || p.magic != kMagic || p.count > kResend) continue;  // e.g. a late matchmaking reply
      if (!have_peer_) {  // host: the first valid packet tells who the joiner is
        peer_ = from;
        have_peer_ = true;
        REXLOG_WARN("[netplay] player 2 connected from {}", inet_ntoa(from.sin_addr));
      }
      for (uint32_t i = 0; i < p.count; ++i) {
        const uint32_t c = p.first + i;
        if (c == remote_known_ && c < cycle_ + kRing) {
          remote_[c % kRing] = p.inputs[i];
          ++remote_known_;
        }
      }
    }
  }

  bool active_ = false, host_ = false, have_peer_ = false;
  uint32_t delay_ = 3, cycle_ = 0, local_known_ = 0, remote_known_ = 0;
  rb2_socket_t sock_{};
  sockaddr_in peer_{};
  std::array<Input, kRing> local_{}, remote_{};
  std::atomic<uint32_t> loads_seen_{0}, agreed_loads_{0};
  std::atomic<bool> stepping_{false};
};

inline Session& Get() {
  static Session session;
  return session;
}

}  // namespace rb2::netplay

extern "C" REX_FUNC(__imp__sub_824903E0);

// XInputGetState(user r3, XINPUT_STATE* r4) -> r3 status. With netplay on, both players' pads come from the session.
REX_HOOK_RAW(sub_824903E0) {
  REX_FUNC_PROLOGUE();
  auto& net = rb2::netplay::Get();
  static bool started = false;
  if (!started) {
    started = true;
    net.Start();
  }
  if (!net.active()) {
    __imp__sub_824903E0(ctx, base);
    return;
  }
  const uint32_t user = ctx.r3.u32, state = ctx.r4.u32;
  static std::array<rb2::netplay::Pad, 2> pads{};
  static uint32_t packets[2] = {};
  static std::array<rb2::netplay::Pad, 2> last{};
  if (user == 0) {
    // New pad-manager cycle: read this machine's controller (always the local user 0), then lockstep.
    __imp__sub_824903E0(ctx, base);
    rb2::netplay::Pad local{};
    if (ctx.r3.u32 == 0) {
      const uint8_t* g = base + state + 4;
      local.buttons = uint16_t(g[0] << 8 | g[1]);
      local.lt = g[2];
      local.rt = g[3];
      local.lx = int16_t(g[4] << 8 | g[5]);
      local.ly = int16_t(g[6] << 8 | g[7]);
      local.rx = int16_t(g[8] << 8 | g[9]);
      local.ry = int16_t(g[10] << 8 | g[11]);
    }
    pads = net.Step(local);
  }
  if (user > 1) {
    ctx.r3.u64 = 1167;  // ERROR_DEVICE_NOT_CONNECTED
    return;
  }
  // The game only takes a pad whose packet number changed: count changes the same way on both machines.
  const rb2::netplay::Pad& p = pads[user];
  if (!(p == last[user])) {
    last[user] = p;
    ++packets[user];
  }
  uint8_t* g = base + state;
  const uint32_t packet = packets[user];
  const uint8_t bytes[16] = {uint8_t(packet >> 24), uint8_t(packet >> 16), uint8_t(packet >> 8), uint8_t(packet),
                             uint8_t(p.buttons >> 8), uint8_t(p.buttons), p.lt, p.rt,
                             uint8_t(uint16_t(p.lx) >> 8), uint8_t(p.lx), uint8_t(uint16_t(p.ly) >> 8), uint8_t(p.ly),
                             uint8_t(uint16_t(p.rx) >> 8), uint8_t(p.rx), uint8_t(uint16_t(p.ry) >> 8), uint8_t(p.ry)};
  std::memcpy(g, bytes, sizeof(bytes));
  ctx.r3.u64 = 0;
}
