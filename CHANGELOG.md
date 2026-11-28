## MirageForge 1.3.0

### 新功能
* 系统属性升级为「传统 Hook / 纯 COW / MountNS」三引擎自由切换，支持在属性区尾部原生插入跨品牌属性；移除独立 COW 开关，改为在「伪装系统属性」行双击或长按切换引擎
* 新增「新安装应用自动加入伪装」，基于 Linux inotify 零耗电监听新装用户应用，内置 Root 管理器安全排除并支持自定义包名/前缀排除
* 新增 Java 层 `ActivityManager.getMemoryInfo` 内存伪装，同步接管总内存与可用内存显示
* 应用编辑页新增一键场景预设（极致隐匿 / 游戏高刷 / 全量伪装）、单应用专属局部覆写、可选 HDR / 杜比视界能力声明及实时生效态徽章
* 新增全屏左右滑页手势，实时联动底部液态玻璃导航球形变与切页

### 修复
* 修复 Android 17 / Zygisk Next 真机下 MountNS VFS 挂载在进程特化阶段被清场回退的问题，改为两阶段独立通道挂载
* 修复真机 Split APK 内嵌库及 Android 17 `libvulkan.so` 导致的 Vulkan 伪装失效问题
* 修复 `cpufreq` 符号链接路径不匹配导致 MountNS 模式下仍触发多余 `VFS_OPEN` 的问题
* 修复低内存宿主机伪装大内存时，可用内存按剩余比例缩放偏低的问题

### 改进
* 强化全方位反检测：无 Hook 场景支持 `DLCLOSE` 零驻留自卸载、补齐 `statfs/fstatfs` 文件系统魔数伪造、`mountinfo/maps` 深度清洗、中性暂存区即挂即蒸与 MountNS 纯内存活体动态波动
* 大幅优化冷启动速度并收紧「0 改动」门控：MountNS 生效后彻底免挂文件 I/O PLT Hook，GOT 修补消除 `/proc/self/maps` 重复读盘并跳过无关系统与巨型驱动库
* 老版本 `routes.db` 与 `config.json` 升级后自动无损迁移至新版 16 列格式
