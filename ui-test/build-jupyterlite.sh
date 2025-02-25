#!/bin/bash
set -e

BUILD_PREFIX=$MAMBA_ROOT_PREFIX/envs/xeus-cpp-wasm-build
PREFIX=$MAMBA_ROOT_PREFIX/envs/xeus-cpp-wasm-host
SYSROOT_PATH=$BUILD_PREFIX/opt/emsdk/upstream/emscripten/cache/sysroot

eval "$(micromamba shell hook --shell bash)"

if lsof -i :8000 > /dev/null 2>&1; then
  echo "Port 8000 is in use, attempting to free it..."
  pkill -f "jupyter lite serve" || true
  sleep 2 # Give it time to terminate
fi

if ! micromamba env list | grep -q xeus-lite-host; then
  micromamba create -n xeus-lite-host jupyterlite-core -c conda-forge --yes
fi

micromamba run -n xeus-lite-host jupyter lite build --XeusAddon.prefix=$PREFIX

echo "Starting JupyterLite server..."
micromamba run -n xeus-lite-host jupyter lite serve --XeusAddon.prefix=$PREFIX > jupyterlite.log 2>&1 &
SERVER_PID=$!

sleep 5

if ps -p $SERVER_PID > /dev/null; then
  echo "JupyterLite server started successfully (PID: $SERVER_PID)"
else
  echo "Failed to start JupyterLite server. Check jupyterlite.log for details:"
  cat jupyterlite.log
  exit 1
fi