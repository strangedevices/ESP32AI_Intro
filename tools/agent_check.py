"""Check an ElevenLabs agent from a laptop, before going near the CoreS3.

Reads ELEVENLABS_AGENT_ID (and ELEVENLABS_API_KEY, if set) from secrets.h in the
repo's top folder. With a key it gets a signed URL, as stage 04 does; without one
it connects to the agent directly, which works for agents with authentication
off. It then streams a 16 kHz mono WAV
in 100 ms chunks (as if the button were held), sends 1.2 s of silence (as on
release), then reports what came back: audio formats, transcripts, tool
calls, message sizes and timing. Saves the agent's reply as reply.wav.

A run opens a real conversation for about 20 seconds, which uses credits.

    python3 -m pip install websockets
    python3 tools/agent_check.py path/to/question.wav

Make a test clip on a Mac:
    say -o ask.aiff "Please show hello on my screen"
    afconvert -f WAVE -d LEI16@16000 -c 1 ask.aiff ask.wav
"""

import asyncio
import base64
import json
import re
import sys
import time
import urllib.request
import wave
from pathlib import Path

import websockets

ROOT = Path(__file__).resolve().parent.parent
CHUNK_MS = 100
SILENCE_CHUNKS = 12
REPLY_GAP_S = 0.7
TIMEOUT_S = 30


def secret(name):
    text = (ROOT / "secrets.h").read_text()
    m = re.search(r'#define\s+%s\s+"([^"]*)"' % name, text)
    value = m.group(1) if m else ""
    return "" if not value or "your_" in value else value   # ignore the example placeholders


def conversation_url():
    agent = secret("ELEVENLABS_AGENT_ID")
    if not agent:
        sys.exit("ELEVENLABS_AGENT_ID is not set in secrets.h")
    key = secret("ELEVENLABS_API_KEY")
    if not key:
        return f"wss://api.elevenlabs.io/v1/convai/conversation?agent_id={agent}"
    req = urllib.request.Request(
        f"https://api.elevenlabs.io/v1/convai/conversation/get-signed-url?agent_id={agent}",
        headers={"xi-api-key": key})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.load(r)["signed_url"]


def load_wav(path):
    with wave.open(str(path)) as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            sys.exit("need a mono 16-bit WAV")
        return w.getframerate(), w.readframes(w.getnframes())


async def main(wav_path):
    rate_in_file, pcm = load_wav(wav_path)
    url = conversation_url()
    t0 = time.monotonic()
    stamp = lambda: f"{time.monotonic() - t0:6.2f}s"
    sizes, reply, out_rate = [], bytearray(), None
    last_audio, first_audio_after_release, released_at = None, None, None
    seen = {}

    async with websockets.connect(url, max_size=None) as ws:
        print(f"{stamp()} connected")
        await ws.send(json.dumps({"type": "conversation_initiation_client_data"}))

        async def receiver():
            nonlocal out_rate, last_audio, first_audio_after_release
            async for raw in ws:
                sizes.append(len(raw))
                msg = json.loads(raw)
                kind = msg.get("type", "?")
                seen[kind] = seen.get(kind, 0) + 1
                if kind == "conversation_initiation_metadata":
                    meta = msg["conversation_initiation_metadata_event"]
                    print(f"{stamp()} formats: in {meta['user_input_audio_format']}, "
                          f"out {meta['agent_output_audio_format']}")
                    out_rate = meta["agent_output_audio_format"]
                    in_fmt = meta["user_input_audio_format"]
                    if in_fmt != f"pcm_{rate_in_file}":
                        print(f"         WARNING: agent expects {in_fmt}, clip is pcm_{rate_in_file}")
                elif kind == "audio":
                    reply.extend(base64.b64decode(msg["audio_event"]["audio_base_64"]))
                    last_audio = time.monotonic()
                    if released_at and first_audio_after_release is None:
                        first_audio_after_release = last_audio - released_at
                elif kind == "ping":
                    await ws.send(json.dumps({"type": "pong", "event_id": msg["ping_event"]["event_id"]}))
                elif kind == "user_transcript":
                    print(f"{stamp()} you said: {msg['user_transcription_event']['user_transcript']}")
                elif kind == "agent_response":
                    print(f"{stamp()} agent: {msg['agent_response_event']['agent_response']}")
                elif kind == "client_tool_call":
                    call = msg["client_tool_call"]
                    print(f"{stamp()} tool call: {call['tool_name']} {json.dumps(call.get('parameters'))}")
                    ok = call["tool_name"] == "show_message"
                    if call.get("expects_response", True):
                        await ws.send(json.dumps({
                            "type": "client_tool_result", "tool_call_id": call["tool_call_id"],
                            "result": "Shown on the device screen." if ok else "Unknown tool",
                            "is_error": not ok}))
                elif kind == "client_error":
                    print(f"{stamp()} SERVER ERROR: {msg.get('error_event')}")

        recv_task = asyncio.create_task(receiver())

        # Wait for the metadata and any first message, then wait as long as
        # the device would spend playing it before the user can talk.
        await asyncio.sleep(2)
        while last_audio and time.monotonic() - last_audio < REPLY_GAP_S:
            await asyncio.sleep(0.1)
        first_message = bytes(reply)
        reply.clear()
        if first_message:
            secs = len(first_message) / 2 / 16000
            print(f"{stamp()} agent first message: {secs:.1f}s, waiting while it 'plays'")
            await asyncio.sleep(secs)

        step = rate_in_file * CHUNK_MS // 1000 * 2
        print(f"{stamp()} talking: {len(pcm) / 2 / rate_in_file:.1f}s of audio")
        for off in range(0, len(pcm), step):
            await ws.send(json.dumps({"user_audio_chunk": base64.b64encode(pcm[off:off + step]).decode()}))
            await asyncio.sleep(CHUNK_MS / 1000)
        released_at = time.monotonic()
        print(f"{stamp()} released, sending silence")
        silence = base64.b64encode(bytes(step)).decode()
        for _ in range(SILENCE_CHUNKS):
            await ws.send(json.dumps({"user_audio_chunk": silence}))
            await asyncio.sleep(CHUNK_MS / 1000)

        deadline = time.monotonic() + TIMEOUT_S
        while time.monotonic() < deadline:
            await asyncio.sleep(0.1)
            if reply and last_audio and time.monotonic() - last_audio > REPLY_GAP_S + 1.5:
                break
        recv_task.cancel()

    print()
    print("message types:", ", ".join(f"{k} x{v}" for k, v in sorted(seen.items())))
    if sizes:
        big = sum(1 for s in sizes if s > 15 * 1024)
        print(f"largest message: {max(sizes)} bytes; over 15 KB: {big} of {len(sizes)}")
    if first_audio_after_release is not None:
        print(f"first reply audio {first_audio_after_release:.2f}s after release, "
              f"last {last_audio - released_at:.2f}s after release "
              f"(the device starts playing {REPLY_GAP_S}s after the last)")
    else:
        print("NO reply audio after release: the turn may not have ended")
    rate = int(out_rate.split("_")[1]) if out_rate and out_rate.startswith("pcm_") else 16000
    for name, data in (("first_message.wav", first_message), ("reply.wav", bytes(reply))):
        if data:
            with wave.open(name, "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(data)
            print(f"saved {name}: {len(data) / 2 / rate:.1f}s")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    asyncio.run(main(sys.argv[1]))
