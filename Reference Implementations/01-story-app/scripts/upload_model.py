# PlatformIO extra script: adds a "upload_model" target that flashes model/model.bin
# into the custom "model" partition (see partitions.csv).
#   pio run -t upload_model
Import("env")
import os

MODEL_OFFSET = "0x190000"
model_path = os.path.join(env.subst("$PROJECT_DIR"), "model", "model.bin")

def upload_model(source, target, env):
    if not os.path.isfile(model_path):
        print("model/model.bin not found")
        env.Exit(1)
    env.AutodetectUploadPort()
    cmd = ('"$UPLOADER" --chip esp32s3 --port "$UPLOAD_PORT" --baud 921600 '
           'write_flash %s "%s"' % (MODEL_OFFSET, model_path))
    return env.Execute(env.VerboseAction(cmd, "Flashing model.bin to " + MODEL_OFFSET))

env.AddCustomTarget("upload_model", None, upload_model,
                    title="Upload model", description="Flash model.bin to the model partition")
