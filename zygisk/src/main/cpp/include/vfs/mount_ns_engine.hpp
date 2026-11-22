#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include "vfs/io_hooks.hpp"

namespace mirage {

constexpr const char *MNT_STAGE_DIR = "/data/adb/mirage_forge/mnt_stage";

// ============================================================================
// App 侧：收集静态硬件节点并通过控制通道请求 Root Companion 进行私有 Mount NS 挂载
// ============================================================================
inline uint32_t request_private_mount_ns_spoof(int ctrl_fd) {
    if (ctrl_fd < 0 || !g_cfg.vfs.enabled) return 0;

    std::vector<std::pair<std::string, std::string>> jobs;
    jobs.reserve(48);

    auto add_node = [&](const std::string &path) {
        std::string content;
        int dyn = -1;
        if (synthesize_vfs_node_on_demand(path, &content, &dyn) && dyn < 0 && !content.empty()) {
            jobs.emplace_back(path, std::move(content));
        }
    };

    static const char *const kStaticNodes[] = {
        "/proc/cpuinfo",
        "/proc/meminfo",
        "/proc/version",
        "/proc/ddr_info",
        "/proc/sys/kernel/osrelease",
        "/sys/class/kgsl/kgsl-3d0/gpu_model",
        "/sys/devices/soc0/soc_id",
        "/sys/devices/soc0/family",
        "/sys/devices/soc0/machine",
        "/sys/devices/system/chip-info/ddr_info",
        "/sys/devices/system/chip-info/ddr_type",
        "/sys/devices/system/chip-info/ddr_size",
        "/sys/firmware/devicetree/base/memory/ddr_device_type",
        "/sys/devices/system/cpu/present",
        "/sys/devices/system/cpu/possible",
        "/sys/devices/system/cpu/online",
        "/sys/devices/system/cpu/offline",
        "/sys/devices/system/cpu/kernel_max",
    };
    for (const char *p : kStaticNodes) add_node(p);

    int cores = (g_cfg.vfs.cpu_cores > 0 && g_cfg.vfs.cpu_cores <= 16) ? g_cfg.vfs.cpu_cores : 0;
    for (int i = 0; i < cores; ++i) {
        std::string pfx = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/";
        add_node(pfx + "online");
        add_node(pfx + "cpu_capacity");
        add_node(pfx + "regs/identification/midr_el1");
        add_node(pfx + "topology/core_cpus_list");
        add_node(pfx + "cpufreq/cpuinfo_max_freq");
        add_node(pfx + "cpufreq/cpuinfo_min_freq");
        add_node(pfx + "cpufreq/scaling_max_freq");
        add_node(pfx + "cpufreq/scaling_min_freq");
        add_node(pfx + "cpufreq/scaling_governor");
        add_node(pfx + "cpufreq/scaling_driver");
    }

    if (jobs.empty()) return 0;

    uint8_t cmd = CMD_MOUNT_NS;
    int32_t app_pid = static_cast<int32_t>(getpid());
    int32_t ppid = static_cast<int32_t>(getppid());
    uint32_t count = static_cast<uint32_t>(jobs.size());

    if (!write_full(ctrl_fd, &cmd, sizeof(cmd)) ||
        !write_full(ctrl_fd, &app_pid, sizeof(app_pid)) ||
        !write_full(ctrl_fd, &ppid, sizeof(ppid)) ||
        !write_full(ctrl_fd, &count, sizeof(count))) {
        return 0;
    }

    for (const auto &job : jobs) {
        write_str(ctrl_fd, job.first);
        write_str(ctrl_fd, job.second);
    }

    std::vector<uint8_t> results(count, 0);
    if (!read_full(ctrl_fd, results.data(), results.size())) {
        return 0;
    }

    uint32_t ok_cnt = 0;
    for (size_t i = 0; i < jobs.size(); ++i) {
        if (results[i] == 1) {
            g_ns_mounted_paths.insert(jobs[i].first);
            ++ok_cnt;
        }
    }
    if (ok_cnt > 0) {
        g_vfs_ns_mounted.store(true, std::memory_order_release);
    }
    return ok_cnt;
}

// ============================================================================
// Root Companion 侧：单线程 fork + setns 私有 Mount NS 内核级挂载实现
// ============================================================================
inline bool companion_handle_mount_ns_cmd(int sock_fd) {
    int32_t app_pid = 0;
    int32_t ppid = 0;
    uint32_t count = 0;
    if (!read_full(sock_fd, &app_pid, sizeof(app_pid)) ||
        !read_full(sock_fd, &ppid, sizeof(ppid)) ||
        !read_full(sock_fd, &count, sizeof(count))) {
        return false;
    }

    if (count > 256) count = 256;
    std::vector<std::string> targets;
    std::vector<std::string> contents;
    targets.reserve(count);
    contents.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        targets.push_back(read_str(sock_fd));
        contents.push_back(read_str(sock_fd));
    }

