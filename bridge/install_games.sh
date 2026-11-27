#!/usr/bin/env bash
# Installs the game-day reminders (family chat) as a background service on the Pi.
#
#   bash install_games.sh
#
# Re-run it any time to change the family code.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEST=/opt/smalltv-bridge
ENVFILE=/etc/smalltv-games.env

command -v python3 >/dev/null || { echo "python3 is missing: sudo apt install -y python3"; exit 1; }

read -rp "Family chat code (the same one the cubes use, starts fam-): " FAM
FAM="${FAM##*#}"   # accept a whole invite link too
[ -n "$FAM" ] || { echo "A family code is required."; exit 1; }

sudo mkdir -p "$DEST"
sudo cp "$HERE/game_reminders.py" "$DEST/"
sudo cp "$HERE/../docs/games.json" "$DEST/games.json"
sudo chmod 644 "$DEST/game_reminders.py" "$DEST/games.json"

sudo tee "$ENVFILE" >/dev/null <<EOF
FAMILY_TOPIC=$FAM
GAMES_FILE=/var/lib/smalltv-games/games.json
EOF
sudo chmod 600 "$ENVFILE"

sudo tee /etc/systemd/system/smalltv-games.service >/dev/null <<'EOF'
[Unit]
Description=SmallTV Assistant - game-day reminders
After=network-online.target time-sync.target
Wants=network-online.target

[Service]
EnvironmentFile=/etc/smalltv-games.env
ExecStartPre=/bin/sh -c 'test -f /var/lib/smalltv-games/games.json || cp /opt/smalltv-bridge/games.json /var/lib/smalltv-games/games.json'
ExecStart=/usr/bin/python3 -u /opt/smalltv-bridge/game_reminders.py
StateDirectory=smalltv-games
Restart=always
RestartSec=30
DynamicUser=yes
MemoryMax=40M

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now smalltv-games
sudo systemctl restart smalltv-games
sleep 4
systemctl --no-pager status smalltv-games | head -n 12 || true
echo
echo "Coming up:"
python3 "$DEST/game_reminders.py" --list "$DEST/games.json" 2>/dev/null | head -n 4 || true
echo
echo "Done. Live log:  journalctl -u smalltv-games -f"
