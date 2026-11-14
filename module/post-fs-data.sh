#!/system/bin/sh
MODDIR=${0%/*}
DATA_DIR="/data/adb/mirage_forge"
COUNT_FILE="$DATA_DIR/boot_count"
CRASH_LOG="$DATA_DIR/crash_events.log"
ROUTES_DB="$DATA_DIR/routes.db"
ROUTES_BAK="$DATA_DIR/routes.db.bootloop_bak"
MAX_RETRY=2

mkdir -p "$DATA_DIR"
chmod 0700 "$DATA_DIR"

# 如果模块已经被禁用，不进行计数
if [ -f "$MODDIR/disable" ]; then
  exit 0
fi

# 读取当前未完成开机的累计次数
COUNT=0
if [ -f "$COUNT_FILE" ]; then
  COUNT=$(cat "$COUNT_FILE" 2>/dev/null)
  case "$COUNT" in
    ''|*[!0-9]*) COUNT=0 ;;
  esac
fi

COUNT=$((COUNT + 1))

# 触发防砖阈值：连续 MAX_RETRY 次未能正常进入桌面并稳定运行 15 秒
if [ "$COUNT" -gt "$MAX_RETRY" ]; then
  # 1. 创建 disable 文件，通知 Magisk / KernelSU / APatch 禁用本模块
  touch "$MODDIR/disable"

  # 2. 双重保险：临时挂起 routes.db，确保当前启动周期内 Zygisk Companion 100% 放行所有进程
  if [ -f "$ROUTES_DB" ]; then
    mv -f "$ROUTES_DB" "$ROUTES_BAK"
  fi

  # 3. 重置计数器，方便用户后续排障后重新开启模块
  echo "0" > "$COUNT_FILE"

  # 4. 写入 crash_events.log，使 WebUI「幻境探针」面板直接显示红字告警
  TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "boot")
  echo "[CRASH][system_boot][BOOTLOOP_GUARD] code=$COUNT fault_addr=0x0 | Stack: 检测到连续 ${MAX_RETRY} 次未能正常开机 ($TIMESTAMP)，防砖看门狗已自动关闭模块并保护系统启动" >> "$CRASH_LOG"
  chmod 0600 "$CRASH_LOG"

  # 5. 更新管理器卡片描述提示用户
  sed -i 's/^description=.*/description=⚠️ [防砖看门狗已触发：检测到连续卡开机，模块已自动关闭！重新开启模块即可恢复原有配置] /' "$MODDIR/module.prop" 2>/dev/null
  exit 0
fi

# 正常记录本次启动计数
echo "$COUNT" > "$COUNT_FILE"
chmod 0600 "$COUNT_FILE"
