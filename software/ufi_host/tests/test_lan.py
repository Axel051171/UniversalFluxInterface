"""LAN host side (doc 1.6): NET 0x48 and discovery replies, AUTH with HMAC-SHA256, the TCP
stream under Device2, `ufi net` / `ufi discover` / `--host`, and the bring-up LAN step."""
from __future__ import annotations

import hashlib
import hmac
import socket
import struct
import threading

import pytest
from test_bringup import FakeV10, _steps, clock  # noqa: F401  (pytest fixture)
from test_protocol2 import FakeV2

from ufi_host import bringup, cli
from ufi_host import protocol2 as P2

MAC = bytes([0x02, 0x20, 0x64, 0x02, 0xCA, 0xB3])


def net_reply(state=5, mode=1, flags=0x07, owner=0, version=4, host=b"ufi-02cab3", lease=3600,
              peer=(0, 0, 0, 0), peer_port=0):
    b = bytes([1, state, mode, flags, 192, 168, 1, 50, 255, 255, 255, 0, 192, 168, 1, 1, 9, 9, 9, 9]) + MAC
    b += struct.pack("<HHI", 55100, 55100, lease) + bytes(peer) + struct.pack("<H", peer_port)
    return b + bytes([owner, version, len(host)]) + host


def disc_reply(flags=8, host=b"ufi-02cab3", ip=(192, 168, 1, 50)):
    return b"UFI!" + bytes([1, flags]) + struct.pack("<H", 55100) + bytes(ip) + MAC + bytes([1, 18, 10, len(host)]) + host


class FakeLan(FakeV10):
    """FakeV10 with the LAN ops: NET answers `net`, AUTH runs the firmware challenge; with a key
    every other request is status 25 until the challenge is answered (as ufi_lan_session.c)."""

    def __init__(self, key=b"", net=None, **kw):
        super().__init__(**kw)
        self.key, self.net = key, net or net_reply()
        self.authed = not key
        self.challenge = None

    def _handle(self, f: P2.Frame):
        r = lambda status=0, payload=b"": self._frame(P2.T_RESPONSE, f.cmd, f.seq, status, payload)
        if f.type == P2.T_REQUEST and f.cmd == P2.AUTH:
            if not f.payload:
                self.challenge = bytes(range(7, 39))
                r(0, bytes([1, (1 if self.key else 0) | (2 if self.authed else 0)]) + self.challenge)
            elif len(f.payload) != 32:
                r(16)
            elif not self.key:
                r(0)
            else:
                ok = self.challenge is not None and hmac.compare_digest(
                    hmac.new(self.key, self.challenge, hashlib.sha256).digest(), bytes(f.payload))
                self.challenge = None
                self.authed = self.authed or ok
                r(0 if ok else P2.AUTH_FAILED)
            return
        if not self.authed:
            if f.type == P2.T_REQUEST:
                r(P2.AUTH_REQUIRED)
            return
        if f.type == P2.T_REQUEST and f.cmd == P2.NET:
            r(0, self.net) if not f.payload else r(16)
            return
        super()._handle(f)


def test_net_status_unpack_and_text():
    n = P2.NetStatus.unpack(net_reply(flags=0x0F | 0x40, owner=2, peer=(192, 168, 1, 20), peer_port=50123))
    assert n.bound and n.ip == "192.168.1.50" and n.mask == "255.255.255.0" and n.gw == "192.168.1.1"
    assert n.mac == "02:20:64:02:ca:b3" and n.port == 55100 and n.discover_port == 55100 and n.lease_s == 3600
    assert n.peer == "192.168.1.20" and n.peer_port == 50123 and n.owner == 2 and n.hostname == "ufi-02cab3"
    text = str(n)
    assert "address in use" in text and "DHCP: 192.168.1.50/255.255.255.0" in text and "lan_key set" in text
    assert "session from 192.168.1.20:50123" in text and "drive owner: LAN" in text
    assert "no link" in str(P2.NetStatus.unpack(net_reply(state=3, flags=0)))
    for bad in (b"", net_reply()[:42], bytes([2]) + net_reply()[1:], net_reply()[:-1]):
        with pytest.raises(ValueError):
            P2.NetStatus.unpack(bad)


def test_discover_reply_unpack():
    r = P2.DiscoverReply.unpack(disc_reply(flags=8 | 2 | 4), "192.168.1.50")
    assert r.ip == "192.168.1.50" and r.port == 55100 and r.mac == "02:20:64:02:ca:b3"
    assert (r.fw_major, r.fw_minor, r.board_rev, r.hostname) == (1, 18, 10, "ufi-02cab3")
    assert "DHCP" in str(r) and "key" in str(r) and "busy" in str(r)
    for bad in (b"UFI?" + disc_reply()[4:], disc_reply()[:21], b"UFI!\x02" + disc_reply()[5:]):
        with pytest.raises(ValueError):
            P2.DiscoverReply.unpack(bad)


def test_net_over_device2():
    dev = P2.Device2(FakeLan())
    n = dev.net()
    assert n.state == 5 and n.mode == 1 and n.version == 4
    old = P2.Device2(FakeV2())                    # firmware before L5
    with pytest.raises(P2.DeviceError) as e:
        old.net()
    assert e.value.status == P2.UNKNOWN_COMMAND


