#!/usr/bin/env bash
#
# Postaví binární Debian balíček sx1255-multimode pro arm64.
#
#   scripts/build_deb.sh [output.deb]
#
set -euo pipefail

cd "$(dirname "$0")/.."

PACKAGE="sx1255-multimode"
VERSION="$(cat VERSION)"
ARCH="$(dpkg --print-architecture)"
OUT="${1:-${PACKAGE}_${VERSION}_${ARCH}.deb}"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

DESTDIR="$STAGE" cmake --install build

# Debian control metadata.
mkdir -p "$STAGE/DEBIAN"
install -m 0755 debian/postinst "$STAGE/DEBIAN/postinst"
install -m 0755 debian/prerm   "$STAGE/DEBIAN/prerm"
install -m 0644 debian/conffiles "$STAGE/DEBIAN/conffiles"
install -m 0644 debian/copyright "$STAGE/usr/share/doc/${PACKAGE}/copyright"

# md5sums for installed files (dpkg integrity metadata).
(cd "$STAGE" && find usr etc lib -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums)

cat > "$STAGE/DEBIAN/control" <<EOF
Package: ${PACKAGE}
Version: ${VERSION}
Section: hamradio
Priority: optional
Architecture: ${ARCH}
Maintainer: Karel Kadlec (OK1LBC) <xkk@post.cz>
Depends: libc6 (>= 2.34), libgcc-s1, libstdc++6, libasound2t64 | libasound2, systemd
Description: multimode SX1255 radio framework (FM/D-Star/M17)
 sx1255-multimode is a modular hotspot/repeater framework for the SX1255 HAT
 on Raspberry Pi Zero 2 W. One RF frontend, one shared IQ stream, and FM
 (noise squelch), D-Star and M17 as independent plugins, plus
 spectrum/waterfall web dashboard, IQ capture/replay, watchdog and automatic
 capture.
EOF

dpkg-deb --build --root-owner-group "$STAGE" "$OUT"

echo
echo "balíček vytvořen: $OUT"
dpkg-deb --info "$OUT" | sed -n '1,20p'
