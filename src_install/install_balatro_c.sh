#!/usr/bin/env bash
# install_balatro_c.sh - set up balatro_c (the Nintendo 3DS port of Balatro) on a 3DS SD card.
#
# balatro_c does not include any of Balatro's own files. This script takes them from YOUR
# copy of Balatro (installed through Steam on this computer) and copies them, together with
# balatro_c.3dsx, to a homebrewed 3DS SD card (or the Azahar emulator's virtual SD card, --azahar):
#
#   <SD>/3ds/balatro_c/balatro_c.3dsx
#   <SD>/3ds/balatro_c/assets/...      (textures, sounds, fonts, shaders, localization)
#
# Usage: ./install_balatro_c.sh [options]
#   --sd PATH       the SD card's root folder (default: found automatically)
#   --azahar        install into the Azahar emulator's virtual SD card instead
#   --game PATH     Balatro.exe, Balatro.love or the Balatro install folder
#                   (default: found in your Steam libraries)
#   --3dsx PATH     the balatro_c.3dsx to install (default: the one next to this script)
#   --no-3dsx       copy only the assets
#   -y, --yes       do not ask for confirmation
#   -h, --help      show this help
#
# Works on Linux (including the Steam Deck), macOS, and on Windows from Git Bash, MSYS2 or WSL.
# Needs one of: unzip, python3, bsdtar (to read the game's archive).

set -u

EXPECTED_VERSION="1.0.1o"   # the Balatro version balatro_c was ported from
APPID=2379780               # Balatro's Steam app id
ASSET_FORMAT=1              # bump when the asset layout the game expects changes
MIN_FREE_KB=$((60 * 1024))  # the assets take about 50MB

here="$(cd "$(dirname "$0")" && pwd)"

# ---------------------------------------------------------------- output helpers
info() { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
ask_yes() {   # ask_yes "question" -> 0 for yes
    [ "$ASSUME_YES" = 1 ] && return 0
    local a
    printf '%s [y/N] ' "$1"
    read -r a || return 1
    case "$a" in y|Y|yes|YES|Yes) return 0 ;; *) return 1 ;; esac
}
usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# ---------------------------------------------------------------- options
SD="" GAME="" DSX="" NO_3DSX=0 ASSUME_YES=0 AZAHAR=0
while [ $# -gt 0 ]; do
    case "$1" in
        --sd) [ $# -ge 2 ] || die "--sd needs a path"; SD="$2"; shift 2 ;;
        --game) [ $# -ge 2 ] || die "--game needs a path"; GAME="$2"; shift 2 ;;
        --3dsx) [ $# -ge 2 ] || die "--3dsx needs a path"; DSX="$2"; shift 2 ;;
        --azahar) AZAHAR=1; shift ;;
        --no-3dsx) NO_3DSX=1; shift ;;
        -y|--yes) ASSUME_YES=1; shift ;;
        -h|--help) usage ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
done

# ---------------------------------------------------------------- platform
OS=linux
case "$(uname -s)" in
    Darwin) OS=mac ;;
    MINGW*|MSYS*|CYGWIN*) OS=win ;;
    Linux) grep -qi microsoft /proc/version 2>/dev/null && OS=wsl ;;
esac

# ---------------------------------------------------------------- reading the game's archive
# Balatro.exe is a LOVE executable: a zip archive appended to the program. Balatro.love
# (macOS) is a plain zip. unzip and python's zipfile both read either.
ZIPTOOL=""
if command -v unzip >/dev/null 2>&1; then ZIPTOOL=unzip
elif command -v python3 >/dev/null 2>&1; then ZIPTOOL=python3
elif command -v bsdtar >/dev/null 2>&1; then ZIPTOOL=bsdtar
else die "need unzip, python3 or bsdtar to read the game's files"; fi

