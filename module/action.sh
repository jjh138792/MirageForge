#!/system/bin/sh
DATA_DIR="/data/adb/mirage_forge"
APPS_DIR="$DATA_DIR/apps"

echo "=========================================="
echo "   蜃楼工坊 · MirageForge 状态自检   "
echo "=========================================="
COUNT=$(ls -1 "$APPS_DIR"/*.conf 2>/dev/null | wc -l)
echo "- 当前已编译生效的应用分流数: $COUNT 款"

if [ "$COUNT" -gt 0 ]; then
  for f in "$APPS_DIR"/*.conf; do
    PKG=$(basename "$f" .conf)
    MODEL=$(grep "^BUILD:MODEL=" "$f" | cut -d'=' -f2)
    GPU=$(grep "^GLES:RENDERER=" "$f" | cut -d'=' -f2)
    PROBE=$(grep "^META:ENABLE_PROBE=" "$f" | cut -d'=' -f2)
    echo "  * $PKG -> [$MODEL | $GPU | 探针:$PROBE]"
  done
fi

echo "------------------------------------------"
echo "- 最近 10 条幻境探针拦截记录:"
logcat -d -s MirageForge:I | grep "\[PROBE\]" | tail -n 10
echo "=========================================="
