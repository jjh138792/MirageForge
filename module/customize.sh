#!/system/bin/sh
DATA_DIR="/data/adb/mirage_forge"
APPS_DIR="$DATA_DIR/apps"

ui_print "- 正在初始化蜃楼工坊数据中心: $DATA_DIR"
mkdir -p "$APPS_DIR"

if [ ! -f "$DATA_DIR/config.json" ]; then
  cat << 'EOF' > "$DATA_DIR/config.json"
{
  "templates": {
    "xiaomi_15_pro": {
      "id": "xiaomi_15_pro",
      "name": "Xiaomi 15 Pro (澎湃旗舰)",
      "MODEL": "2410DPN6CC",
      "BRAND": "Xiaomi",
      "MANUFACTURER": "Xiaomi",
      "DEVICE": "haotian",
      "PRODUCT": "haotian",
      "HARDWARE": "qcom",
      "BOARD": "sun",
      "FINGERPRINT": "Xiaomi/haotian/haotian:15/AP3A.240905.015.A2/OS2.0.12.0.VOBCNXM:user/release-keys"
    },
    "rog_8_pro": {
      "id": "rog_8_pro",
      "name": "ROG 游戏手机 8 Pro (高帧电竞)",
      "MODEL": "ASUS_AI2401_A",
      "BRAND": "asus",
      "MANUFACTURER": "asus",
      "DEVICE": "AI2401",
      "PRODUCT": "CN_AI2401",
      "HARDWARE": "qcom",
      "BOARD": "pineapple",
      "FINGERPRINT": "asus/CN_AI2401/AI2401:14/UKQ1.230804.001/34.1420.1420.218:user/release-keys"
    }
  },
  "appRoutes": {}
}
EOF
fi

chmod -R 700 "$DATA_DIR"
ui_print "- WebUI 面板已就绪，可在 KernelSU / WebUI 管理器中直接打开配置"
