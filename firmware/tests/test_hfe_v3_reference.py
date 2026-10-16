"""Pinned independent HFE cells; no claim of physical copy-protection qualification."""
import hashlib
import subprocess
import sys
import urllib.request
from run_tests import BUILD,FW,compiler,extra_cflags
SHA='e4d39194a67991127457a23114cec3330d92e362feafc483646e70e504ddef02'
BASE='https://raw.githubusercontent.com/dbalsom/fluxfox/1d72ff1b329cc072cec00fe42ae51bededf5374f/tests/images/transylvania/'
def main():
    path=BUILD/'Transylvania.hfe';BUILD.mkdir(exist_ok=True)
    if '--download' in sys.argv:
        for name in ('Transylvania.hfe','LICENSE.txt'):
            dest=BUILD/(name if name.endswith('.hfe') else 'Transylvania.LICENSE.txt')
            if not dest.exists():dest.write_bytes(urllib.request.urlopen(BASE+name,timeout=60).read())
    if not path.exists() and '--if-present' in sys.argv:
        print('SKIP HFEv3 external reference: run test_hfe_v3_reference.py --download');return
    if hashlib.sha256(path.read_bytes()).hexdigest()!=SHA:raise ValueError('reference SHA256 mismatch')
    exe=BUILD/('hfe_v3_reference.exe' if sys.platform=='win32' else 'hfe_v3_reference')
    subprocess.run([compiler(),'-std=c11','-O2','-Wall','-Wextra','-Werror','-D_CRT_SECURE_NO_WARNINGS',
                    *extra_cflags(),'-I',str(FW/'include'),str(FW/'tests/hfe_v3_reference.c'),
                    str(FW/'src/ufi_hfe_v3.c'),str(FW/'src/ufi_image.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(path)],check=True,timeout=30)
if __name__=='__main__':main()
