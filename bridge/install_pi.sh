#!/usr/bin/env bash
# Installs the Claude bridge as a background service on a Raspberry Pi
# (tested target: Pi Zero W with Raspberry Pi OS; runs fine alongside Pi-hole).
#
#   bash install_pi.sh
#
# Re-run it any time to change the topic, key or options.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEST=/opt/smalltv-bridge
ENVFILE=/etc/smalltv-bridge.env

command -v python3 >/dev/null || { echo "python3 is missing: sudo apt install -y python3"; exit 1; }

read -rp "ntfy topic your cube uses (e.g. alan-cube-7f3k9q2x): " TOPIC
[ -n "$TOPIC" ] || { echo "A topic is required."; exit 1; }
read -rsp "Anthropic API key (sk-ant-...): " KEY; echo
[ -n "$KEY" ] || { echo "An API key is required."; exit 1; }
read -rp "Let Claude filter out spam/promo notifications? [y/N]: " FILTER
EXTRA=""
[[ "${FILTER,,}" == y* ]] && EXTRA="--filter"

sudo mkdir -p "$DEST"
sudo cp "$HERE/claude_bridge.py" "$DEST/"

sudo tee "$ENVFILE" >/dev/null <<EOF
ANTHROPIC_API_KEY=$KEY
BRIDGE_TOPIC=$TOPIC
BRIDGE_ARGS=$EXTRA
EOF
sudo chmod 600 "$ENVFILE"

sudo tee /etc/systemd/system/smalltv-bridge.service >/dev/null <<'EOF'
[Unit]
Description=SmallTV Assistant - Claude bridge
After=network-online.target
Wants=network-online.target

[Service]
EnvironmentFile=/etc/smalltv-bridge.env
ExecStart=/usr/bin/python3 -u /opt/smalltv-bridge/claude_bridge.py --topic ${BRIDGE_TOPIC} $BRIDGE_ARGS
Restart=always
RestartSec=10
DynamicUser=yes
MemoryMax=60M

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now smalltv-bridge
sleep 2
sudo systemctl --no-pager status smalltv-bridge | head -n 12
echo
echo "Done. Live log:  journalctl -u smalltv-bridge -f"