zip_cat() {   # zip_cat ARCHIVE MEMBER -> member contents on stdout
    case "$ZIPTOOL" in
        unzip) unzip -p "$1" "$2" 2>/dev/null ;;
        python3) python3 -c 'import sys,zipfile; sys.stdout.buffer.write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))' "$1" "$2" 2>/dev/null ;;
        bsdtar) bsdtar -xOf "$1" "$2" 2>/dev/null ;;
    esac
}
zip_extract() {   # zip_extract ARCHIVE DEST PATTERN... (shell-style patterns, e.g. 'localization/*')
    local a="$1" d="$2"; shift 2
    case "$ZIPTOOL" in
        unzip)
            unzip -qq -o "$a" "$@" -d "$d" 2>/dev/null
            [ $? -le 1 ] ;;   # 1 = warnings only (the program in front of the archive)
        python3)
            python3 - "$a" "$d" "$@" <<'PY'
import sys, zipfile, fnmatch, os
a, d, pats = sys.argv[1], sys.argv[2], sys.argv[3:]
z = zipfile.ZipFile(a)
for n in z.namelist():
    if not n.endswith('/') and any(fnmatch.fnmatchcase(n, p) for p in pats):
        z.extract(n, d)
PY
            ;;
        bsdtar) (cd "$d" && bsdtar -xf "$a" "$@" 2>/dev/null) ;;
    esac
}

# ---------------------------------------------------------------- finding the game
steam_roots() {   # Steam install folders that may exist on this system
    case "$OS" in
        linux)
            printf '%s\n' "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root" \
                "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam" "$HOME/snap/steam/common/.local/share/Steam" ;;
        mac) printf '%s\n' "$HOME/Library/Application Support/Steam" ;;
        win) printf '%s\n' "/c/Program Files (x86)/Steam" "/c/Program Files/Steam" ;;
        wsl) printf '%s\n' "/mnt/c/Program Files (x86)/Steam" "/mnt/c/Program Files/Steam" ;;
    esac
}
to_local_path() {   # a Windows path from libraryfolders.vdf ("D:\\Games\\Steam") as a local path
    local p="$1"
    case "$OS" in
        win|wsl)
            case "$p" in
                [A-Za-z]:*)
                    local drive rest
                    drive="$(printf '%s' "${p%%:*}" | tr 'A-Z' 'a-z')"
                    rest="$(printf '%s' "${p#?:}" | sed 's#\\\\#/#g; s#\\#/#g')"
                    if [ "$OS" = wsl ]; then p="/mnt/$drive$rest"; else p="/$drive$rest"; fi ;;
            esac ;;
    esac
    printf '%s\n' "$p"
}
steam_libraries() {   # every Steam library folder (each root plus the ones libraryfolders.vdf lists)
    local r line
    steam_roots | while IFS= read -r r; do
        [ -d "$r/steamapps" ] || continue
        printf '%s\n' "$r"
        [ -f "$r/steamapps/libraryfolders.vdf" ] || continue
        grep '"path"' "$r/steamapps/libraryfolders.vdf" | sed 's/.*"path"[[:space:]]*"\(.*\)".*/\1/' |
            while IFS= read -r line; do to_local_path "$line"; done
    done | awk '!seen[$0]++'
}
game_archive_in() {   # game_archive_in DIR -> the archive holding the game's files
    if [ -f "$1/Balatro.exe" ]; then printf '%s\n' "$1/Balatro.exe"
    elif [ -f "$1/Balatro.app/Contents/Resources/Balatro.love" ]; then printf '%s\n' "$1/Balatro.app/Contents/Resources/Balatro.love"
    elif [ -f "$1/Contents/Resources/Balatro.love" ]; then printf '%s\n' "$1/Contents/Resources/Balatro.love"
    else return 1; fi
}

ARCHIVE=""
if [ -n "$GAME" ]; then
    if [ -f "$GAME" ]; then ARCHIVE="$GAME"
    elif [ -d "$GAME" ]; then ARCHIVE="$(game_archive_in "$GAME")" || die "no Balatro.exe or Balatro.love in $GAME"
    else die "not found: $GAME"; fi
