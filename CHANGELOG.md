## MirageForge v1.2.0

- **核心安全**：虚拟属性池升级为 `r--p` 只读匿名 `mmap` 内存页，反射字典在 `postAppSpecialize` 后阅后即焚，防堆内存特征扫描
- **伪装扩展**：新增 `Build.BOARD`、`Build.SOC_MODEL`、`DISPLAY_ID`、`INCREMENTAL`、`SECURITY_PATCH`、SELinux app 域受限属性区与按应用独立 DPI 缩放
- **液态玻璃 WebUI**：引入球面法线光学折射透镜与二阶胡克定律弹簧阻尼导航球（支持长按放大与阻尼拖拽），新增日/夜/跟随系统三态平滑切换
- **零 Shell Fork**：接入 KernelSU `pm`/`io` 原生接口，包枚举与配置读写零子进程开销
- **版本统一**：全端动态读取 `module.prop` 版本号，彻底消除硬编码
