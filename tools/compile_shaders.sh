#!/bin/sh
# Regenerates engine/rhi/vulkan/shaders/*.spv.h from engine/shaders/*.{vert,frag}.
# Needs glslangValidator (apt: glslang-tools). Generated headers are committed so
# a normal build does not need the tool.
set -e
cd "$(dirname "$0")/.."
for f in engine/shaders/*.vert engine/shaders/*.frag; do
  b=$(basename "$f"); n=$(echo "$b" | tr '.' '_')
  glslangValidator -V --target-env vulkan1.3 --vn "${n}_spv" -o "engine/rhi/vulkan/shaders/${b}.spv.h" "$f"
done
