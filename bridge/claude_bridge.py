#!/usr/bin/env python3
"""
Claude bridge for the SmallTV Assistant.

Listens on two ntfy topics and talks to your cube through a third:

    <topic>-in   raw phone notifications   -> Claude rewrites them -> <topic>
    <topic>-ask  questions you type        -> Claude answers them  -> <topic>

The cube itself subscribes to <topic>.

Only uses the Python standard library. Needs an Anthropic API key in the
ANTHROPIC_API_KEY environment variable.

Examples
    python claude_bridge.py --topic alan-cube-7f3k9q2x
    python claude_bridge.py --topic alan-cube-7f3k9q2x --filter
    python claude_bridge.py --topic alan-cube-7f3k9q2x ask "How far is the Moon?"
    python claude_bridge.py --topic alan-cube-7f3k9q2x say "Tea's ready" --mood happy
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

MOODS = ["neutral", "happy", "sad", "surprised", "thinking", "sleepy", "love", "angry"]
API_URL = "https://api.anthropic.com/v1/messages"

PERSONA = (
    "You are Bit, a tiny, friendly assistant who lives on a 240x240 pixel screen "
    "on your owner's desk. You speak in short, warm, plain sentences."
)

NOTIFY_PROMPT = PERSONA + """
Rewrite the phone notification below as a message for the screen.
Rules:
- "title": at most 20 characters, usually the app or the person it is from.
- "message": at most 110 characters. Keep the key facts (who, what, when, amounts).
  A touch of personality is fine, but never invent facts.
- Plain ASCII only. No emoji.
- "mood": one of neutral, happy, sad, surprised, thinking, sleepy, love, angry -
  pick the one that fits the content (a delivery arriving = happy, a failed payment = surprised).
- "skip": true only if FILTERING is on and the notification is clearly spam, an advert,
  a promo, or a meaningless system message. Otherwise false.
