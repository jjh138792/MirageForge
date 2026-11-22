#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <dirent.h>
#include <signal.h>
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

struct MountNsChildReport {
    uint8_t mode;       // 0=fail, 1=tmpfs-ctx, 2=tmpfs-xattr, 3=data-xattr
    uint8_t fail_stage; // 0=ok, 1=open_ns, 2=setns, 3=priv, 4=stage_label
    int32_t last_errno;
    uint8_t results[256];
};

// ============================================================================
// App 侧：收集静态硬件节点并请求 Root Companion 进行私有 Mount NS 挂载
// ============================================================================
inline uint32_t request_private_mount_ns_spoof(int ctrl_fd, std::string *out_diag) {
    if (ctrl_fd < 0 || !g_cfg.vfs.enabled || !g_cfg.vfs.enable_mount_ns) {
        if (out_diag) *out_diag = "disabled";
        return 0;
    }

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

    if (jobs.empty()) {
        if (out_diag) *out_diag = "no_nodes";
        return 0;
    }

    uint8_t cmd = CMD_MOUNT_NS;
    int32_t app_pid = static_cast<int32_t>(getpid());
    int32_t ppid = static_cast<int32_t>(getppid());
    uint32_t count = static_cast<uint32_t>(jobs.size());

    if (!write_full(ctrl_fd, &cmd, sizeof(cmd)) ||
        !write_full(ctrl_fd, &app_pid, sizeof(app_pid)) ||
        !write_full(ctrl_fd, &ppid, sizeof(ppid)) ||
        !write_full(ctrl_fd, &count, sizeof(count))) {
        if (out_diag) *out_diag = "ipc_write_failed";
        return 0;
    }

    for (const auto &job : jobs) {
        write_str(ctrl_fd, job.first);
        write_str(ctrl_fd, job.second);
    }

    std::vector<uint8_t> results(count, 0);
    if (!read_full(ctrl_fd, results.data(), results.size())) {
        if (out_diag) *out_diag = "ipc_read_failed";
        return 0;
    }
    std::string diag = read_str(ctrl_fd);
    if (out_diag) *out_diag = diag;

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
// Root Companion 侧：清理已退出进程的遗留暂存目录
// ============================================================================
inline void cleanup_stale_mnt_stage_dirs() {
    DIR *dir = opendir(MNT_STAGE_DIR);
    if (!dir) return;
    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
        pid_t old_pid = static_cast<pid_t>(std::atoi(ent->d_name));
        if (old_pid <= 0 || kill(old_pid, 0) == 0) continue;

        std::string sub = std::string(MNT_STAGE_DIR) + "/" + ent->d_name;
        DIR *sdir = opendir(sub.c_str());
        if (sdir) {
            struct dirent *fent = nullptr;
            while ((fent = readdir(sdir)) != nullptr) {
                if (fent->d_name[0] == '.') continue;
                unlink((sub + "/" + fent->d_name).c_str());
            }
            closedir(sdir);
        }
        rmdir(sub.c_str());
    }
    closedir(dir);
}

