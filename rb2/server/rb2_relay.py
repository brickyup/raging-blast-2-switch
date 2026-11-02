"""Raging Blast 2 online versus server: pairs players into matches and relays their game traffic.

Players never need port forwarding: each game connects out to this server. Only the machine running it needs its
UDP port reachable (forward it on the router, or run it on a VPS). Python 3.8+, no packages.

    python3 rb2_relay.py [--port 7777]

Protocol (UDP):
  game -> server  "RB2H" + room code (32 bytes, NUL padded; empty = quick match)   resent until matched
  server -> game  "RB2R" + role (1 byte: 1 = player 1, 2 = player 2) + ready (1 byte)
  game <-> game   "RB2P" ... (the lockstep packets, relayed unchanged to the other player of the match)
"""
import argparse
import socket
import time

HELLO, REPLY, DATA = b"RB2H", b"RB2R", b"RB2P"
TIMEOUT = 30.0  # a player silent this long leaves its match


class Relay:
    def __init__(self):
        self.rooms = {}    # room -> [addr of player 1, addr of player 2]
        self.room_of = {}  # addr -> room
        self.seen = {}     # addr -> last packet time
        self.quick = 0     # quick-match rooms opened so far

    def hello(self, addr, room):
        if addr not in self.room_of:
            if not room:  # quick match: join a waiting quick room or open one
                room = next((r for r, m in self.rooms.items() if r.startswith("quick#") and len(m) == 1), None)
                if room is None:
                    self.quick += 1
                    room = f"quick#{self.quick}"
            members = self.rooms.setdefault(room, [])
            if len(members) >= 2:
                return []  # full: ignore until someone leaves
            members.append(addr)
            self.room_of[addr] = room
            print(f"{addr[0]}:{addr[1]} joined room '{room}' as player {len(members)}", flush=True)
        members = self.rooms[self.room_of[addr]]
        ready = len(members) == 2
        # Tell both players their role, so the one already waiting learns the match is complete.
        return [(m, REPLY + bytes([i + 1, ready])) for i, m in enumerate(members)]

    def data(self, addr, packet):
        room = self.room_of.get(addr)
        members = self.rooms.get(room, [])
        return [(m, packet) for m in members if m != addr]

    def expire(self, now):
        for addr in [a for a, t in self.seen.items() if now - t > TIMEOUT]:
            del self.seen[addr]
            room = self.room_of.pop(addr, None)
            if room is not None:
                members = self.rooms[room]
                members.remove(addr)
                print(f"{addr[0]}:{addr[1]} left room '{room}'", flush=True)
                if not members:
                    del self.rooms[room]

    def handle(self, addr, packet, now):
        self.seen[addr] = now
        if packet.startswith(HELLO) and len(packet) == 36:
            return self.hello(addr, packet[4:].rstrip(b"\0").decode("utf-8", "replace"))
        if packet.startswith(DATA):
            return self.data(addr, packet)
        return []


def self_test():
    r = Relay()
    a, b, c = ("1.1.1.1", 1), ("2.2.2.2", 2), ("3.3.3.3", 3)
    room = HELLO + b"goku".ljust(32, b"\0")
    assert r.handle(a, room, 0) == [(a, REPLY + bytes([1, 0]))]
    assert r.handle(b, room, 0) == [(a, REPLY + bytes([1, 1])), (b, REPLY + bytes([2, 1]))]
    assert r.handle(c, room, 0) == []  # room full
    assert r.handle(a, DATA + b"x", 0) == [(b, DATA + b"x")]
    quick = HELLO + bytes(32)
    assert r.handle(c, quick, 0)[0][1][4:] == bytes([1, 0])
    r.expire(TIMEOUT + 1)
    assert not r.rooms and not r.room_of


def main():
    self_test()
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=7777)
    port = parser.parse_args().port
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", port))
    sock.settimeout(1.0)
    print(f"RB2 server on UDP port {port}", flush=True)
    relay = Relay()
    last_expire = time.monotonic()
    while True:
        try:
            packet, addr = sock.recvfrom(2048)
        except socket.timeout:
            packet = None
        except ConnectionResetError:  # Windows reports a closed peer port on the next receive
            continue
        now = time.monotonic()
        if packet:
            for to, out in relay.handle(addr, packet, now):
                sock.sendto(out, to)
        if now - last_expire > 5:
            relay.expire(now)
            last_expire = now


if __name__ == "__main__":
    main()