else
    while IFS= read -r lib; do
        [ -f "$lib/steamapps/appmanifest_$APPID.acf" ] || continue   # installed through this Steam library
        if a="$(game_archive_in "$lib/steamapps/common/Balatro")"; then ARCHIVE="$a"; break; fi
    done <<EOF
$(steam_libraries)
EOF
    [ -n "$ARCHIVE" ] || die "Balatro was not found in your Steam libraries. Install it through Steam, or point to it with --game PATH."
fi
info "Balatro: $ARCHIVE"

# the game's version (first line of version.jkr, e.g. "1.0.1o-FULL")
version="$(zip_cat "$ARCHIVE" version.jkr | head -n 1 | tr -d '\r')"
[ -n "$version" ] || die "$ARCHIVE does not look like Balatro (no version.jkr inside)"
case "$version" in
    "$EXPECTED_VERSION"|"$EXPECTED_VERSION"-*) info "version: $version" ;;
    *) die "your Balatro is version $version, but balatro_c needs $EXPECTED_VERSION. Its files would not match the port." ;;
esac

# ---------------------------------------------------------------- the .3dsx
if [ "$NO_3DSX" = 0 ]; then
    if [ -z "$DSX" ]; then
        for c in "$here/balatro_c.3dsx" "$here/../build_3ds/balatro_c.3dsx"; do
            [ -f "$c" ] && { DSX="$c"; break; }
        done
    fi
    [ -n "$DSX" ] && [ -f "$DSX" ] || die "balatro_c.3dsx not found next to this script (use --3dsx PATH, or --no-3dsx for the assets only)"
fi

# ---------------------------------------------------------------- finding the SD card
sd_candidates() {   # mounted volumes that have a /3ds folder (homebrewed 3DS SD cards)
    local d
    case "$OS" in
        linux) u="${USER:-$(id -un)}"; set -- /run/media/"$u"/* /media/"$u"/* /media/* /mnt/* ;;
        mac) set -- /Volumes/* ;;
        win) set -- /[d-z] ;;
        wsl) set -- /mnt/[d-z] ;;
    esac
    for d in "$@"; do [ -d "$d/3ds" ] && printf '%s\n' "$d"; done | awk '!seen[$0]++'
}

azahar_sdmc() {   # the Azahar emulator's virtual SD card (its user folder must exist)
    local d
    case "$OS" in
        linux) set -- "${XDG_DATA_HOME:-$HOME/.local/share}/azahar-emu" "$HOME/.var/app/org.azahar_emu.Azahar/data/azahar-emu" ;;
        mac) set -- "$HOME/Library/Application Support/Azahar" ;;
        win) set -- "$(cygpath -u "${APPDATA:-}" 2>/dev/null)/Azahar" ;;
        wsl) set -- /mnt/c/Users/*/AppData/Roaming/Azahar ;;
    esac
    for d in "$@"; do [ -d "$d" ] && { printf '%s\n' "$d/sdmc"; return; }; done
}

if [ -z "$SD" ] && [ "$AZAHAR" = 1 ]; then
    SD="$(azahar_sdmc)"
    [ -n "$SD" ] || die "Azahar's user folder was not found; start Azahar once, or give its sdmc folder with --sd"
    mkdir -p "$SD/3ds" || die "could not create $SD/3ds"
elif [ "$AZAHAR" = 1 ]; then
    mkdir -p "${SD%/}/3ds" || die "could not create $SD/3ds"
fi
if [ -z "$SD" ]; then
    cands="$(sd_candidates)"
    n=0; [ -n "$cands" ] && n="$(printf '%s\n' "$cands" | wc -l | tr -d ' ')"
    if [ "$n" = 0 ]; then
        info "No SD card with a /3ds folder was found. Insert your 3DS SD card (homebrewed, with a /3ds folder),"
        printf 'or type the path to its root folder: '
        read -r SD || die "no SD card given"
    elif [ "$n" = 1 ]; then
        SD="$cands"
    else
        info "Several possible 3DS SD cards were found:"
        printf '%s\n' "$cands" | awk '{ printf "  %d) %s\n", NR, $0 }'
        printf 'Which one? [1-%s] ' "$n"
        read -r pick || die "no SD card chosen"
        case "$pick" in ''|*[!0-9]*) die "not a number: $pick" ;; esac
        SD="$(printf '%s\n' "$cands" | sed -n "${pick}p")"
        [ -n "$SD" ] || die "no such choice: $pick"
    fi
