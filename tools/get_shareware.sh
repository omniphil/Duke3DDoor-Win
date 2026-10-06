#!/bin/sh
# get_shareware.sh -- fetches the Duke Nukem 3D shareware episode (v1.3D, DUKE3D.GRP) into data/.
#
#     sh tools/get_shareware.sh
#
# The data isn't in the repository: it is 3D Realms', given away as shareware (its LICENSE.TXT lets free BBSs and BBSs
# of 250 nodes or fewer offer it), and never became free software (only the engine did). This takes it from the
# Internet Archive's copy of the shareware release (item "3dduke13") and checks it is exactly that release. It needs
# curl and python3.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
url=https://archive.org/download/3dduke13/3dduke13.zip
want=04e4ca70b8a2d59ed56c451c5c1d5d39
grp=c03558e3a78d1c5356dc69b6134c5b55
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "Downloading $url"
curl -fsSL -o "$tmp/3dduke13.zip" "$url"
got=$(python3 -c "import hashlib,sys; print(hashlib.md5(open(sys.argv[1],'rb').read()).hexdigest())" "$tmp/3dduke13.zip")
if [ "$got" != "$want" ]; then
    echo "The download isn't the expected shareware release (md5 $got, wanted $want)." >&2
    exit 1
fi
mkdir -p "$here/data"
python3 - "$tmp/3dduke13.zip" "$here/data" "$grp" <<'PY'
import hashlib, io, sys, zipfile
outer = zipfile.ZipFile(sys.argv[1])
# DN3DSW13.SHR is the installer's archive: a zip behind a small DOS stub, which zipfile reads as it is
inner = zipfile.ZipFile(io.BytesIO(outer.read('DN3DSW13.SHR')))
for name in ('DUKE3D.GRP', 'LICENSE.TXT'):
    data = inner.read(name)
    if name == 'DUKE3D.GRP' and hashlib.md5(data).hexdigest() != sys.argv[3]:
        sys.exit('DUKE3D.GRP is not the v1.3D shareware file')
    with open(f'{sys.argv[2]}/{name}', 'wb') as f:
        f.write(data)
print(f'DUKE3D.GRP and LICENSE.TXT into {sys.argv[2]}')
PY
