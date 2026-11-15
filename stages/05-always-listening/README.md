# Stage 05 - Always listening, and tools the agent can use

This is the stretch stage. It adds two ideas to the stage 04 assistant.

## Goal
- **Auto mode**: the device listens by itself and starts a conversation when it hears
  you, without holding a connection open while it waits.
- **Agent tools**: the agent can make the device do things. Ask it to "start recording"
  and it records the conversation to the SD card; ask "what's your battery level" and it
  reads the device's own status back to you.

## Build and flash
```bash
cd "Reference Implementations/05-always-listening"
```
```bash
pio run -t upload
```
```bash
pio device monitor
```

It uses the same `Reference Implementations/secrets.h` as before, and needs the SD card
for the recording tools.

## Part 1: Auto mode
- Tap **Auto**. The bar shows **AUTO waiting** with a level meter and a red line.
- Speak near the device. When the level stays above the red line for about half a second,
  it opens a conversation and the agent greets you.
- When the conversation ends it waits four seconds, then listens again.
- Tap **Auto** again to turn it off.

Why it is built this way: an open conversation costs about 1,000 credits a minute whether
or not anyone is talking. So the waiting is done locally, with a simple loudness check on
the microphone, and the network is only used once there is something to say. The settings
are `WAKE_PEAK` and `WAKE_CHUNKS` at the top of `src/main.cpp`.

A loudness check is crude: a cough or a door will wake it. Each false wake costs a few
seconds of credits before the silence guard ends the call (12 seconds of listening with
nobody speaking).

## Part 2: Give your agent tools
The device already knows three tools. You tell the agent they exist.

In the ElevenLabs dashboard, open your agent, find its **Tools** section, and add a
**Client** tool for each of these. The name must match exactly. None need parameters.
Turn on the option to wait for the tool's response, so the agent hears the result.

| Name | Description to give the agent |
|---|---|
| `start_recording` | Start recording this conversation to the device's SD card. Use when the user asks to record. |
| `stop_recording` | Stop the recording and save it. Use when the user asks to stop recording. |
| `get_device_status` | Get the device's battery level, Wi-Fi signal, uptime and SD card state. |

It also helps to add a line to the agent's system prompt, such as: "You are running on a
small handheld device. You can record the conversation and check the device's status
using your tools."

Then tap **Talk** and try: "Please start recording", say a few things, "stop recording".
The bar turns red with **REC** while it records. The file lands in `recordings` on the SD
card as `conv_000.wav`, with both sides of the conversation in order.

To test the device side without the agent (and without credits), use the serial monitor:
`r` start recording, `s` stop, `d` device status.

## Now change it with Claude Code
The interesting part is inventing tools. Look at `runTool()` in `src/main.cpp`: a tool is
a name, a few lines of code, and a sentence returned to the agent. Ideas:
- `set_screen_color` with a `color` parameter: the agent changes the screen.
- `play_last_recording`: the agent plays back what it recorded.
- `set_volume` with a `level` parameter.
- `show_text`: the agent puts a short message on the screen in big letters.
- Improve the wake check: require speech-like rhythm, or a double clap.

For each new tool, add it in `runTool()` and add a matching Client tool on your agent.

## Done when
Auto mode starts a conversation when you speak, and the agent has run at least one tool
on your device.

## Stuck?
- The agent says it cannot record: the tool is not defined on the agent, or the name does
  not match exactly.
- "error: no SD card": reseat the card and reset the device.
- Auto never wakes: lower `WAKE_PEAK`. Wakes constantly: raise it.
- See `fallback-prompts/05-always-listening.md`.
