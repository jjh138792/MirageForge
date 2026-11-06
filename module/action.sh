#!/system/bin/sh
CLI="/data/adb/modules/mirage_forge/bin/mirageforge_cli"
LAUNCH_LOG="/data/adb/mirage_forge/launch_history.log"

if [ -x "$CLI" ]; then
  "$CLI" status
else
  echo "! 错误：未找到可执行的 $CLI"
  exit 1
fi

echo "------------------------------------------"
echo "- 最近 8 次应用注入遥测 (launch_history):"
if [ -f "$LAUNCH_LOG" ]; then
  tail -n 8 "$LAUNCH_LOG" | while IFS='|' read -r ts pkg pid model; do
    echo "  [PID:$pid] $pkg => 伪装机型: $model"
  done
else
  echo "  (暂无注入记录，启动已配置的目标应用后自动生成)"
fi

echo "------------------------------------------"
echo "- 最近 8 条幻境探针拦截记录:"
if [ -x "$CLI" ]; then
  "$CLI" probe list --limit 8
fi
echo "=========================================="
