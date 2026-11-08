# Stage 01 - First upload: a language model on your CoreS3

## Goal
Flash a 28.9-million-parameter language model onto the CoreS3 and watch it write
children's stories on the screen, with no network and no server. Then make one change
to it using Claude Code.

This is a port of [slvDev/esp32-ai](https://github.com/slvDev/esp32-ai) (MIT) to the
CoreS3. The model is trained on the TinyStories dataset and runs about 6 tokens per second
on this board.

## Setup
From the repo root:

```bash
cd "Reference Implementations/01-story-app"
```

Flash the model into its own flash partition. This is 15 MB and takes about two minutes.
You only do it once; reflashing the app later does not touch it.

```bash
pio run -t upload_model
```

Then build and flash the app:

```bash
pio run -t upload
```

Watch it:

```bash
pio device monitor
```

The screen shows "loading model...", then a story streams in. The bar at the top shows
tokens per second. Tap the screen to start a new story. In the serial monitor, press `n`
for the same.

## Now change it with Claude Code
Open Claude Code in the `01-story-app` folder:

```bash
claude
```

Pick one:
- Change the colours or the text size on the screen.
- Make the status bar show how many tokens have been generated so far.
- Add a new starting prompt. The token ids are generated from the tokenizer; ask Claude
  how, or look at `src/main.cpp` for the list and the comment above it.
- Change the temperature and see how the stories differ (0 is the same story every time).

Rebuild and reflash with `pio run -t upload`. The model stays where it is.

## How it works, in one paragraph
The model's weights live in a custom flash partition and are memory-mapped, so the
25-million-parameter embedding table is read straight from flash one row per token. The
small transformer core is unpacked to int8 in PSRAM at boot, and the hot scratch buffers
sit in internal SRAM. Two CPU cores split the big matrix-vector products. See the comments
at the top of `src/main.cpp` and the original project's README for the details.

## Done when
A story is streaming on the screen, tapping starts a new one, and you have rebuilt the app
with at least one change of your own.

## Stuck?
- "model partition not found" or "bad model magic" on screen: run `pio run -t upload_model`.
- Upload fails: check the cable is a data cable plugged directly into the laptop, and
  that nothing else (a serial monitor) has the port open.
- See `fallback-prompts/01-first-upload.md`.
