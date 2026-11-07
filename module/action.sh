#!/system/bin/sh
CLI="/data/adb/modules/mirage_forge/bin/mirageforge_cli"

if [ ! -x "$CLI" ]; then
  echo "! 错误：未找到可执行的 $CLI"
  exit 1
fi

"$CLI" status

echo "--------------------------------------------------------"
echo "- 最近 6 次应用注入遥测 (probe telemetry):"
"$CLI" probe telemetry --limit 6

echo "--------------------------------------------------------"
echo "- 最近捕获的 Native 崩溃信号 (probe crashes):"
"$CLI" probe crashes --limit 5

echo "--------------------------------------------------------"
echo "- 最近 6 条幻境探针与调用栈回溯 (probe list):"
"$CLI" probe list --limit 6
echo "========================================================"
