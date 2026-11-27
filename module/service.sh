#!/system/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2025-2026 jjh138792
# MirageForge: https://github.com/jjh138792/MirageForge

MODDIR="${0%/*}"
CLI="$MODDIR/bin/mirageforge_cli"

# 1. 等待系统完全开机且 /data 解密就绪
while [ "$(getprop sys.boot_completed)" != "1" ]; do
  sleep 2
done

# 等待 /data/system/packages.list 就绪（最多 20 秒）
WAIT_TRY=0
while [ ! -f "/data/system/packages.list" ] && [ "$WAIT_TRY" -lt 10 ]; do
  sleep 2
  WAIT_TRY=$((WAIT_TRY + 1))
done

# 2. 清理上次开机遗留的临时挂载暂存目录
rm -rf /data/local/tmp/.st_ns 2>/dev/null

# 3. 初始化配置目录并拉起零耗电 inotify 新应用监听器（单实例自守卫）
if [ -x "$CLI" ]; then
  "$CLI" init >/dev/null 2>&1
  nohup "$CLI" watch-installs >/dev/null 2>&1 &
fi
