#!/system/bin/sh
SKIPUNZIP=1

ui_print "========================================================"
ui_print "   蜃楼工坊 · MirageForge  "
ui_print "========================================================"
ui_print "- 设备架构: $ARCH ($ABI) | Android API: $API"

if [ "$API" -lt 26 ]; then
  abort "! 错误：MirageForge v4.0 仅支持 Android 8.0 (API 26) 及以上版本"
fi

ui_print "- 正在解压模块核心文件与 WebUI 静态资源..."
unzip -o "$ZIPFILE" \
  "module.prop" \
  "action.sh" \
  "uninstall.sh" \
  "zygisk/*" \
  "webroot/*" \
  -d "$MODPATH" >&2 || abort "! 解压模块文件失败，请检查 ZIP 完整性"

if [ ! -f "$MODPATH/zygisk/$ABI.so" ]; then
  abort "! 错误：安装包内未包含当前架构 ($ABI) 的 Zygisk 引擎库"
fi
ui_print "- 已就绪 Zygisk v4 原生引擎: zygisk/$ABI.so"

DATA_DIR="/data/adb/mirage_forge"
APPS_DIR="$DATA_DIR/apps"
mkdir -p "$APPS_DIR"

if [ ! -f "$DATA_DIR/config.json" ]; then
  ui_print "- 正在生成 v4.0 旗舰级全息硬件模板库..."
  cat << 'EOF' > "$DATA_DIR/config.json"
{
  "templates": {
    "xiaomi_15_pro": {
      "id": "xiaomi_15_pro",
      "name": "Xiaomi 15 Pro (骁龙8至尊版 / Adreno 830)",
      "MODEL": "2410DPN6CC",
      "BRAND": "Xiaomi",
      "MANUFACTURER": "Xiaomi",
      "DEVICE": "haotian",
      "PRODUCT": "haotian",
      "BOARD": "sun",
      "HARDWARE": "qcom",
      "BOOTLOADER": "unknown",
      "SOC_MANUFACTURER": "Qualcomm",
      "SOC_MODEL": "SM8750-AB",
      "SUPPORTED_ABIS": "arm64-v8a,armeabi-v7a,armeabi",
      "BUILD_ID": "AP3A.240905.015.A2",
      "BUILD_TIME": "1736899200000",
      "RELEASE": "15",
      "SDK_INT": "35",
      "SECURITY_PATCH": "2025-02-01",
      "FINGERPRINT": "Xiaomi/haotian/haotian:15/AP3A.240905.015.A2/OS2.0.102.0.VOBCNXM:user/release-keys",
      "GL_VENDOR": "Qualcomm",
      "GL_RENDERER": "Adreno (TM) 830",
      "GL_VERSION": "OpenGL ES 3.2 V@0800.0 (GIT@8f4b2a1, I9b3d7e6c1a, 1731000000)",
      "CPU_HARDWARE": "Qualcomm Technologies, Inc SM8750",
      "CPU_CORES": 8,
      "CPU_MAX_FREQ": 4320000,
      "RAM_GB": 16,
      "SOC_ID": "618",
      "SOC_MACHINE": "SM8750",
      "KERNEL_RELEASE": "6.6.30-android15-8-g94b8f12ab4c1",
      "KERNEL_VERSION": "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025",
      "CUSTOM_PROPS": "ro.miui.ui.version.code=15\nro.miui.ui.version.name=V816\nro.product.marketname=Xiaomi 15 Pro"
    },
    "rog_9_pro": {
      "id": "rog_9_pro",
      "name": "ROG 游戏手机 9 Pro (24GB / 165Hz 满血电竞)",
      "MODEL": "ASUS_AI2501_A",
      "BRAND": "asus",
      "MANUFACTURER": "asus",
      "DEVICE": "AI2501",
      "PRODUCT": "CN_AI2501",
      "BOARD": "sun",
      "HARDWARE": "qcom",
      "BOOTLOADER": "unknown",
      "SOC_MANUFACTURER": "Qualcomm",
      "SOC_MODEL": "SM8750-AB",
      "SUPPORTED_ABIS": "arm64-v8a,armeabi-v7a,armeabi",
      "BUILD_ID": "AQ3A.240812.002",
      "BUILD_TIME": "1737504000000",
      "RELEASE": "15",
      "SDK_INT": "35",
      "SECURITY_PATCH": "2025-02-05",
      "FINGERPRINT": "asus/CN_AI2501/AI2501:15/AQ3A.240812.002/35.1810.1810.88:user/release-keys",
      "GL_VENDOR": "Qualcomm",
      "GL_RENDERER": "Adreno (TM) 830",
      "GL_VERSION": "OpenGL ES 3.2 V@0800.0",
      "CPU_HARDWARE": "Qualcomm Technologies, Inc SM8750",
      "CPU_CORES": 8,
      "CPU_MAX_FREQ": 4320000,
      "RAM_GB": 24,
      "SOC_ID": "618",
      "SOC_MACHINE": "SM8750",
      "KERNEL_RELEASE": "6.6.30-android15-8-rog9",
      "KERNEL_VERSION": "#1 SMP PREEMPT Mon Jan 20 08:30:00 UTC 2025",
      "CUSTOM_PROPS": "ro.product.asus.series=ROG Phone 9\nro.vendor.asus.gaming=1"
    },
    "redmagic_10_pro": {
      "id": "redmagic_10_pro",
      "name": "红魔 10 Pro+ (24GB 氘锋透明版)",
      "MODEL": "NX789J",
      "BRAND": "nubia",
      "MANUFACTURER": "ZTE",
      "DEVICE": "NX789J",
      "PRODUCT": "NX789J",
      "BOARD": "sun",
      "HARDWARE": "qcom",
      "BOOTLOADER": "unknown",
      "SOC_MANUFACTURER": "Qualcomm",
      "SOC_MODEL": "SM8750-AB",
      "SUPPORTED_ABIS": "arm64-v8a,armeabi-v7a,armeabi",
      "BUILD_ID": "AP3A.240905.015.A2",
      "BUILD_TIME": "1736467200000",
      "RELEASE": "15",
      "SDK_INT": "35",
      "SECURITY_PATCH": "2025-01-01",
      "FINGERPRINT": "nubia/NX789J/NX789J:15/AP3A.240905.015.A2/20250110.213512:user/release-keys",
      "GL_VENDOR": "Qualcomm",
      "GL_RENDERER": "Adreno (TM) 830",
      "GL_VERSION": "OpenGL ES 3.2 V@0800.0",
      "CPU_HARDWARE": "Qualcomm Technologies, Inc SM8750",
      "CPU_CORES": 8,
      "CPU_MAX_FREQ": 4320000,
      "RAM_GB": 24,
      "SOC_ID": "618",
      "SOC_MACHINE": "SM8750",
      "KERNEL_RELEASE": "6.6.30-android15-8-redmagic",
      "KERNEL_VERSION": "#1 SMP PREEMPT Fri Jan 10 21:35:12 CST 2025",
      "CUSTOM_PROPS": "ro.product.marketname=红魔 10 Pro+"
    },
    "vivo_x200_pro": {
      "id": "vivo_x200_pro",
      "name": "vivo X200 Pro (天玑9400 / Immortalis-G925)",
      "MODEL": "V2405A",
      "BRAND": "vivo",
      "MANUFACTURER": "vivo",
      "DEVICE": "PD2405",
      "PRODUCT": "PD2405",
      "BOARD": "mt6991",
      "HARDWARE": "mt6991",
      "BOOTLOADER": "unknown",
      "SOC_MANUFACTURER": "Mediatek",
      "SOC_MODEL": "MT6991",
      "SUPPORTED_ABIS": "arm64-v8a,armeabi-v7a,armeabi",
      "BUILD_ID": "AP3A.240905.015.A2",
      "BUILD_TIME": "1732105920000",
      "RELEASE": "15",
      "SDK_INT": "35",
      "SECURITY_PATCH": "2025-02-01",
      "FINGERPRINT": "vivo/PD2405/PD2405:15/AP3A.240905.015.A2/compiler11201512:user/release-keys",
      "GL_VENDOR": "ARM",
      "GL_RENDERER": "Immortalis-G925 MC12",
      "GL_VERSION": "OpenGL ES 3.2 v1.r49p0-01eac0",
      "CPU_HARDWARE": "MT6991",
      "CPU_CORES": 8,
      "CPU_MAX_FREQ": 3620000,
      "RAM_GB": 16,
      "SOC_ID": "6991",
      "SOC_MACHINE": "MT6991",
      "KERNEL_RELEASE": "6.6.30-android15-8-mt6991",
      "KERNEL_VERSION": "#1 SMP PREEMPT Wed Nov 20 15:12:00 CST 2024",
      "CUSTOM_PROPS": "ro.vivo.os.build.display.id=OriginOS 5\nro.vivo.market.name=vivo X200 Pro"
    }
  },
  "appRoutes": {}
}
EOF
fi

ui_print "- 正在配置模块与数据沙箱权限..."
set_perm_recursive "$MODPATH/zygisk" 0 0 0755 0644
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
chmod 0700 "$DATA_DIR"
chmod 0700 "$APPS_DIR"
chmod 0600 "$DATA_DIR/config.json" 2>/dev/null
chmod 0600 "$APPS_DIR"/*.conf 2>/dev/null

ui_print "- MirageForge 部署完成！请在 KernelSU / WebUI-X 中打开配置中心。"
