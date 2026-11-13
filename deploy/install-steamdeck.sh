#!/usr/bin/env bash
set -euo pipefail
# Install ONLY the new DLL and known data/config files. Existing icons, art,
# SokuModLoader/SWRSToys settings and unrelated files remain untouched.
src="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
game="${1:-/run/media/deck/share/fxtz/th123}"
dest="$game/modules/WhatHurtsMost"
backup="${HOME}/Backups/SokuMaxDamage/test-$(date +%Y%m%d-%H%M%S)"

if [[ ! -d "$game" ]]; then
  printf 'Game folder not found: %s\nUsage: bash install-steamdeck.sh /path/to/th123\n' "$game" >&2
  exit 1
fi
required=(WhatHurtsMost.dll FrameData.txt MoveProfiles.txt WikiMaxProfiles.txt WhatHurtsMost.ini)
for name in "${required[@]}"; do
  [[ -s "$src/$name" ]] || { printf 'Missing test package file: %s\n' "$src/$name" >&2; exit 1; }
done
if pgrep -afi '[/\\](th123|SokuLauncher)\.exe' >/dev/null 2>&1; then
  echo "The game/launcher appears to be running. Exit it completely and rerun." >&2
  exit 1
fi
if [[ -d "$dest" ]]; then
  mkdir -p "$backup"
  cp -a -- "$dest/." "$backup/"
  printf 'Backup: %s\n' "$backup"
fi
mkdir -p "$dest"
for name in "${required[@]}"; do
  install -m 0644 -- "$src/$name" "$dest/$name"
done
printf '\nInstalled test files to:\n  %s\n' "$dest"
echo "Existing icons/ and art/ folders were NOT removed or replaced."
echo "SokuModLoader / SWRSToys configuration was NOT modified."
echo "Launch the game through your existing configured launcher and test locally first."
