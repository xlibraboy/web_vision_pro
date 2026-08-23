#!/usr/bin/env python3
import argparse
import json
import os
import pwd
import subprocess
import sys
import threading
from typing import List, Optional


def auth_file_has_credentials(path: str) -> bool:
    if not os.path.exists(path):
        return False
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return isinstance(data, dict) and bool(data)
    except Exception:
        return False


def build_command(args: argparse.Namespace) -> List[str]:
    cmd = ["pi", "--mode", "rpc", "--no-session"]
    if args.provider:
        cmd += ["--provider", args.provider]
    if args.model:
        cmd += ["--model", args.model]
    if args.thinking:
        cmd += ["--thinking", args.thinking]
    if args.tools:
        cmd += ["--tools", args.tools]
    if args.no_context_files:
        cmd += ["--no-context-files"]
    if args.offline:
        cmd += ["--offline"]
    return cmd


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Send a one-shot prompt to Pi via RPC mode.")
    p.add_argument("prompt", nargs="+", help="Prompt text to send to Pi")
    p.add_argument("--provider", help="Pi provider, e.g. anthropic/openai/google/openrouter")
    p.add_argument("--model", help="Pi model id/pattern")
    p.add_argument("--thinking", choices=["off", "minimal", "low", "medium", "high", "xhigh"])
    p.add_argument("--tools", default="read,grep,find,ls", help="Comma-separated Pi tools allowlist")
    p.add_argument("--no-context-files", action="store_true", help="Disable AGENTS.md/CLAUDE.md loading in Pi")
    p.add_argument("--offline", action="store_true", help="Disable Pi startup network operations")
    p.add_argument("--home", help="HOME to use for Pi auth/config lookup. Defaults to detected real user home if current HOME has no Pi auth.")
    p.add_argument("--verbose-events", action="store_true", help="Emit raw RPC events to stderr")
    return p.parse_args()


def detect_pi_home(explicit_home: Optional[str] = None) -> str:
    if explicit_home:
        return os.path.expanduser(explicit_home)

    current_home = os.path.expanduser("~")
    current_auth = os.path.join(current_home, ".pi", "agent", "auth.json")
    if auth_file_has_credentials(current_auth):
        return current_home

    real_home = pwd.getpwuid(os.getuid()).pw_dir
    real_auth = os.path.join(real_home, ".pi", "agent", "auth.json")
    if auth_file_has_credentials(real_auth):
        return real_home

    fallback_home = "/home/autoinst578"
    fallback_auth = os.path.join(fallback_home, ".pi", "agent", "auth.json")
    if auth_file_has_credentials(fallback_auth):
        return fallback_home

    return current_home


def main() -> int:
    args = parse_args()
    prompt = " ".join(args.prompt)
    cmd = build_command(args)
    pi_home = detect_pi_home(args.home)
    env = os.environ.copy()
    env["HOME"] = pi_home

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
        cwd=os.getcwd(),
        env=env,
    )

    stderr_lines: List[str] = []

    def drain_stderr() -> None:
        assert proc.stderr is not None
        for line in proc.stderr:
            stderr_lines.append(line)

    stderr_thread = threading.Thread(target=drain_stderr, daemon=True)
    stderr_thread.start()

    assert proc.stdin is not None
    assert proc.stdout is not None

    proc.stdin.write(json.dumps({"type": "prompt", "message": prompt}) + "\n")
    proc.stdin.flush()

    saw_text = False
    response_error = None
    assistant_error = None

    try:
        for raw_line in proc.stdout:
            line = raw_line[:-1] if raw_line.endswith("\n") else raw_line
            if line.endswith("\r"):
                line = line[:-1]
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                if args.verbose_events:
                    print(f"[non-json] {line}", file=sys.stderr)
                continue

            if args.verbose_events:
                print(json.dumps(event, ensure_ascii=False), file=sys.stderr)

            event_type = event.get("type")
            if event_type == "response" and not event.get("success", True):
                response_error = event
                break
            elif event_type == "error":
                response_error = event
                break
            elif event_type == "message_update":
                delta = event.get("assistantMessageEvent", {})
                if delta.get("type") == "text_delta":
                    sys.stdout.write(delta.get("delta", ""))
                    sys.stdout.flush()
                    saw_text = True
            elif event_type == "message_end":
                message = event.get("message", {})
                if message.get("role") == "assistant" and message.get("stopReason") == "error":
                    assistant_error = message.get("errorMessage") or "Assistant turn ended with an unspecified error."
            elif event_type == "agent_end":
                break
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        proc.wait(timeout=10)
        stderr_thread.join(timeout=2)

    if saw_text:
        sys.stdout.write("\n")
        sys.stdout.flush()

    stderr_text = "".join(stderr_lines).strip()

    if response_error is not None:
        print(json.dumps(response_error, ensure_ascii=False), file=sys.stderr)
        if stderr_text:
            print(stderr_text, file=sys.stderr)
        return 1

    if assistant_error is not None:
        print(assistant_error, file=sys.stderr)
        if stderr_text and stderr_text not in assistant_error:
            print(stderr_text, file=sys.stderr)
        return 1

    if proc.returncode != 0:
        if stderr_text:
            print(stderr_text, file=sys.stderr)
        else:
            print(f"Pi exited with code {proc.returncode}", file=sys.stderr)
        return proc.returncode

    if stderr_text and args.verbose_events:
        print(stderr_text, file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