    std::vector<uint8_t> results(count, 0);

    // 绝对 0 改动安全闸门 1：校验目标进程确实处于独立 Mount Namespace（不与 Zygote / init 共享）
    char app_ns_path[64], ppid_ns_path[64];
    char app_ns_id[64] = {0}, ppid_ns_id[64] = {0}, init_ns_id[64] = {0};
    std::snprintf(app_ns_path, sizeof(app_ns_path), "/proc/%d/ns/mnt", app_pid);
    std::snprintf(ppid_ns_path, sizeof(ppid_ns_path), "/proc/%d/ns/mnt", ppid);

    ssize_t l_app  = readlink(app_ns_path, app_ns_id, sizeof(app_ns_id) - 1);
    ssize_t l_ppid = (ppid > 0) ? readlink(ppid_ns_path, ppid_ns_id, sizeof(ppid_ns_id) - 1) : -1;
    ssize_t l_init = readlink("/proc/1/ns/mnt", init_ns_id, sizeof(init_ns_id) - 1);

    bool ns_isolated = (l_app > 0) &&
                       (l_init <= 0 || std::strcmp(app_ns_id, init_ns_id) != 0) &&
                       (l_ppid <= 0 || std::strcmp(app_ns_id, ppid_ns_id) != 0);

    if (!ns_isolated || count == 0) {
        return write_full(sock_fd, results.data(), results.size());
    }

    mkdir(DATA_ROOT_DIR, 0700);
    mkdir(MNT_STAGE_DIR, 0755);

    // 预先提取裸指针数组，保证 fork 后的子进程全程 0 堆分配 (Async-Signal-Safe)
    std::vector<const char *> raw_targets(count, nullptr);
    std::vector<const char *> raw_datas(count, nullptr);
    std::vector<size_t> raw_lens(count, 0);
    for (uint32_t i = 0; i < count; ++i) {
        raw_targets[i] = targets[i].c_str();
        raw_datas[i]   = contents[i].data();
        raw_lens[i]    = contents[i].size();
    }

    int res_pipe[2] = {-1, -1};
    if (pipe(res_pipe) != 0) {
        return write_full(sock_fd, results.data(), results.size());
    }