fi
SD="${SD%/}"
[ -d "$SD" ] || die "not a folder: $SD"
[ -d "$SD/3ds" ] || die "$SD has no /3ds folder; is it a homebrewed 3DS SD card?"
info "SD card: $SD"

free_kb="$(df -Pk "$SD" 2>/dev/null | awk 'NR == 2 { print $4 }')"
if [ -n "$free_kb" ] && [ "$free_kb" -lt "$MIN_FREE_KB" ] 2>/dev/null; then
    die "not enough free space on the SD card (need about 60MB, have $((free_kb / 1024))MB)"
fi

dest="$SD/3ds/balatro_c"
info ""
info "This will copy Balatro's assets from your copy of the game"
[ "$NO_3DSX" = 0 ] && info "and balatro_c.3dsx"
info "to $dest/"
ask_yes "Continue?" || { info "Cancelled."; exit 1; }

# ---------------------------------------------------------------- extracting
tmp="$(mktemp -d 2>/dev/null || mktemp -d -t balatro_c)" || die "could not make a temporary folder"
trap 'rm -rf "$tmp"' EXIT INT TERM

info "Extracting the assets..."
# the same set the port has always used: 1x textures, sounds, shaders (only their names
# are used), fonts except the large GoNoto fallbacks, and the localization files
zip_extract "$ARCHIVE" "$tmp/x" \
    'resources/textures/1x/*' 'resources/sounds/*' 'resources/shaders/*' \
    'resources/fonts/m6x11plus.ttf' 'resources/fonts/NotoSans-Bold.ttf' 'resources/fonts/NotoSansJP-Bold.ttf' \
    'resources/fonts/NotoSansKR-Bold.ttf' 'resources/fonts/NotoSansSC-Bold.ttf' 'resources/fonts/NotoSansTC-Bold.ttf' \
    'localization/*' || die "could not extract the game's files"
for f in resources/textures/1x/gamepad_ui.png resources/sounds/music1.ogg resources/fonts/m6x11plus.ttf localization/en-us.lua; do
    [ -f "$tmp/x/$f" ] || die "the extracted files are incomplete (missing $f)"
done

# lets the game check that the assets are there and match it
{
    printf 'balatro_c assets\n'
    printf 'format=%s\n' "$ASSET_FORMAT"
    printf 'game_version=%s\n' "$version"
} > "$tmp/x/balatro_c_assets.txt"

# ---------------------------------------------------------------- copying to the SD card
info "Copying to the SD card..."
mkdir -p "$dest" || die "could not create $dest"
rm -rf "$dest/assets.new"
cp -R "$tmp/x" "$dest/assets.new" || { rm -rf "$dest/assets.new"; die "copying to the SD card failed"; }
rm -rf "$dest/assets"
mv "$dest/assets.new" "$dest/assets" || die "could not move the assets into place"
if [ "$NO_3DSX" = 0 ]; then
    cp "$DSX" "$dest/balatro_c.3dsx" || die "could not copy balatro_c.3dsx"
fi
sync 2>/dev/null

info ""
if [ "$AZAHAR" = 1 ]; then
    info "Done. balatro_c is installed in Azahar's virtual SD card."
    info "In Azahar, choose File > Load File and open:"
    info "  $dest/balatro_c.3dsx"
    info "(New 3DS mode must be on: Emulation > Configure > System.)"
else
    info "Done. balatro_c is installed in /3ds/balatro_c on your SD card."
    info "Eject the SD card safely, put it back in your New 3DS and start balatro_c from the Homebrew Launcher."
fi