FILTERING: {filtering}
Reply with JSON only, like {{"title": "...", "message": "...", "mood": "...", "skip": false}}"""

ASK_PROMPT = PERSONA + """
Answer your owner's question for the screen.
Rules:
- "message": at most 150 characters, plain ASCII, no emoji, no markdown.
- "title": at most 20 characters, a tiny heading for the answer.
- "mood": one of neutral, happy, sad, surprised, thinking, sleepy, love, angry.
Reply with JSON only, like {"title": "...", "message": "...", "mood": "..."}"""


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def to_ascii(s, limit):
    s = (s or "").replace("’", "'").replace("‘", "'").replace("“", '"').replace("”", '"')
    s = s.replace("—", "-").replace("–", "-").replace("…", "...")
    s = s.encode("ascii", "ignore").decode()
    s = re.sub(r"[ \t]+", " ", s).strip()
    return s[:limit]


def parse_json_reply(text):
    """Pull the first {...} object out of a model reply."""
    start, end = text.find("{"), text.rfind("}")
    if start < 0 or end <= start:
        raise ValueError("no JSON in reply: %r" % text[:200])
    return json.loads(text[start:end + 1])


class Bridge:
    def __init__(self, args):
        self.a = args
        self.server = args.server.rstrip("/")
        self.key = os.environ.get("ANTHROPIC_API_KEY", "")
        self.recent = {}  # (title, msg) -> time, for de-duplication
        self.last_id = None

    # ---- output -------------------------------------------------------
    def _auth(self, headers):
        if self.a.token:
            headers["Authorization"] = "Bearer " + self.a.token
        return headers

    def send(self, title, message, mood="neutral"):
        title, message = to_ascii(title, 40), to_ascii(message, 300)
        mood = mood if mood in MOODS else "neutral"
        if self.a.device:
            url = self.a.device.rstrip("/") + "/notify"
            data = json.dumps({"title": title, "message": message, "mood": mood}).encode()
            req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
        else:
            url = "%s/%s" % (self.server, self.a.topic)
            headers = self._auth({"Title": title, "Tags": mood})
            req = urllib.request.Request(url, data=message.encode(), headers=headers)
        try:
            urllib.request.urlopen(req, timeout=15).read()
            log("-> cube [%s] %s: %s" % (mood, title, message))
        except Exception as e:  # noqa: BLE001
            log("!! could not send to cube:", e)

    # ---- Claude -------------------------------------------------------
    def claude(self, system, user, max_tokens=300):
        if not self.key:
            raise RuntimeError("ANTHROPIC_API_KEY is not set")
        body = {
            "model": self.a.model,
            "max_tokens": max_tokens,
            "system": system,
            "messages": [{"role": "user", "content": user}],
        }
        req = urllib.request.Request(
            API_URL,
            data=json.dumps(body).encode(),
            headers={
                "x-api-key": self.key,
                "anthropic-version": "2023-06-01",
                "content-type": "application/json",
            },
        )
        with urllib.request.urlopen(req, timeout=60) as r:
            resp = json.load(r)
        text = "".join(b.get("text", "") for b in resp.get("content", []) if b.get("type") == "text")
        return parse_json_reply(text)

    # ---- handlers -----------------------------------------------------
    def on_notification(self, title, message):
        key = (title, message)
        now = time.time()
        self.recent = {k: t for k, t in self.recent.items() if now - t < 120}
        if key in self.recent:
            log("   duplicate, skipped")
            return
        self.recent[key] = now

        if message.strip().startswith("!"):  # control command: pass straight through
            self.send(title, message)
            return
        try:
            user = "Notification title: %s\nNotification text: %s" % (title or "(none)", message or "(none)")
            out = self.claude(NOTIFY_PROMPT.format(filtering="on" if self.a.filter else "off"), user)
            if out.get("skip") and self.a.filter:
                log("   filtered out as noise")
                return
            self.send(out.get("title", title), out.get("message", message), out.get("mood", "neutral"))
        except Exception as e:  # noqa: BLE001
            log("!! Claude failed (%s), forwarding the original" % e)
            self.send(title, message)

    def on_question(self, question):
        self.send("Hmm...", question, "thinking")
        try:
            out = self.claude(ASK_PROMPT, question)
            self.send(out.get("title", "Answer"), out.get("message", ""), out.get("mood", "happy"))
        except Exception as e:  # noqa: BLE001
            log("!! Claude failed:", e)
            self.send("Oops", "I couldn't reach Claude just now.", "sad")

    # ---- ntfy subscription --------------------------------------------
    def listen(self):
        topics = "%s-in,%s-ask" % (self.a.topic, self.a.topic)
        backoff = 2
        log("listening on %s/%s (sending to %s)" % (self.server, topics, self.a.device or self.a.topic))
        while True:
            url = "%s/%s/json" % (self.server, topics)
            if self.last_id:
                url += "?since=" + urllib.parse.quote(self.last_id)
            try:
                req = urllib.request.Request(url, headers=self._auth({}))
                with urllib.request.urlopen(req, timeout=90) as r:
                    backoff = 2
                    for raw in r:
                        line = raw.decode("utf-8", "ignore").strip()
                        if line:
                            self.handle_line(line)
            except KeyboardInterrupt:
                raise
            except Exception as e:  # noqa: BLE001
                log("!! connection problem (%s), retrying in %ds" % (e, backoff))
                time.sleep(backoff)
                backoff = min(backoff * 2, 60)

    def handle_line(self, line):
        try:
            ev = json.loads(line)
        except ValueError:
            return
        if ev.get("event") != "message":
            return
        self.last_id = ev.get("id") or self.last_id
        topic = ev.get("topic", "")
        title, message = ev.get("title", ""), ev.get("message", "")
        log("<- %s | %s | %s" % (topic, title, message))
        if topic.endswith("-ask"):
            self.on_question(" ".join(x for x in (title, message) if x))
        else:
            self.on_notification(title, message)


def main(argv=None):
    p = argparse.ArgumentParser(description="Claude bridge for the SmallTV Assistant")
    p.add_argument("--topic", required=True, help="the topic your cube subscribes to")
    p.add_argument("--server", default="https://ntfy.sh", help="ntfy server (default https://ntfy.sh)")
    p.add_argument("--token", default=os.environ.get("NTFY_TOKEN", ""), help="ntfy access token, if your server needs one")
    p.add_argument("--device", default="", help="send straight to the cube instead, e.g. http://smalltv-assistant.local")
    p.add_argument("--model", default=os.environ.get("CLAUDE_MODEL", "claude-haiku-5-5"))
    p.add_argument("--filter", action="store_true", help="let Claude drop spam and promo notifications")
    sub = p.add_subparsers(dest="cmd")
    a = sub.add_parser("ask", help="ask one question and show the answer on the cube")
    a.add_argument("question")
    s = sub.add_parser("say", help="show a message on the cube (no Claude)")
    s.add_argument("message")
    s.add_argument("--title", default="")
    s.add_argument("--mood", default="neutral", choices=MOODS)
    args = p.parse_args(argv)

    b = Bridge(args)
    if args.cmd == "ask":
        b.on_question(args.question)
    elif args.cmd == "say":
        b.send(args.title, args.message, args.mood)
    else:
        if not b.key:
            sys.exit("Set ANTHROPIC_API_KEY first (https://console.anthropic.com/).")
        try:
            b.listen()
        except KeyboardInterrupt:
            log("bye")


if __name__ == "__main__":
    main()