def test_auth_with_key():
    fake = FakeLan(key=b"s3cret")
    dev = P2.Device2(fake)
    with pytest.raises(P2.DeviceError) as e:
        dev.ping()
    assert e.value.status == P2.AUTH_REQUIRED and "lan-key" in str(e.value)
    with pytest.raises(P2.DeviceError) as e:
        dev.auth("wrong")
    assert e.value.status == P2.AUTH_FAILED
    dev.auth("s3cret")
    assert fake.authed and dev.ping()
    open_dev = P2.Device2(FakeLan())
    open_dev.auth("anything")                     # no key on the device: nothing to prove
    assert open_dev.ping()


def test_connect_tcp_with_a_stream_double():
    fake = FakeLan(key=b"k")
    fake.close = lambda: None
    dev = P2.connect_tcp("192.0.2.1", key="k", stream=fake)
    assert fake.authed and dev.max_payload == 4096
    with pytest.raises(P2.DeviceError):
        bad = FakeLan(key=b"k")
        bad.close = lambda: None
        P2.connect_tcp("192.0.2.1", key="bad", stream=bad)


def test_tcp_stream_over_a_socket_pair():
    a, b = socket.socketpair()
    try:
        s = P2.TcpStream("unused", sock=a)
        assert s.write(b"\x55\xaa") == 2 and b.recv(2) == b"\x55\xaa"
        assert s.read(10) == b""                  # nothing there: the 0.2 s poll ends empty
        b.sendall(b"abc")
        assert s.read(10) == b"abc"
        b.close()
        with pytest.raises(ConnectionError):
            s.read(10)
    finally:
        a.close()


def test_device2_round_trip_over_tcp_loopback():
    """A real TCP connection on 127.0.0.1 carrying v2 frames to a FakeLan behind a thread."""
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port = server.getsockname()[1]
    fake = FakeLan(key=b"loop")

    def serve():
        conn, _ = server.accept()
        conn.settimeout(2)
        try:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                fake.write(data)
                out = fake.read(1 << 16)
                while out:
                    conn.sendall(out)
                    out = fake.read(1 << 16)
        except OSError:
            pass
        finally:
            conn.close()

    t = threading.Thread(target=serve, daemon=True)
    t.start()
    dev = P2.connect_tcp("127.0.0.1", port, timeout=3, key="loop")
    try:
        assert dev.net().ip == "192.168.1.50"
    finally:
        dev.s.close()
        server.close()
    t.join(3)


class FakeUdp:
    def __init__(self, replies):
        self.replies, self.sent, self.closed = list(replies), [], False

    def setsockopt(self, *a):
        pass

    def settimeout(self, t):
        pass

    def sendto(self, data, addr):
        self.sent.append((data, addr))

    def recvfrom(self, n):
        if not self.replies:
            raise socket.timeout()
        return self.replies.pop(0)

    def close(self):
        self.closed = True


def test_discover_collects_and_sorts():
    sock = FakeUdp([(disc_reply(ip=(192, 168, 1, 77)), ("192.168.1.77", 55100)), (b"junk", ("10.0.0.1", 55100)),
                    (disc_reply(), ("192.168.1.50", 55100)), (disc_reply(), ("192.168.1.50", 55100))])
    found = P2.discover(0.3, sock=sock)
    assert [r.ip for r in found] == ["192.168.1.50", "192.168.1.77"] and sock.closed
    assert sock.sent == [(b"UFI?\x01\x00\x00\x00", ("255.255.255.255", 55100))]


def test_cli_discover_and_net(monkeypatch, capsys):
    monkeypatch.setattr(P2, "discover", lambda timeout, address: [P2.DiscoverReply.unpack(disc_reply())])
    assert cli.main(["discover"]) == 0
    assert "192.168.1.50:55100" in capsys.readouterr().out
    monkeypatch.setattr(P2, "discover", lambda timeout, address: [])
    assert cli.main(["discover"]) == 1
    monkeypatch.setattr(P2, "connect", lambda port=None, timeout=5.0: P2.Device2(FakeLan()))
    assert cli.main(["net"]) == 0
    assert "address in use" in capsys.readouterr().out
    seen = {}

    def fake_tcp(host, port, key=None):
        seen.update(host=host, port=port, key=key)
        return P2.Device2(FakeLan())
    monkeypatch.setattr(P2, "connect_tcp", fake_tcp)
    assert cli.main(["--host", "192.168.1.50", "--lan-key", "k", "--lan-port", "6000", "net"]) == 0
    assert seen == dict(host="192.168.1.50", port=6000, key="k")


@pytest.mark.parametrize("net,status,text", [
    (net_reply(), "ok", "DHCP 192.168.1.50/255.255.255.0"),
    (net_reply(state=3, flags=0), "warn", "cable"),
    (net_reply(state=4, flags=1), "warn", "link, no address"),
    (net_reply(state=1, version=0), "fail", "VERSIONR 0x00"),
    (net_reply(state=2, flags=0), "skip", "lan=off"),
], ids=["bound", "no-link", "no-address", "no-chip", "off"])
def test_bringup_lan_step(clock, net, status, text):
    fake = FakeLan(net=net)
    clock["rtcs"].append(fake)
    rep = bringup.Report()
    bringup.run(P2.Device2(fake), rep, expect_rev=10)
    st = _steps(rep)["lan"]
    assert st.status == status and text in st.detail, st.detail
