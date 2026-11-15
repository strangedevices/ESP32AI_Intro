# Fallback prompt - stage 05 (always listening and agent tools)

Run `claude` inside `Reference Implementations/05-always-listening` and paste:

```
This PlatformIO project is an ElevenLabs voice assistant on an M5Stack CoreS3 with an Auto
mode (local loudness wake) and client tools the agent can call (src/main.cpp runTool()).
secrets.h in the repo's top folder holds my credentials: never read, print or edit it. Conversations cost credits,
so do NOT start one. Please:
1. Run `pio run` and fix any build error.
2. Flash with `pio run -t upload`, open the serial port at 115200 without toggling DTR/RTS.
3. Send `d` and show me the device status line. Then send `r`, wait 3 seconds, send `s`, and
   show me the two "Tool" lines. That tests the tools without the agent.
```

To add a tool of your own:

```
Add a client tool called set_screen_color to runTool() in src/main.cpp. It takes a string
parameter "color" (red, green, blue, yellow, purple, white or black), fills the log area of
the screen with that colour for two seconds, and returns a short sentence the agent can read
out. Unknown colours return an error. Also add a serial key to simulate it. Then tell me
exactly what name, description and parameter to enter for the matching Client tool on my
ElevenLabs agent.
```

Hints the presenter may give:
- An open conversation costs credits even in silence. Wait locally, connect late.
- The tool name on the agent and in the code must match exactly.
- A tool's result is text the agent reads, so return a sentence, not a status code.
