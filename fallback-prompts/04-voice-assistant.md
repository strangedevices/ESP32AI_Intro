# Fallback prompt - stage 04 (voice assistant)

Run `claude` inside `Reference Implementations/04-assistant` and paste:

```
This PlatformIO project makes an M5Stack CoreS3 talk to an ElevenLabs agent over a WebSocket
(src/main.cpp, src/ws_client.h, src/ui.h). secrets.h in the repo's top folder holds my Wi-Fi details, API key and
agent id: never read, print or edit that file. Please:
1. Run `pio run` and fix any build error.
2. Flash with `pio run -t upload`, open the serial port at 115200 without toggling DTR/RTS,
   and send `t` to start a conversation.
3. Watch for 30 seconds and show me the log: I expect "Connected in ... ms", an
   "Agent audio pcm_16000" line and an "Agent:" greeting. Then send `t` again to end it.
If it fails, tell me which step failed (Wi-Fi, signed URL with its HTTP code, WebSocket,
or no audio) before changing any code.
```

To build it yourself from stage 03 instead, a starting prompt:

```
Starting from the stage 03 recorder harness (ui.h, M5.Mic and M5.Speaker at 16 kHz), add a
"Talk" button that holds a voice conversation with an ElevenLabs agent. Get a signed URL from
GET https://api.elevenlabs.io/v1/convai/conversation/get-signed-url?agent_id=... with the
xi-api-key header, open that wss URL, send {"type":"conversation_initiation_client_data"},
send mic audio as {"user_audio_chunk":"<base64 pcm>"}, play the base64 PCM in "audio" events,
answer every "ping" with {"type":"pong","event_id":N}, and log user_transcript and
agent_response text. The mic and speaker share one I2S bus, so make it half-duplex. Credentials
are ELEVENLABS_API_KEY and ELEVENLABS_AGENT_ID in secrets.h in the repo's top folder; never print them.
```

Hints the presenter may give if your own attempt struggles:
- Incoming audio messages are far bigger than most WebSocket libraries allow (15 KB).
- `M5.Speaker.playRaw()` does not copy its buffer.
- No pong, no conversation.
