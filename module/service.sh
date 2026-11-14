#!/system/bin/sh
MODDIR=${0%/*}
DATA_DIR="/data/adb/mirage_forge"
COUNT_FILE="$DATA_DIR/boot_count"
ROUTES_DB="$DATA_DIR/routes.db"
ROUTES_BAK="$DATA_DIR/routes.db.bootloop_bak"
NORMAL_DESC="description=全息硬件虚拟化引擎：只读匿名页属性池 | 阅后即焚反扫描 | 按应用独立 DPI 与受限属性区 | 异构单核动态频率 | 光学液态玻璃 WebUI"

# 后台异步等待系统完全启动，绝不阻塞其他模块的 service.sh 执行
(
  # 1. 等待系统发出 boot_completed 信号（最长等待 180 秒）
  WAIT_SEC=0
  while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 2
    WAIT_SEC=$((WAIT_SEC + 2))
    if [ "$WAIT_SEC" -ge 180 ]; then
      exit 0
    fi
  done

  # 2. 额外观察 15 秒，防范进入桌面后立即触发 SystemUI 崩溃软重启
  sleep 15

  if [ "$(getprop sys.boot_completed)" = "1" ]; then
    mkdir -p "$DATA_DIR"
    echo "0" > "$COUNT_FILE"
    chmod 0600 "$COUNT_FILE"

    # 若用户排障后手动重新启用了模块且本次顺利开机，自动恢复备份的路由表与模块描述
    if [ ! -f "$MODDIR/disable" ]; then
      if [ ! -f "$ROUTES_DB" ] && [ -f "$ROUTES_BAK" ]; then
        mv -f "$ROUTES_BAK" "$ROUTES_DB"
        chmod 0600 "$ROUTES_DB"
      fi
      sed -i "s|^description=.*|$NORMAL_DESC|" "$MODDIR/module.prop" 2>/dev/null
    fi
  fi
) &
