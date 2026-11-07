#!/system/bin/sh
SKIPUNZIP=1

ui_print "========================================================"
ui_print "   蜃楼工坊 · MirageForge v1.0.1                        "
ui_print "   CLI-Driven Hardware Hypervisor & Stack Backtrace     "
ui_print "========================================================"
ui_print "- 设备架构: $ARCH ($ABI) | Android API: $API"

if [ "$API" -lt 26 ]; then
  abort "! 错误：MirageForge 仅支持 Android 8.0 (API 26) 及以上版本"
fi

ui_print "- 正在解压模块核心文件..."
unzip -o "$ZIPFILE" \
  "module.prop" \
  "action.sh" \
  "uninstall.sh" \
  "bin/*" \
  "zygisk/*" \
  "webroot/*" \
  -d "$MODPATH" >&2 || abort "! 解压模块文件失败，请检查刷入包完整性"

# 1. 校验 Zygisk 引擎动态库
if [ ! -f "$MODPATH/zygisk/$ABI.so" ]; then
  abort "! 错误：未找到适配当前架构 ($ABI) 的 Zygisk 动态库"
fi
ui_print "- 已校验 Zygisk 引擎: zygisk/$ABI.so"

# 2. 部署适配当前架构的 mirageforge_cli 二进制文件
mkdir -p "$MODPATH/bin"
if [ -f "$MODPATH/bin/$ABI/mirageforge_cli" ]; then
  mv -f "$MODPATH/bin/$ABI/mirageforge_cli" "$MODPATH/bin/mirageforge_cli"
fi
rm -rf "$MODPATH/bin/arm64-v8a" "$MODPATH/bin/armeabi-v7a" "$MODPATH/bin/x86_64" "$MODPATH/bin/x86" 2>/dev/null

if [ ! -f "$MODPATH/bin/mirageforge_cli" ]; then
  abort "! 错误：未找到适配当前架构 ($ABI) 的 mirageforge_cli 控制中枢"
fi

chmod 0755 "$MODPATH/bin/mirageforge_cli"
chcon u:object_r:system_file:s0 "$MODPATH/bin/mirageforge_cli" 2>/dev/null

# 创建 /system/bin 软链接，方便在终端直接运行 mirageforge_cli
mkdir -p "$MODPATH/system/bin"
ln -sf "/data/adb/modules/mirage_forge/bin/mirageforge_cli" "$MODPATH/system/bin/mirageforge_cli"

# 3. 调用 mirageforge_cli init 初始化数据中心与同步编译所有应用配置
ui_print "- 正在通过 mirageforge_cli 初始化全息模板库与 DDR 对称配置..."
"$MODPATH/bin/mirageforge_cli" init
"$MODPATH/bin/mirageforge_cli" sync >/dev/null 2>&1

# 4. 设置权限与安全上下文
ui_print "- 正在设置文件权限与 SELinux 安全上下文..."
set_perm_recursive "$MODPATH/zygisk" 0 0 0755 0644
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

ui_print "- MirageForge v1.0.1 安装完成！重启设备后生效。"
