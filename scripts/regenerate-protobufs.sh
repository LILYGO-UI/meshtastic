#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
protoc_bin=${MESHTASTIC_PROTOC:-protoc}

if [ "$("$protoc_bin" --version)" != "libprotoc 36.0" ]; then
    echo "Meshtastic code generation requires protoc 36.0" >&2
    exit 1
fi

"$protoc_bin" \
    --proto_path="$project_root/third_party/meshtastic-protobufs" \
    --cpp_out="lite:$project_root/src/generated" \
    meshtastic/admin.proto \
    meshtastic/apponly.proto \
    meshtastic/atak.proto \
    meshtastic/channel.proto \
    meshtastic/config.proto \
    meshtastic/connection_status.proto \
    meshtastic/device_ui.proto \
    meshtastic/module_config.proto \
    meshtastic/portnums.proto \
    meshtastic/telemetry.proto \
    meshtastic/xmodem.proto \
    meshtastic/mesh.proto