// ============================================================================
// Root Companion 侧：单线程 fork + setns 私有 Mount NS 内核级挂载实现
// 三级自适应策略：tmpfs-ctx -> tmpfs-xattr -> data-xattr (100% 兼容全版本 Android)
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

    // 绝对 0 改动安全闸门 1：确认目标进程处于独立 Mount Namespace（与 Zygote / init 隔离）
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
        write_full(sock_fd, results.data(), results.size());
        write_str(sock_fd, "skip=ns_not_isolated");
        return true;
    }

    mkdir(DATA_ROOT_DIR, 0700);
    mkdir(MNT_STAGE_DIR, 0755);
    cleanup_stale_mnt_stage_dirs();

    char pid_stage_dir[128];
    std::snprintf(pid_stage_dir, sizeof(pid_stage_dir), "%s/%d", MNT_STAGE_DIR, app_pid);

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
        write_full(sock_fd, results.data(), results.size());
        write_str(sock_fd, "fail=pipe");
        return true;
    }

    pid_t worker = fork();
    if (worker == 0) {
        close(res_pipe[0]);
        MountNsChildReport rep{};

        int ns_fd = open(app_ns_path, O_RDONLY | O_CLOEXEC);
        if (ns_fd < 0) {
            rep.fail_stage = 1;
            rep.last_errno = errno;
        } else if (syscall(__NR_setns, ns_fd, CLONE_NEWNS) != 0) {
            rep.fail_stage = 2;
            rep.last_errno = errno;
            close(ns_fd);
        } else {
            close(ns_fd);

            // 绝对 0 改动安全闸门 2：仅将 /proc 与 /sys 隔离为 MS_PRIVATE，杜绝挂载向外传播
            bool proc_priv = (mount(nullptr, "/proc", nullptr, MS_PRIVATE, nullptr) == 0);
            bool sys_priv  = (mount(nullptr, "/sys", nullptr, MS_PRIVATE, nullptr) == 0);
            if (!proc_priv && !sys_priv) {
                rep.fail_stage = 3;
                rep.last_errno = errno;
            } else {
                static constexpr const char *kSysFileCtx = "u:object_r:system_file:s0";
                const size_t kCtxLen = std::strlen(kSysFileCtx) + 1;

                mkdir(pid_stage_dir, 0755);
                bool need_umount_stage = false;

                // Tier 1: tmpfs + context=u:object_r:system_file:s0（纯内存、0 磁盘路径）
                if (mount("tmpfs", pid_stage_dir, "tmpfs", MS_NOSUID | MS_NODEV,
                          "size=2M,mode=0755,context=u:object_r:system_file:s0") == 0) {
                    mount(nullptr, pid_stage_dir, nullptr, MS_PRIVATE, nullptr);
                    rep.mode = 1;
                    need_umount_stage = true;
                } else {
                    // Tier 2: 普通 tmpfs + 验证 lsetxattr
                    if (mount("tmpfs", pid_stage_dir, "tmpfs", MS_NOSUID | MS_NODEV, "size=2M,mode=0755") == 0) {
                        mount(nullptr, pid_stage_dir, nullptr, MS_PRIVATE, nullptr);
                        char probe_f[160];
                        std::snprintf(probe_f, sizeof(probe_f), "%s/.p", pid_stage_dir);
                        int pfd = open(probe_f, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0444);
                        if (pfd >= 0) {
                            close(pfd);
                            if (lsetxattr(probe_f, "security.selinux", kSysFileCtx, kCtxLen, 0) == 0) {
                                rep.mode = 2;
                                need_umount_stage = true;
                            }
                            unlink(probe_f);
                        }
                        if (rep.mode == 0) {
                            umount2(pid_stage_dir, MNT_DETACH);
                        }
                    }

                    // Tier 3: /data (ext4/f2fs) 原生 fs_use_xattr + 绑定后立即 unlink（全内核 100% 兼容）
                    if (rep.mode == 0) {
                        char probe_f[160];
                        std::snprintf(probe_f, sizeof(probe_f), "%s/.p", pid_stage_dir);
                        int pfd = open(probe_f, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0444);
                        if (pfd >= 0) {
                            close(pfd);
                            if (lsetxattr(probe_f, "security.selinux", kSysFileCtx, kCtxLen, 0) == 0) {
                                rep.mode = 3;
                            } else {
                                rep.fail_stage = 4;
                                rep.last_errno = errno;
                            }
                            unlink(probe_f);
                        } else {
                            rep.fail_stage = 4;
                            rep.last_errno = errno;
                        }
                    }
                }

                if (rep.mode != 0) {
                    for (uint32_t i = 0; i < count; ++i) {
                        const char *tgt = raw_targets[i];
                        if (!tgt || tgt[0] != '/') continue;
                        bool is_proc = (std::strncmp(tgt, "/proc/", 6) == 0);
                        bool is_sys  = (std::strncmp(tgt, "/sys/", 5) == 0);
                        if ((is_proc && !proc_priv) || (is_sys && !sys_priv) || (!is_proc && !is_sys)) {
                            continue;
                        }
                        if (access(tgt, F_OK) != 0) continue;

                        char src_path[160];
                        std::snprintf(src_path, sizeof(src_path), "%s/n%u", pid_stage_dir, i);
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

                        if (rep.mode != 1) {
                            if (lsetxattr(src_path, "security.selinux", kSysFileCtx, kCtxLen, 0) != 0) {
                                unlink(src_path);
                                continue;
                            }
                        }

                        if (mount(src_path, tgt, nullptr, MS_BIND, nullptr) == 0) {
                            mount(nullptr, tgt, nullptr, MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr);
                            rep.results[i] = 1;
                        }

                        // Tier 3 绑定后立即 unlink，磁盘 0 残留且内核保持 inode 存活直至 App 退出
                        if (rep.mode == 3) {
                            unlink(src_path);
                        }
                    }

                    if (need_umount_stage) {
                        umount2(pid_stage_dir, MNT_DETACH);
                    }
                    rmdir(pid_stage_dir);
                } else {
                    rmdir(pid_stage_dir);
                }
            }
        }

        write_full(res_pipe[1], &rep, sizeof(rep));
        close(res_pipe[1]);
        _exit(0);
    }

    close(res_pipe[1]);
    MountNsChildReport rep{};
    if (worker > 0) {
        read_full(res_pipe[0], &rep, sizeof(rep));
        int status = 0;
        waitpid(worker, &status, 0);
    }
    close(res_pipe[0]);

    uint32_t ok_cnt = 0;
    for (uint32_t i = 0; i < count; ++i) {
        results[i] = rep.results[i];
        if (results[i] == 1) ++ok_cnt;
    }

    const char *mode_name = (rep.mode == 1) ? "tmpfs-ctx" :
                            (rep.mode == 2) ? "tmpfs-xattr" :
                            (rep.mode == 3) ? "data-xattr" : "none";
    char diag_buf[160];
    if (ok_cnt > 0) {
        std::snprintf(diag_buf, sizeof(diag_buf), "mode=%s mounted=%u/%u (0 memfd)", mode_name, ok_cnt, count);
    } else {
        std::snprintf(diag_buf, sizeof(diag_buf), "fallback_plt stage=%u mode=%s errno=%d",
                      static_cast<unsigned>(rep.fail_stage), mode_name, rep.last_errno);
    }

    write_full(sock_fd, results.data(), results.size());
    write_str(sock_fd, diag_buf);
    return true;
}

} // namespace mirage
