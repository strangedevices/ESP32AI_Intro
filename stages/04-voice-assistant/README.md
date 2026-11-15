# Stage 04 - Voice assistant

## Goal
Hold a spoken conversation with an ElevenLabs agent from the CoreS3: the device streams
your voice to the agent over a WebSocket and plays the agent's reply through the speaker.

Do the account and agent setup below first; you can do it before the workshop.

## Setup part 1: make an agent
You need an ElevenLabs account (the free tier is enough to start) and one agent in it.

1. Sign in at https://elevenlabs.io and open **Agents** (ElevenAgents) in the sidebar.
2. Choose **Browse templates**, find **Personal Voice Assistant**, and press
   **Use template**. It is a general-purpose assistant that answers questions and takes
   notes.

   ![Personal Voice Assistant template](images/template-personal-voice-assistant.png)

3. The template offers a Zendesk integration. Skip it; we do not use it.
4. Give the agent a name and create it. You can try it in the browser with the
   **Preview** tab to check it talks before going near the device.

Any other template or a blank agent works too. Changing the agent's prompt, voice and
first message later is a good way to make the assistant your own.

## Setup part 2: find your agent id
The agent id starts with `agent_`. The easiest places to find it:

- On the **Agents** list, press the **...** button at the right of your agent's row and
  choose **Copy agent JSON config**. Paste it into a text editor: the id is at the top.
- It is also in the web-page embed code for the agent (the widget snippet).

## Setup part 3: API key
In ElevenLabs, open your profile menu, then **API Keys**, and create a key. Copy it
straight away; it is only shown once.

## Setup part 4: put both in secrets.h
Open `Reference Implementations/secrets.h` (the file you made in stage 02 from
`secrets.h.example`) and fill in:

```c
#define ELEVENLABS_API_KEY  "sk_..."
#define ELEVENLABS_AGENT_ID "agent_..."
```

This file is gitignored. Never commit it, paste it into a chat, or show it on a shared
screen. If you ask Claude Code for help, tell it not to print the file.

## If your free credits run out
Voice conversations use credits quickly. If you run out during the workshop, ask the
presenter: there is a shared workshop agent with its own key. You replace **both** values
in `secrets.h` with the ones you are given, because a key only works with agents in the
same account.

## Build and flash
```bash
cd "Reference Implementations/04-assistant"
```
```bash
pio run -t upload
```
```bash
pio device monitor
```

## Try it
- Tap **Talk**. The device joins Wi-Fi if needed, asks ElevenLabs for a signed URL, and
  opens the WebSocket (about two seconds). The agent greets you.
- When the bar says **LISTENING** (green, with a level meter), speak. The agent works out
  by itself when you have finished. Your words and the agent's reply appear in the log.
- While the bar says **AGENT SPEAKING** the microphone is off, so you cannot interrupt.
- Tap **End** to finish, or just say goodbye: the agent can hang up by itself.
- Serial shortcuts: `t` talk/end, `w` Wi-Fi, `c` clear.

## Mind your credits
A conversation costs credits for as long as it is open: roughly 1,000 credits a minute in
our tests. A free account has 10,000 a month, so you have about ten minutes of talking.
Spend them on short, deliberate conversations.

The firmware protects you: the status bar shows the seconds used, a conversation ends by
itself after 20 seconds of silence, and no conversation runs longer than 90 seconds. The
limits are `IDLE_END_S` and `MAX_SESSION_S` at the top of `src/main.cpp`. Check what you
have left under your profile in the ElevenLabs dashboard.

## How it works
- `src/ws_client.h` is a small WebSocket client written for this workshop. The usual
  Arduino WebSockets library rejects messages over 15 KB, and the agent sends audio in
  messages of hundreds of KB, so this one buffers each message in PSRAM.
- Audio from the agent arrives as base64 PCM inside JSON. It is decoded into a ring buffer
  in PSRAM and fed to the speaker a chunk at a time. Your voice goes the other way: each
  128 ms mic chunk is base64-encoded and sent as `{"user_audio_chunk": "..."}`.
- The mic and speaker share one I2S bus, so the device is half-duplex: it switches to the
  speaker when agent audio arrives and back to the mic 300 ms after playback finishes.
- The server sends `ping` messages; the device must answer each with a `pong` or the
  conversation is dropped.

## Now change it with Claude Code
- Change your agent's prompt, voice or first message in the ElevenLabs dashboard. No
  reflash needed.
- Show the conversation length and audio received in the status bar.
- Add barge-in: tapping the screen while the agent speaks stops playback and listens.
- Save each conversation's transcript to the SD card.
- Make the screen show a "thinking" state between your words and the agent's reply.

## Done when
You have had a spoken conversation with your agent on the device and made one change.

## Stuck?
- "Signed URL: HTTP 401": the API key is wrong or lacks agent permission.
- "Signed URL: HTTP 404": the agent id is wrong, or the agent belongs to another account.
- Crackly or distorted voice: lower `SPEAKER_VOLUME` at the top of `src/main.cpp`.
- Connects but silent: check the log line "Agent audio pcm_16000"; other formats need the
  agent's output format changed in the dashboard.
- See `fallback-prompts/04-voice-assistant.md`.
