#!/system/bin/sh
# 声明由 customize.sh 完全接管解压与安装流程
SKIPUNZIP=1

ui_print "==========================================="
ui_print "   蜃楼工坊 · MirageForge v2.0 (Zygisk)   "
ui_print "   Five-Layer Deep Device Virtualization   "
ui_print "==========================================="
ui_print "- 设备架构: $ARCH ($ABI)"
ui_print "- Android API: $API"

if [ "$API" -lt 26 ]; then
  abort "! 错误：MirageForge 仅支持 Android 8.0 (API 26) 及以上系统"
fi

# 1. 解压模块核心文件至 $MODPATH
ui_print "- 正在解压模块核心组件..."
unzip -o "$ZIPFILE" \
  "module.prop" \
  "uninstall.sh" \
  "zygisk/*" \
  "webroot/*" \
  -d "$MODPATH" >&2 || abort "! 解压模块文件失败，请检查刷入包完整性"

# 2. 校验当前架构的 Zygisk 动态库是否存在
if [ ! -f "$MODPATH/zygisk/$ABI.so" ]; then
  abort "! 错误：未找到适配当前架构 ($ABI) 的 Zygisk 引擎库"
fi
ui_print "- 已校验 Zygisk 注入引擎: zygisk/$ABI.so"

# 3. 初始化配置中心与运行时编译目录
DATA_DIR="/data/adb/mirage_forge"
APPS_DIR="$DATA_DIR/apps"

ui_print "- 正在构建配置与二进制缓存目录: $DATA_DIR"
mkdir -p "$APPS_DIR"

if [ ! -f "$DATA_DIR/config.json" ]; then
  ui_print "- 首次安装，正在生成预设旗舰机型库..."
  cat << 'EOF' > "$DATA_DIR/config.json"
{
  "templates": {
    "xiaomi_15_pro": {
      "id": "xiaomi_15_pro",
      "name": "Xiaomi 15 Pro (澎湃 OS 2)",
      "MODEL": "2410DPN6CC",
      "BRAND": "Xiaomi",
      "MANUFACTURER": "Xiaomi",
      "DEVICE": "haotian",
      "PRODUCT": "haotian",
      "HARDWARE": "qcom",
      "BOARD": "sun",
      "SOC_MANUFACTURER": "Qualcomm",
      "SOC_MODEL": "SM8750-AB",
      "BUILD_ID": "AP3A.240905.015.A2",
      "RELEASE": "15",
      "SDK_INT": "35",
      "SECURITY_PATCH": "2025-02-01",
      "FINGERPRINT": "Xiaomi/haotian/haotian:15/AP3A.240905.015.A2/OS2.0.102.0.VOBCNXM:user/release-keys",
      "MEMFD_SHIELD": true,
      "CUSTOM_PROPS": "ro.miui.ui.version.code=15\nro.miui.ui.version.name=V816\nro.product.marketname=Xiaomi 15 Pro"
    },
    "rog_9_pro": {
      "id": "rog_9_pro",
      "name": "ROG 游戏手机 9 Pro (满血高帧)",
      "MODEL": "ASUS_AI2501_A",
      "BRAND": "asus",
      "MANUFACTURER": "asus",
      "DEVICE": "AI2501",
      "PRODUCT": "CN_AI2501",
      "HARDWARE": "qcom",
      "BOARD": "sun",
      "SOC_MANUFACTURER": "Qualcomm",
      "SOC_MODEL": "SM8750-AB",
      "BUILD_ID": "AQ3A.240812.002",
      "RELEASE": "15",
      "SDK_INT": "35",
      "SECURITY_PATCH": "2025-01-05",
      "FINGERPRINT": "asus/CN_AI2501/AI2501:15/AQ3A.240812.002/35.1810.1810.88:user/release-keys",
      "MEMFD_SHIELD": true,
      "CUSTOM_PROPS": "ro.product.asus.series=ROG Phone 9"
    }
  },
  "appRoutes": {}
}
EOF
fi

# 4. 设置模块与数据目录权限及 SELinux 上下文
ui_print "- 正在设置文件权限与 SELinux 安全上下文..."
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/zygisk" 0 0 0755 0644
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644
chmod 0700 "$DATA_DIR"
chmod -R 0600 "$DATA_DIR"/* 2>/dev/null
chmod 0700 "$APPS_DIR"
chcon -R u:object_r:system_file:s0 "$MODPATH" 2>/dev/null

ui_print "- MirageForge v2.0 安装完成！重启后生效。"
