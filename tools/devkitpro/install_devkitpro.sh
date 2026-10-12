#!/bin/bash
# install_devkitpro.sh - set up devkitPro (devkitARM, libctru, citro3d, 3DS tools)
# system-wide via its official pacman repositories. Run once:
#     sudo bash tools/devkitpro/install_devkitpro.sh
# Safe to re-run. Afterwards open a new terminal (or `source /etc/profile.d/devkit-env.sh`)
# so DEVKITPRO / DEVKITARM are set.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
if [ "$(id -u)" -ne 0 ]; then echo "run this with sudo" >&2; exit 1; fi

# 1. devkitPro's signing key and keyring package (the server rejects plain
#    curl, so it is fetched with a pacman user agent if not already here)
if [ ! -f "$here/devkitpro-keyring.pkg.tar.zst" ]; then
    curl -fL -A 'pacman/6.1.0 (Linux x86_64) libalpm/14.0.0' -o "$here/devkitpro-keyring.pkg.tar.zst" \
        https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst
fi
pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0
pacman -U --noconfirm "$here/devkitpro-keyring.pkg.tar.zst"

# 2. the repositories (only added once)
if ! grep -q '^\[dkp-libs\]' /etc/pacman.conf; then
    cp /etc/pacman.conf /etc/pacman.conf.before-devkitpro
    cat >> /etc/pacman.conf <<'CONF'

[dkp-libs]
Server = https://pkg.devkitpro.org/packages

[dkp-linux]
Server = https://pkg.devkitpro.org/packages/linux/$arch/
CONF
fi

# 3. the 3DS toolchain and the libraries this port links (zlib)
pacman -Sy --needed --noconfirm 3ds-dev 3ds-zlib

echo
echo "devkitPro installed in /opt/devkitpro. Open a new terminal, then build with:"
echo "    cd ~/Projects/balatro_c/src_3ds && make"