    // 必须 fork 单线程子进程执行 setns(CLONE_NEWNS)，多线程进程直接调 setns 会被内核 EINVAL 拒绝
    pid_t worker = fork();
    if (worker == 0) {
        close(res_pipe[0]);
        uint8_t child_res[256] = {0};

        int ns_fd = open(app_ns_path, O_RDONLY | O_CLOEXEC);
        if (ns_fd >= 0 && syscall(__NR_setns, ns_fd, CLONE_NEWNS) == 0) {
            close(ns_fd);

            // 绝对 0 改动安全闸门 2：仅将 /proc 与 /sys 隔离为 MS_PRIVATE，杜绝挂载向外部传播且保留 /storage 传播
            bool proc_priv = (mount(nullptr, "/proc", nullptr, MS_SLAVE, nullptr) == 0 || true) &&
                             (mount(nullptr, "/proc", nullptr, MS_PRIVATE, nullptr) == 0);
            bool sys_priv  = (mount(nullptr, "/sys", nullptr, MS_SLAVE, nullptr) == 0 || true) &&
                             (mount(nullptr, "/sys", nullptr, MS_PRIVATE, nullptr) == 0);

            if (proc_priv || sys_priv) {
                static constexpr const char *kSysFileCtx = "u:object_r:system_file:s0";
                const size_t kCtxLen = std::strlen(kSysFileCtx) + 1;

                // 优先以 context=u:object_r:system_file:s0 挂载匿名 tmpfs；失败则退回普通 tmpfs + lsetxattr 验证
                bool ctx_mount_ok = (mount("tmpfs", MNT_STAGE_DIR, "tmpfs",
                                           MS_NOSUID | MS_NODEV,
                                           "size=2M,mode=0755,context=u:object_r:system_file:s0") == 0);
                bool plain_mount_ok = false;
                if (!ctx_mount_ok) {
                    plain_mount_ok = (mount("tmpfs", MNT_STAGE_DIR, "tmpfs",
                                            MS_NOSUID | MS_NODEV,
                                            "size=2M,mode=0755") == 0);
                }

                if (ctx_mount_ok || plain_mount_ok) {
                    mount(nullptr, MNT_STAGE_DIR, nullptr, MS_PRIVATE, nullptr);
                    bool label_verified = ctx_mount_ok;

                    if (!label_verified) {
                        char probe_f[128];
                        std::snprintf(probe_f, sizeof(probe_f), "%s/.p", MNT_STAGE_DIR);
                        int pfd = open(probe_f, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0444);
                        if (pfd >= 0) {
                            close(pfd);
                            label_verified = (lsetxattr(probe_f, "security.selinux", kSysFileCtx, kCtxLen, 0) == 0);
                            unlink(probe_f);
                        }
                    }

                    // 仅当 SELinux 标签确认对 untrusted_app 可读时才执行 Bind-Mount，防止 EACCES 破坏应用
                    if (label_verified) {
                        for (uint32_t i = 0; i < count; ++i) {
                            const char *tgt = raw_targets[i];
                            if (!tgt || tgt[0] != '/') continue;
                            bool is_proc = (std::strncmp(tgt, "/proc/", 6) == 0);
                            bool is_sys  = (std::strncmp(tgt, "/sys/", 5) == 0);
                            if ((is_proc && !proc_priv) || (is_sys && !sys_priv) || (!is_proc && !is_sys)) {
                                continue;
                            }
                            if (access(tgt, F_OK) != 0) continue;

                            char src_path[128];
                            std::snprintf(src_path, sizeof(src_path), "%s/n%u", MNT_STAGE_DIR, i);
                            int sfd = open(src_path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0444);
                            if (sfd < 0) continue;

                            size_t written = 0;
                            while (written < raw_lens[i]) {
                                ssize_t n = write(sfd, raw_datas[i] + written, raw_lens[i] - written);
                                if (n <= 0) break;
                                written += static_cast<size_t>(n);
                            }
                            close(sfd);
                            chmod(src_path, 0444);
                            chown(src_path, 0, 0);

                            if (!ctx_mount_ok) {
                                if (lsetxattr(src_path, "security.selinux", kSysFileCtx, kCtxLen, 0) != 0) {
                                    unlink(src_path);
                                    continue;
                                }
                            }

                            if (mount(src_path, tgt, nullptr, MS_BIND, nullptr) == 0) {
                                mount(nullptr, tgt, nullptr, MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr);
                                child_res[i] = 1;
                            }
                        }
                    }

                    // 立刻 MNT_DETACH 拆卸中转目录，不留 /data/adb 挂载源痕迹
                    umount2(MNT_STAGE_DIR, MNT_DETACH);
                }
            }
        } else if (ns_fd >= 0) {
            close(ns_fd);
        }

        write_full(res_pipe[1], child_res, count);
        close(res_pipe[1]);
        _exit(0);
    }

    close(res_pipe[1]);
    if (worker > 0) {
        read_full(res_pipe[0], results.data(), count);
        int status = 0;
        waitpid(worker, &status, 0);
    }
    close(res_pipe[0]);

    return write_full(sock_fd, results.data(), results.size());
}

} // namespace mirage
