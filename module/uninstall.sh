#!/system/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2025-2026 jjh138792
# MirageForge: https://github.com/jjh138792/MirageForge

PID_FILE="/data/adb/mirage_forge/watch_installs.pid"
if [ -f "$PID_FILE" ]; then
  WPID=$(cat "$PID_FILE" 2>/dev/null)
  if [ -n "$WPID" ] && [ -r "/proc/$WPID/cmdline" ]; then
    if grep -q "watch-installs" "/proc/$WPID/cmdline" 2>/dev/null; then
      kill "$WPID" 2>/dev/null
    fi
  fi
fi

rm -rf /data/adb/mirage_forge
rm -rf /data/local/tmp/.st_ns
