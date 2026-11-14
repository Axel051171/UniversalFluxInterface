"""write_atomic(): an interrupted or failed export leaves the destination as it was and no
temporary file behind; every image writer goes through it."""
from __future__ import annotations

import os
import re
from pathlib import Path

import pytest

from ufi_host import atomic
from ufi_host.scp import ScpRevolution, write_scp


def test_new_file_and_replacement(tmp_path):
    dest = tmp_path / "disk.scp"
    atomic.write_atomic(dest, b"first")
    assert dest.read_bytes() == b"first" and os.listdir(tmp_path) == ["disk.scp"]
    atomic.write_atomic(str(dest), bytearray(b"second, longer"))
    assert dest.read_bytes() == b"second, longer" and os.listdir(tmp_path) == ["disk.scp"]
    atomic.write_atomic(dest, b"")
    assert dest.read_bytes() == b""


@pytest.mark.parametrize("fail", ["write", "fsync", "readback", "replace", "interrupt"])
def test_failure_keeps_the_old_file(tmp_path, monkeypatch, fail):
    dest = tmp_path / "disk.hfe"
    dest.write_bytes(b"good image")
    error: type[BaseException] = KeyboardInterrupt if fail == "interrupt" else OSError

    def boom(*a, **k):
        raise error("simulated")

    if fail in ("write", "interrupt"):
        real = os.fdopen

        def fdopen(fd, mode):
            f = real(fd, mode)

            class Half:
                def __enter__(self):
                    return self

                def __exit__(self, *exc):
                    f.close()

                def write(self, data):
                    f.write(data[: len(data) // 2])     # half of it reached the file
                    boom()

                flush = f.flush
                fileno = f.fileno

            return Half()

        monkeypatch.setattr(atomic.os, "fdopen", fdopen)
    elif fail == "fsync":
        monkeypatch.setattr(atomic.os, "fsync", boom)
    elif fail == "readback":
        monkeypatch.setattr(Path, "read_bytes", lambda self: b"not what was written")
    else:
        monkeypatch.setattr(atomic.os, "replace", boom)
    with pytest.raises(error):
        atomic.write_atomic(dest, b"new image, much longer than the old one")
    monkeypatch.undo()
    assert dest.read_bytes() == b"good image"
    assert os.listdir(tmp_path) == ["disk.hfe"]             # no temporary file left


def test_missing_directory_fails_before_anything_is_written(tmp_path):
    with pytest.raises(OSError):
        atomic.write_atomic(tmp_path / "nowhere" / "x.scp", b"data")
    assert os.listdir(tmp_path) == []


def test_scp_writer_is_atomic(tmp_path, monkeypatch):
    dest = tmp_path / "t.scp"
    revs = [ScpRevolution(8_000_000, [4000] * 100)]
    write_scp(str(dest), {0: revs}, 1)
    good = dest.read_bytes()
    assert good[:3] == b"SCP"
    monkeypatch.setattr(atomic.os, "replace", lambda *a: (_ for _ in ()).throw(OSError("disk full")))
    with pytest.raises(OSError):
        write_scp(str(dest), {0: revs, 2: revs}, 1)
    assert dest.read_bytes() == good and os.listdir(tmp_path) == ["t.scp"]


def test_no_writer_opens_its_destination_directly():
    """Every binary output of the package goes through write_atomic(); a new writer that opens
    its destination with "wb" fails here."""
    package = Path(atomic.__file__).parent
    direct = [f"{p.name}:{n}" for p in sorted(package.glob("*.py")) if p.name != "atomic.py"
              for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1)
              if re.search(r"""open\([^)]*['"]wb['"]""", line)]
    assert direct == []
