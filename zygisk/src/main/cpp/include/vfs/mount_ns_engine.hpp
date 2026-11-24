// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <utility>
#include <unordered_set>
#include <atomic>
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
// App 侧 (preAppSpecialize 阶段，处 u:r:zygote:s0)：
// 收集并按 realpath 去重硬件节点（消除同簇 policy 叠挂），在子进程 unshare(CLONE_NEWNS) 后同步挂载
// ============================================================================
inline uint32_t request_private_mount_ns_in_pre_specialize(int fd, std::string *out_diag) {
    auto send_skip = [&](const std::string &reason) -> uint32_t {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        if (out_diag) *out_diag = reason;
        return 0;
    };

    if (fd < 0 || !g_cfg.vfs.enabled || !g_cfg.vfs.enable_mount_ns) {
        return send_skip("disabled");
    }

    // 绝对 0 改动安全闸门 0：父进程必须是 Zygote (>1)，绝不允许在 Zygote 本体执行 unshare
    pid_t ppid = getppid();
    if (ppid <= 1) {
        return send_skip("skip=ppid_is_init");
    }

    std::vector<std::pair<std::string, std::string>> jobs;
    std::unordered_set<std::string> seen_real_paths;
    jobs.reserve(48);

    auto add_node = [&](const std::string &path) {
        // 目标节点明确不存在 (ENOENT) 时提前跳过，减少无效 IPC 传输
        if (access(path.c_str(), F_OK) != 0 && errno == ENOENT) {
            return;
        }
        // 通过 realpath 对软链接目标去重（例如同簇 cpu0..cpu5/cpufreq 都指向 policy0，只挂载 1 次）
        char resolved[PATH_MAX];
        std::string canonical = (realpath(path.c_str(), resolved) != nullptr) ? std::string(resolved) : path;
        if (!seen_real_paths.insert(canonical).second) {
            return;
        }

        std::string content;
        int dyn = -1;
        if (synthesize_vfs_node_on_demand(path, &content, &dyn) && !content.empty()) {
            jobs.emplace_back(std::move(canonical), std::move(content));
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
        // 关闭传统 PLT 引擎后，同时挂载 cur_freq 防止暴露真机当前频率
        add_node(pfx + "cpufreq/scaling_cur_freq");
        add_node(pfx + "cpufreq/cpuinfo_cur_freq");
        add_node(pfx + "cpufreq/cpuinfo_max_freq");
        add_node(pfx + "cpufreq/cpuinfo_min_freq");
        add_node(pfx + "cpufreq/scaling_max_freq");
        add_node(pfx + "cpufreq/scaling_min_freq");
        add_node(pfx + "cpufreq/scaling_governor");
        add_node(pfx + "cpufreq/scaling_driver");
    }

    if (jobs.empty()) {
        return send_skip("no_nodes");
    }

    if (unshare(CLONE_NEWNS) != 0) {
        return send_skip("unshare_failed(errno=" + std::to_string(errno) + ")");
    }

    uint8_t want_ns = 1;
    int32_t ppid_i32 = static_cast<int32_t>(ppid);
    uint32_t count = static_cast<uint32_t>(jobs.size());

    if (!write_full(fd, &want_ns, sizeof(want_ns)) ||
        !write_full(fd, &ppid_i32, sizeof(ppid_i32)) ||
        !write_full(fd, &count, sizeof(count))) {
        if (out_diag) *out_diag = "pre_ipc_write_hdr_failed(errno=" + std::to_string(errno) + ")";
        return 0;
    }

    for (const auto &job : jobs) {
        if (!write_str(fd, job.first) || !write_str(fd, job.second)) {
            if (out_diag) *out_diag = "pre_ipc_write_body_failed(errno=" + std::to_string(errno) + ")";
            return 0;
        }
    }

    std::vector<uint8_t> results(count, 0);
    if (!read_full(fd, results.data(), results.size())) {
        if (out_diag) *out_diag = "pre_ipc_read_res_failed(errno=" + std::to_string(errno) + ")";
        return 0;
    }
    std::string diag = read_str(fd);
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
// Root Companion 侧：节流清理已退出进程的遗留暂存目录（最多每 60 秒 1 次，不拖慢启动）
// ============================================================================
inline void cleanup_stale_mnt_stage_dirs_throttled() {
    static std::atomic<int64_t> s_last_clean_sec{0};
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t prev = s_last_clean_sec.load(std::memory_order_relaxed);
    if (now - prev < 60 || !s_last_clean_sec.compare_exchange_strong(prev, now, std::memory_order_acq_rel)) {
        return;
    }

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
// Root Companion 侧：在 companion_handler 中同步处理 Mount NS 请求
// 三级自适应策略：tmpfs-ctx -> tmpfs-xattr -> data-xattr
// ============================================================================
inline uint32_t companion_handle_mount_ns_sync(int fd, int32_t app_pid, std::string *out_diag) {
    uint8_t want_ns = 0;
    if (!read_full(fd, &want_ns, sizeof(want_ns)) || want_ns == 0) {
        return 0;
    }

    int32_t ppid = 0;
    uint32_t count = 0;
    if (!read_full(fd, &ppid, sizeof(ppid)) ||
        !read_full(fd, &count, sizeof(count))) {
        return 0;
    }

    if (count > 256) count = 256;
    std::vector<std::string> targets;
    std::vector<std::string> contents;
    targets.reserve(count);
    contents.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        targets.push_back(read_str(fd));
        contents.push_back(read_str(fd));
    }

    std::vector<uint8_t> results(count, 0);

    // 绝对 0 改动安全闸门 1：确认目标进程确实处于独立 Mount Namespace（与 Zygote / init 完全隔离）
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
        std::string diag = "skip=ns_not_isolated (fallback_legacy_plt)";
        if (out_diag) *out_diag = diag;
        write_full(fd, results.data(), results.size());
        write_str(fd, diag);
        return 0;
    }

    mkdir(DATA_ROOT_DIR, 0700);
    mkdir(MNT_STAGE_DIR, 0755);
    cleanup_stale_mnt_stage_dirs_throttled();

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
        std::string diag = "fail=pipe (fallback_legacy_plt)";
        if (out_diag) *out_diag = diag;
        write_full(fd, results.data(), results.size());
        write_str(fd, diag);
        return 0;
    }

    pid_t worker = fork();
    if (worker == 0) {
        close(res_pipe[0]);
        umask(0); // 配合 open(..., 0444) 一步到位，省掉循环内冗余 chmod/chown 调用
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

            // 绝对 0 改动安全闸门 2：先将整棵挂载树设为 MS_REC | MS_SLAVE，再将 /proc 与 /sys 设为 MS_REC | MS_PRIVATE
            bool root_slave = (mount(nullptr, "/", nullptr, MS_REC | MS_SLAVE, nullptr) == 0);
            bool proc_priv  = (mount(nullptr, "/proc", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0);
            bool sys_priv   = (mount(nullptr, "/sys", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0);

            if (!proc_priv && !sys_priv) {
                rep.fail_stage = 3;
                rep.last_errno = errno;
            } else {
                static constexpr const char *kSysFileCtx = "u:object_r:system_file:s0";
                const size_t kCtxLen = std::strlen(kSysFileCtx) + 1;

                mkdir(pid_stage_dir, 0755);
                bool need_umount_stage = false;

                if (root_slave) {
                    if (mount("tmpfs", pid_stage_dir, "tmpfs", MS_NOSUID | MS_NODEV,
                              "size=2M,mode=0755,context=u:object_r:system_file:s0") == 0) {
                        mount(nullptr, pid_stage_dir, nullptr, MS_PRIVATE, nullptr);
                        rep.mode = 1;
                        need_umount_stage = true;
                    } else if (mount("tmpfs", pid_stage_dir, "tmpfs", MS_NOSUID | MS_NODEV, "size=2M,mode=0755") == 0) {
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
                }

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
    char diag_buf[192];
    if (ok_cnt > 0) {
        std::snprintf(diag_buf, sizeof(diag_buf),
                      "mode=%s mounted=%u/%u (legacy_plt_vfs=OFF)", mode_name, ok_cnt, count);
    } else {
        std::snprintf(diag_buf, sizeof(diag_buf),
                      "fallback_legacy_plt stage=%u mode=%s errno=%d",
                      static_cast<unsigned>(rep.fail_stage), mode_name, rep.last_errno);
    }

    if (out_diag) *out_diag = diag_buf;
    write_full(fd, results.data(), results.size());
    write_str(fd, diag_buf);
    return ok_cnt;
}

} // namespace mirage
