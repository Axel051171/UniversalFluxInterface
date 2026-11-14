"""Write an output file so that an interruption never leaves a half-written image.

The image writers (SCP, HFE, G64, DSK, DC42, downloads) used to open the destination with "wb":
a crash, a full disk or Ctrl-C in the middle left a truncated file under the final name, and an
existing good file of that name was already gone.  write_atomic() writes a temporary file next
to the destination, forces it to the medium, reads it back and compares, and only then puts it
in place with os.replace() (atomic on one file system, on Windows and POSIX).  On any failure
the temporary file is removed and the destination is exactly what it was before.
"""
from __future__ import annotations

import os
import tempfile
from pathlib import Path


def write_atomic(path: str | os.PathLike, data: bytes | bytearray) -> None:
    dest = Path(path)
    data = bytes(data)
    fd, tmp_name = tempfile.mkstemp(prefix=dest.name + ".", suffix=".tmp", dir=dest.parent or ".")
    tmp = Path(tmp_name)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        if tmp.read_bytes() != data:
            raise OSError(f"{dest}: the written file does not read back as written")
        if os.name != "nt":                 # mkstemp makes the file 0600; give it what open("wb") would
            umask = os.umask(0)
            os.umask(umask)
            os.chmod(tmp, 0o666 & ~umask)
        os.replace(tmp, dest)
    except BaseException:
        try:
            tmp.unlink()
        except OSError:
            pass
        raise
