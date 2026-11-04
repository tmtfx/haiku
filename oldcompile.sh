#!/boot/system/bin/env bash

export HAIKU_REVISION=PiratiDelFrico-rev11
export HAIKU_IMAGE_SIZE=1280

time jam -j$(nproc) \
    -sHAIKU_INCLUDE_TRADEMARKS="#" \
    -sLOVE_IS_IN_THE_AIR=1 \
    -q @nightly-anyboot
