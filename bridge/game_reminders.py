#!/usr/bin/env python3
"""Game-day reminders for the family chat.

Reads the fixture list (docs/games.json, published on the GitHub Pages site so it
can be updated without touching the Pi) and posts to the family chat topic:

  * the evening before a game   "Mezz plays her next game tomorrow at 12:30 ..."
  * on the morning of the game  "Mezz plays today at 12:30 ..."
  * at tip-off                  "Tip-off! Good luck Mezz and the Rockets"
  * after the game              "How did Mezz get on? Tap the basketball ..."

Messages go out once each; what has been sent is remembered in a small state file.
Only standard-library Python is used, so it runs on a Pi Zero W.

Settings (environment variables):
  FAMILY_TOPIC   the family chat code (required)
  NTFY_SERVER    default https://ntfy.sh
  GAMES_URL      default https://ap-decon.github.io/smalltv-assistant/games.json
  GAMES_FILE     local copy used when the URL can't be reached
  STATE_DIR      where to remember sent reminders (systemd sets this)
"""
import json
import os
import sys
import time
import urllib.request
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

SERVER = os.environ.get("NTFY_SERVER", "https://ntfy.sh").rstrip("/")
TOPIC = os.environ.get("FAMILY_TOPIC", "").strip()
GAMES_URL = os.environ.get("GAMES_URL", "https://ap-decon.github.io/smalltv-assistant/games.json")
GAMES_FILE = os.environ.get("GAMES_FILE", os.path.join(os.path.dirname(os.path.abspath(__file__)), "games.json"))
STATE_DIR = os.environ.get("STATE_DIRECTORY") or os.environ.get("STATE_DIR") or "."
STATE_FILE = os.path.join(STATE_DIR, "sent.json")

EVE_HOUR = 19          # evening-before reminder at 19:00
MORNING_HOUR = 8       # game-day reminder at 08:00 (or 3 h before an early game)
AFTER_MINUTES = 110    # "how did it go?" this long after tip-off
LATE_LIMIT = timedelta(hours=2)   # don't send a reminder more than 2 h late


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def load_games():
    """Fixture list from the website, falling back to (and refreshing) the local copy."""
    try:
        with urllib.request.urlopen(GAMES_URL + "?t=%d" % time.time(), timeout=20) as r:
            data = json.loads(r.read().decode("utf-8"))
        try:
            with open(GAMES_FILE + ".tmp", "w") as f:
                json.dump(data, f)
            os.replace(GAMES_FILE + ".tmp", GAMES_FILE)
        except OSError:
            pass
        return data
    except Exception as e:
        log("could not fetch fixtures, using local copy:", e)
        with open(GAMES_FILE) as f:
            return json.load(f)


def load_state():
    try:
        with open(STATE_FILE) as f:
            return set(json.load(f))
    except (OSError, ValueError):
        return set()


def save_state(sent):
    tmp = STATE_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(sorted(sent)[-200:], f)
    os.replace(tmp, STATE_FILE)


def post(title, message, sticker=None):
    body = {"topic": TOPIC, "title": title, "message": message}
    if sticker:
        body["tags"] = ["sticker-" + sticker]
    req = urllib.request.Request(SERVER + "/", data=json.dumps(body).encode("utf-8"),
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=20) as r:
        r.read()
    log("sent:", message)


def clock(dt):
    return dt.strftime("%H:%M")


def reminders(game, player, team, tz):
    """(key, when, message, sticker) for one game."""
    start = datetime.fromisoformat(game["start"]).replace(tzinfo=tz)
    opp = game["opponent"]
    where = game.get("venue", "")
    side = "at home" if game.get("home") else "away"
    place = f"{side}, {where}" if where else side
    day = start.strftime("%a %d %b")

    eve = (start - timedelta(days=1)).replace(hour=EVE_HOUR, minute=0)
    morning = min(start.replace(hour=MORNING_HOUR, minute=0), start - timedelta(hours=3))
    key = start.strftime("%Y%m%d%H%M")
    return [
        (key + "-eve", eve,
         f"{player} plays her next game tomorrow at {clock(start)} vs {opp} ({place})", None),
        (key + "-day", morning,
         f"{player} plays today at {clock(start)} vs {opp} ({place}). Go {team}!", "fire"),
        (key + "-tip", start,
         f"Tip-off! {team} vs {opp}. Good luck {player}!", "star"),
        (key + "-after", start + timedelta(minutes=AFTER_MINUTES),
         f"How did {player} get on vs {opp}? Tap the basketball in the family chat to send the score.", None),
    ], day


def next_game_line(games, player, tz, now):
    for g in games:
        start = datetime.fromisoformat(g["start"]).replace(tzinfo=tz)
        if start > now:
            return f"{player}'s next game: {start.strftime('%a %d %b %H:%M')} vs {g['opponent']}"
    return None


def main():
    if not TOPIC:
        sys.exit("FAMILY_TOPIC is not set")
    sent = load_state()
    data = None
    last_fetch = 0
    log("game reminders running for topic", TOPIC[:6] + "...")
    while True:
        if time.time() - last_fetch > 3600 or data is None:
            try:
                data = load_games()
                last_fetch = time.time()
                tz = ZoneInfo(data.get("timezone", "Europe/London"))
                line = next_game_line(data["games"], data.get("player", "Mezz"), tz, datetime.now(tz))
                if line:
                    log(line)
            except Exception as e:
                log("no fixture list yet:", e)
                time.sleep(300)
                continue

        tz = ZoneInfo(data.get("timezone", "Europe/London"))
        now = datetime.now(tz)
        player = data.get("player", "Mezz")
        team = data.get("team", "Rockets")
        for g in data.get("games", []):
            try:
                items, _ = reminders(g, player, team, tz)
            except (KeyError, ValueError) as e:
                log("skipping bad fixture", g, e)
                continue
            for key, when, msg, sticker in items:
                if key in sent or now < when:
                    continue
                sent.add(key)          # mark first so a failure never repeats endlessly
                save_state(sent)
                if now - when > LATE_LIMIT:
                    continue           # Pi was off at the time; skip stale reminders
                try:
                    post("Game day", msg, sticker)
                except Exception as e:
                    log("send failed:", e)
                    sent.discard(key)  # try again next minute
                    save_state(sent)
        time.sleep(60)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--list":
        d = json.load(open(sys.argv[2] if len(sys.argv) > 2 else GAMES_FILE))
        z = ZoneInfo(d.get("timezone", "Europe/London"))
        for g in d["games"]:
            for key, when, msg, _ in reminders(g, d["player"], d["team"], z)[0]:
                print(when.strftime("%a %d %b %H:%M"), "|", msg)
        sys.exit(0)
    main()
