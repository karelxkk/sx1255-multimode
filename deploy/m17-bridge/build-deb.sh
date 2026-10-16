#!/bin/bash
#
# Sestaví samostatný instalační .deb balíček pro m17-bridge (APRS).
#
# Použití:
#   ./build-deb.sh [výstupní.deb] [cesta/k/m17-bridge]
#
# Pokud je dostupné Go a zdrojový strom m17, binárka se přebuilduje
# z aktuálního zdroje. Jinak se použije zadaná předpřipravená binárka.
#
set -euo pipefail

cd "$(dirname "$0")"

VERSION="0.8.0-2"
PACKAGE="m17-bridge"
ARCH="arm64"
OUT="${1:-build/${PACKAGE}_${VERSION}_${ARCH}.deb}"
BINARY="${2:-/home/xkk/projects/repeater/m17/cmd/m17-bridge/m17-bridge}"
SRC_DIR="${M17_SRC:-/home/xkk/projects/repeater/m17}"

if [ ! -x "${BINARY}" ] && ! { command -v go >/dev/null && [ -d "${SRC_DIR}" ]; }; then
    echo "chyba: binárka m17-bridge nenalezena: ${BINARY}" >&2
    exit 1
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

install -d "${STAGE}/DEBIAN" \
           "${STAGE}/usr/bin" \
           "${STAGE}/etc" \
           "${STAGE}/lib/systemd/system"

if command -v go >/dev/null && [ -d "${SRC_DIR}" ]; then
    echo "building m17-bridge from ${SRC_DIR}"
    (cd "${SRC_DIR}" && go build -ldflags='-s -w' -o "${STAGE}/usr/bin/m17-bridge" ./cmd/m17-bridge)
    chmod 0755 "${STAGE}/usr/bin/m17-bridge"
else
    install -m 0755 "${BINARY}" "${STAGE}/usr/bin/m17-bridge"
fi
install -m 0644 m17-bridge.ini "${STAGE}/etc/m17-bridge.ini"
install -m 0644 m17-bridge.service "${STAGE}/lib/systemd/system/m17-bridge.service"

install -m 0644 debian/control   "${STAGE}/DEBIAN/control"
install -m 0644 debian/conffiles "${STAGE}/DEBIAN/conffiles"
install -m 0755 debian/postinst  "${STAGE}/DEBIAN/postinst"
install -m 0755 debian/prerm     "${STAGE}/DEBIAN/prerm"
install -m 0755 debian/postrm    "${STAGE}/DEBIAN/postrm"

mkdir -p "$(dirname "${OUT}")"
dpkg-deb --root-owner-group --build "${STAGE}" "${OUT}"

echo "balíček vytvořen: ${OUT}"
dpkg-deb --info "${OUT}" | sed -n '1,20p'
