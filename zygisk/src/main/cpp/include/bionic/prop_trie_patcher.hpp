// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/system_properties.h>

namespace mirage {

// ============================================================================
// AOSP Bionic prop_area / prop_bt / prop_info 二进制布局（Android 8 ~ 16 通用）
// ============================================================================
struct PropAreaHeaderRaw {
    uint32_t bytes_used;
    uint32_t serial;
    uint32_t magic;       // 0x504f5250 ("PROP")
    uint32_t version;     // 0xfc6ed0ab
    uint32_t reserved[28];
};
static_assert(sizeof(PropAreaHeaderRaw) == 128, "PropAreaHeaderRaw must be 128 bytes");

struct PropBtRaw {
    uint32_t namelen;
    uint32_t prop;        // 相对 data_ 的字节偏移，指向 PropInfoRaw
    uint32_t left;        // 相对 data_ 的字节偏移，指向左兄弟 PropBtRaw
    uint32_t right;       // 相对 data_ 的字节偏移，指向右兄弟 PropBtRaw
    uint32_t children;    // 相对 data_ 的字节偏移，指向子段 PropBtRaw 树根
    char name[1];         // 变长空终止段名（不含 '.'）
};
static_assert(offsetof(PropBtRaw, name) == 20, "PropBtRaw::name offset must be 20");

struct PropInfoRaw {
    uint32_t serial;
    union {
        char value[PROP_VALUE_MAX]; // PROP_VALUE_MAX == 92
        struct {
            char error_message[56];
            uint32_t offset;        // 相对本 PropInfoRaw 起始地址的长值字符串偏移
        } long_property;
    };
    char name[1];                   // 变长空终止完整属性名
};
static_assert(offsetof(PropInfoRaw, value) == 4, "PropInfoRaw::value offset mismatch");
static_assert(offsetof(PropInfoRaw, name) == 96, "PropInfoRaw::name offset mismatch");

constexpr uint32_t kPropAreaMagic   = 0x504f5250u;
constexpr uint32_t kPropAreaVersion = 0xfc6ed0abu;
constexpr uint32_t kPropLongFlag    = 1u << 16;
constexpr size_t   kPropBtHdrSize   = offsetof(PropBtRaw, name);
constexpr size_t   kPropInfoHdrSize = offsetof(PropInfoRaw, name);
constexpr const char *kLongPropErrMsg = "Must use __system_property_read_callback() to read";

inline size_t align4_size(size_t n) noexcept {
    return (n + 3u) & ~static_cast<size_t>(3u);
}

inline int cmp_prop_bt_segment(const char *a, uint32_t a_len, const char *b, uint32_t b_len) noexcept {
    if (a_len < b_len) return -1;
    if (a_len > b_len) return 1;
    return std::memcmp(a, b, a_len);
}

inline bool is_valid_prop_area_header(const uint8_t *area_base, size_t area_len) noexcept {
    if (!area_base || area_len <= sizeof(PropAreaHeaderRaw) + kPropBtHdrSize) return false;
    const auto *hdr = reinterpret_cast<const PropAreaHeaderRaw *>(area_base);
    if (hdr->magic != kPropAreaMagic || hdr->version != kPropAreaVersion) return false;
    size_t data_cap = area_len - sizeof(PropAreaHeaderRaw);
    return hdr->bytes_used >= kPropBtHdrSize && hdr->bytes_used <= data_cap;
}

inline bool alloc_in_prop_area(
    PropAreaHeaderRaw *hdr,
    uint8_t *data,
    size_t data_cap,
    size_t raw_size,
    uint32_t *out_off) noexcept {
    if (!hdr || !data || !out_off || raw_size == 0) return false;
    size_t aligned = align4_size(raw_size);
    size_t cur = hdr->bytes_used;
    if (aligned > data_cap || cur > data_cap - aligned) return false;
    std::memset(data + cur, 0, aligned);
    *out_off = static_cast<uint32_t>(cur);
    hdr->bytes_used = static_cast<uint32_t>(cur + aligned);
    return true;
}

inline bool write_prop_info_value(
    PropAreaHeaderRaw *hdr,
    uint8_t *data,
    size_t data_cap,
    uint32_t pi_off,
    const std::string &val,
    bool allow_alloc_long) noexcept {
    if (pi_off > data_cap || data_cap - pi_off < kPropInfoHdrSize) return false;
    auto *pi = reinterpret_cast<PropInfoRaw *>(data + pi_off);
    uint32_t old_serial = __atomic_load_n(&pi->serial, __ATOMIC_RELAXED);

    if (val.size() < PROP_VALUE_MAX) {
        std::memset(pi->value, 0, PROP_VALUE_MAX);
        if (!val.empty()) {
            std::memcpy(pi->value, val.data(), val.size());
        }
        uint32_t counter = ((old_serial & 0xFFFFu) + 2u) & 0xFFFEu;
        uint32_t new_serial = (static_cast<uint32_t>(val.size()) << 24) | counter;
        __atomic_store_n(&pi->serial, new_serial, __ATOMIC_RELEASE);
        return true;
    }

    // 超长属性 (>= 92 字节，如超长指纹)
    if ((old_serial & kPropLongFlag) != 0) {
        uint32_t loff = pi->long_property.offset;
        if (loff >= kPropInfoHdrSize && pi_off + loff < data_cap) {
            char *dst = reinterpret_cast<char *>(data + pi_off + loff);
            size_t avail = data_cap - (pi_off + loff);
            size_t old_len = strnlen(dst, avail);
            if (val.size() <= old_len) {
                std::memcpy(dst, val.data(), val.size());
                dst[val.size()] = '\0';
                uint32_t counter = ((old_serial & 0xFFFFu) + 2u) & 0xFFFEu;
                __atomic_store_n(&pi->serial, counter | kPropLongFlag, __ATOMIC_RELEASE);
                return true;
            }
        }
    }

    if (!allow_alloc_long) return false;

    uint32_t long_off = 0;
    if (!alloc_in_prop_area(hdr, data, data_cap, val.size() + 1, &long_off)) {
        return false;
    }
    if (long_off <= pi_off) return false;

    std::memcpy(data + long_off, val.data(), val.size());
    data[long_off + val.size()] = '\0';

    std::memset(pi->long_property.error_message, 0, sizeof(pi->long_property.error_message));
    std::strncpy(pi->long_property.error_message, kLongPropErrMsg, sizeof(pi->long_property.error_message) - 1);
    pi->long_property.offset = long_off - pi_off;

    uint32_t counter = ((old_serial & 0xFFFFu) + 2u) & 0xFFFEu;
    __atomic_store_n(&pi->serial, counter | kPropLongFlag, __ATOMIC_RELEASE);
    return true;
}

struct PropAreaPatchResult {
    uint32_t patched_existing = 0;
    uint32_t inserted_new     = 0;
    uint32_t skipped          = 0;
};

inline PropAreaPatchResult patch_prop_area_buffer(
    uint8_t *area_base,
    size_t area_len,
    const std::vector<std::pair<std::string, std::string>> &kvs,
    bool allow_insert,
    std::vector<std::string> *out_touched_keys = nullptr) {
    PropAreaPatchResult res{};
    if (!is_valid_prop_area_header(area_base, area_len) || kvs.empty()) {
        res.skipped = static_cast<uint32_t>(kvs.size());
        return res;
    }

    auto *hdr = reinterpret_cast<PropAreaHeaderRaw *>(area_base);
    uint8_t *data = area_base + sizeof(PropAreaHeaderRaw);
    const size_t data_cap = area_len - sizeof(PropAreaHeaderRaw);

    auto get_bt = [&](uint32_t off) -> PropBtRaw * {
        if (off >= hdr->bytes_used || hdr->bytes_used - off < kPropBtHdrSize) return nullptr;
        auto *bt = reinterpret_cast<PropBtRaw *>(data + off);
        if (off == 0) {
            // AOSP 空属性区根节点位于 data + 0 且 bytes_used 初始为 20（无 name 字节）
            return (bt->namelen == 0) ? bt : nullptr;
        }
        if (bt->namelen > 255 || off + kPropBtHdrSize + bt->namelen + 1 > hdr->bytes_used) return nullptr;
        return bt;
    };

    auto alloc_bt = [&](const char *seg, uint32_t seg_len, uint32_t *out_off) -> PropBtRaw * {
        uint32_t off = 0;
        if (!alloc_in_prop_area(hdr, data, data_cap, kPropBtHdrSize + seg_len + 1, &off)) {
            return nullptr;
        }
        auto *bt = reinterpret_cast<PropBtRaw *>(data + off);
        bt->namelen = seg_len;
        std::memcpy(bt->name, seg, seg_len);
        bt->name[seg_len] = '\0';
        *out_off = off;
        return bt;
    };

    for (const auto &kv : kvs) {
        const std::string &key = kv.first;
        const std::string &val = kv.second;
        if (key.empty() || key.size() > 255) {
            ++res.skipped;
            continue;
        }

        uint32_t cur_off = 0;
        const char *rem = key.c_str();
        bool walk_ok = true;

        while (true) {
            const char *dot = std::strchr(rem, '.');
            uint32_t seg_len = dot ? static_cast<uint32_t>(dot - rem)
                                   : static_cast<uint32_t>(std::strlen(rem));
            if (seg_len == 0 || seg_len > 255) {
                walk_ok = false;
                break;
            }

            PropBtRaw *parent = get_bt(cur_off);
            if (!parent) {
                walk_ok = false;
                break;
            }

            uint32_t child_root = __atomic_load_n(&parent->children, __ATOMIC_ACQUIRE);
            if (child_root == 0) {
                if (!allow_insert) {
                    walk_ok = false;
                    break;
                }
                uint32_t new_off = 0;
                if (!alloc_bt(rem, seg_len, &new_off)) {
                    walk_ok = false;
                    break;
                }
                __atomic_store_n(&parent->children, new_off, __ATOMIC_RELEASE);
                cur_off = new_off;
            } else {
                uint32_t node_off = child_root;
                bool matched = false;
                for (int guard = 0; guard < 128; ++guard) {
                    PropBtRaw *node = get_bt(node_off);
                    if (!node) break;

                    int cmp = cmp_prop_bt_segment(rem, seg_len, node->name, node->namelen);
                    if (cmp == 0) {
                        cur_off = node_off;
                        matched = true;
                        break;
                    }
                    uint32_t *branch = (cmp < 0) ? &node->left : &node->right;
                    uint32_t next_off = __atomic_load_n(branch, __ATOMIC_ACQUIRE);
                    if (next_off != 0) {
                        node_off = next_off;
                    } else {
                        if (!allow_insert) break;
                        uint32_t new_off = 0;
                        if (!alloc_bt(rem, seg_len, &new_off)) break;
                        __atomic_store_n(branch, new_off, __ATOMIC_RELEASE);
                        cur_off = new_off;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    walk_ok = false;
                    break;
                }
            }

            if (!dot) break;
            rem = dot + 1;
        }

        if (!walk_ok) {
            ++res.skipped;
            continue;
        }

        PropBtRaw *leaf = get_bt(cur_off);
        if (!leaf) {
            ++res.skipped;
            continue;
        }

        uint32_t pi_off = __atomic_load_n(&leaf->prop, __ATOMIC_ACQUIRE);
        if (pi_off != 0) {
            if (pi_off >= hdr->bytes_used ||
                hdr->bytes_used - pi_off < kPropInfoHdrSize + key.size() + 1) {
                ++res.skipped;
                continue;
            }
            auto *pi = reinterpret_cast<PropInfoRaw *>(data + pi_off);
            if (std::memcmp(pi->name, key.data(), key.size() + 1) != 0) {
                ++res.skipped;
                continue;
            }
            if (write_prop_info_value(hdr, data, data_cap, pi_off, val, allow_insert)) {
                ++res.patched_existing;
                if (out_touched_keys) out_touched_keys->push_back(key);
            } else {
                ++res.skipped;
            }
        } else {
            if (!allow_insert) {
                ++res.skipped;
                continue;
            }
            uint32_t new_pi_off = 0;
            if (!alloc_in_prop_area(hdr, data, data_cap, kPropInfoHdrSize + key.size() + 1, &new_pi_off)) {
                ++res.skipped;
                continue;
            }
            auto *new_pi = reinterpret_cast<PropInfoRaw *>(data + new_pi_off);
            std::memcpy(new_pi->name, key.data(), key.size());
            new_pi->name[key.size()] = '\0';

            if (write_prop_info_value(hdr, data, data_cap, new_pi_off, val, true)) {
                __atomic_store_n(&leaf->prop, new_pi_off, __ATOMIC_RELEASE);
                ++res.inserted_new;
                if (out_touched_keys) out_touched_keys->push_back(key);
            } else {
                ++res.skipped;
            }
        }
    }

    if (res.patched_existing > 0 || res.inserted_new > 0) {
        __atomic_fetch_add(&hdr->serial, 2u, __ATOMIC_RELEASE);
    }
    return res;
}

// ============================================================================
// Android 9+ /dev/__properties__/property_info 二进制上下文解析器（带运行时自检）
// 严格按 AOSP property_info_parser.h 格式解引用 uint32_t 偏移表
// ============================================================================
class PropertyInfoContextResolver {
public:
    bool load_and_verify() {
        loaded_ = false;
        buf_.clear();

        static const char *const kInfoPaths[] = {
            "/dev/__properties__/property_info",
            "/__properties__/property_info",
        };
        int fd = -1;
        for (const char *p : kInfoPaths) {
            fd = open(p, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) break;
        }
        if (fd < 0) return false;

        struct stat st{};
        if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(AreaHdr)) || st.st_size > 2 * 1024 * 1024) {
            close(fd);
            return false;
        }
        buf_.resize(static_cast<size_t>(st.st_size));
        size_t got = 0;
        while (got < buf_.size()) {
            ssize_t n = read(fd, buf_.data() + got, buf_.size() - got);
            if (n <= 0) break;
            got += static_cast<size_t>(n);
        }
        close(fd);
        if (got != buf_.size()) {
            buf_.clear();
            return false;
        }

        const auto *hdr = reinterpret_cast<const AreaHdr *>(buf_.data());
        if (hdr->current_version != 1 || hdr->minimum_supported_version != 1 ||
            hdr->size > buf_.size() || hdr->contexts_offset + 4 > hdr->size ||
            hdr->root_offset + sizeof(TrieNodeRaw) > hdr->size) {
            buf_.clear();
            return false;
        }

        loaded_ = true;

        std::string probe_ctx = resolve_context("ro.product.model");
        if (probe_ctx.rfind("u:object_r:", 0) != 0) {
            loaded_ = false;
            buf_.clear();
            return false;
        }
        return true;
    }

    bool is_loaded() const noexcept { return loaded_; }

    std::string resolve_context(const std::string &prop_name) const {
        if (!loaded_ || prop_name.empty()) return "";
        const auto *hdr = reinterpret_cast<const AreaHdr *>(buf_.data());
        const size_t limit = hdr->size;

        auto get_node = [&](uint32_t off) -> const TrieNodeRaw * {
            if (off > limit || limit - off < sizeof(TrieNodeRaw)) return nullptr;
            return reinterpret_cast<const TrieNodeRaw *>(buf_.data() + off);
        };
        auto get_entry = [&](uint32_t off) -> const PropEntryRaw * {
            if (off > limit || limit - off < sizeof(PropEntryRaw)) return nullptr;
            return reinterpret_cast<const PropEntryRaw *>(buf_.data() + off);
        };
        auto get_cstr = [&](uint32_t off, uint32_t *out_max = nullptr) -> const char * {
            if (off >= limit) return nullptr;
            if (out_max) *out_max = static_cast<uint32_t>(limit - off);
            return reinterpret_cast<const char *>(buf_.data() + off);
        };

        const TrieNodeRaw *cur = get_node(hdr->root_offset);
        if (!cur) return "";

        uint32_t best_ctx = ~0u;
        if (const auto *root_pe = get_entry(cur->property_entry)) {
            if (root_pe->context_index != ~0u) best_ctx = root_pe->context_index;
        }

        const char *rem = prop_name.c_str();
        for (int depth = 0; depth < 64 && cur != nullptr; ++depth) {
            size_t rem_len = std::strlen(rem);

            // 1. 前缀规则匹配（prefix_entries 为 uint32_t 偏移数组，每项指向一个 PropEntryRaw）
            if (cur->num_prefixes > 0 && cur->prefix_entries < limit) {
                size_t max_entries = (limit - cur->prefix_entries) / sizeof(uint32_t);
                uint32_t cnt = cur->num_prefixes < max_entries ? cur->num_prefixes : static_cast<uint32_t>(max_entries);
                const auto *offs = reinterpret_cast<const uint32_t *>(buf_.data() + cur->prefix_entries);
                uint32_t best_prefix_len = 0;
                for (uint32_t i = 0; i < cnt; ++i) {
                    const auto *pe = get_entry(offs[i]);
                    if (!pe) continue;
                    uint32_t avail = 0;
                    const char *pname = get_cstr(pe->name_offset, &avail);
                    if (pname && pe->namelen > 0 && pe->namelen <= avail &&
                        rem_len >= pe->namelen && pe->namelen >= best_prefix_len &&
                        std::memcmp(pname, rem, pe->namelen) == 0) {
                        if (pe->context_index != ~0u) {
                            best_ctx = pe->context_index;
                            best_prefix_len = pe->namelen;
                        }
                    }
                }
            }

            const char *dot = std::strchr(rem, '.');
            if (!dot) {
                // 2. 最后一段：优先精确匹配（exact_match_entries 同样为 uint32_t 偏移数组）
                if (cur->num_exact_matches > 0 && cur->exact_match_entries < limit) {
                    size_t max_entries = (limit - cur->exact_match_entries) / sizeof(uint32_t);
                    uint32_t cnt = cur->num_exact_matches < max_entries ? cur->num_exact_matches : static_cast<uint32_t>(max_entries);
                    const auto *offs = reinterpret_cast<const uint32_t *>(buf_.data() + cur->exact_match_entries);
                    for (uint32_t i = 0; i < cnt; ++i) {
                        const auto *pe = get_entry(offs[i]);
                        if (!pe) continue;
                        uint32_t avail = 0;
                        const char *ename = get_cstr(pe->name_offset, &avail);
                        if (ename && pe->namelen == rem_len && pe->namelen <= avail &&
                            std::memcmp(ename, rem, rem_len) == 0) {
                            if (pe->context_index != ~0u) {
                                return context_by_index(pe->context_index);
                            }
                        }
                    }
                }
                const TrieNodeRaw *leaf_child = find_child(cur, rem, rem_len, limit);
                if (leaf_child) {
                    if (const auto *pe = get_entry(leaf_child->property_entry)) {
                        if (pe->context_index != ~0u) best_ctx = pe->context_index;
                    }
                }
                break;
            }

            size_t seg_len = static_cast<size_t>(dot - rem);
            const TrieNodeRaw *next_node = find_child(cur, rem, seg_len, limit);
            if (!next_node) break;
            cur = next_node;
            if (const auto *pe = get_entry(cur->property_entry)) {
                if (pe->context_index != ~0u) best_ctx = pe->context_index;
            }
            rem = dot + 1;
        }

        return (best_ctx != ~0u) ? context_by_index(best_ctx) : "";
    }

private:
    struct AreaHdr {
        uint32_t current_version;
        uint32_t minimum_supported_version;
        uint32_t size;
        uint32_t contexts_offset;
        uint32_t types_offset;
        uint32_t root_offset;
    };

    struct PropEntryRaw {
        uint32_t name_offset;
        uint32_t namelen;
        uint32_t context_index;
        uint32_t type_index;
    };

    struct TrieNodeRaw {
        uint32_t property_entry;
        uint32_t num_child_nodes;
        uint32_t child_nodes;
        uint32_t num_prefixes;
        uint32_t prefix_entries;
        uint32_t num_exact_matches;
        uint32_t exact_match_entries;
    };

    const TrieNodeRaw *find_child(const TrieNodeRaw *node, const char *seg, size_t seg_len, size_t limit) const {
        if (!node || node->num_child_nodes == 0 || node->child_nodes >= limit) return nullptr;
        size_t max_children = (limit - node->child_nodes) / sizeof(uint32_t);
        uint32_t cnt = node->num_child_nodes < max_children ? node->num_child_nodes : static_cast<uint32_t>(max_children);
        const auto *offs = reinterpret_cast<const uint32_t *>(buf_.data() + node->child_nodes);

        for (uint32_t i = 0; i < cnt; ++i) {
            uint32_t co = offs[i];
            if (co > limit || limit - co < sizeof(TrieNodeRaw)) continue;
            const auto *cn = reinterpret_cast<const TrieNodeRaw *>(buf_.data() + co);
            if (cn->property_entry > limit || limit - cn->property_entry < sizeof(PropEntryRaw)) continue;
            const auto *pe = reinterpret_cast<const PropEntryRaw *>(buf_.data() + cn->property_entry);
            if (pe->name_offset >= limit || pe->namelen != seg_len || limit - pe->name_offset < seg_len) continue;
            const char *cname = reinterpret_cast<const char *>(buf_.data() + pe->name_offset);
            if (std::memcmp(cname, seg, seg_len) == 0) {
                return cn;
            }
        }
        return nullptr;
    }

    std::string context_by_index(uint32_t idx) const {
        const auto *hdr = reinterpret_cast<const AreaHdr *>(buf_.data());
        const size_t limit = hdr->size;
        if (hdr->contexts_offset + 4 > limit) return "";
        const auto *arr = reinterpret_cast<const uint32_t *>(buf_.data() + hdr->contexts_offset);
        uint32_t num = arr[0];
        if (idx >= num || hdr->contexts_offset + (static_cast<size_t>(idx) + 2) * sizeof(uint32_t) > limit) {
            return "";
        }
        uint32_t str_off = arr[idx + 1];
        if (str_off >= limit) return "";
        const char *s = reinterpret_cast<const char *>(buf_.data() + str_off);
        size_t len = strnlen(s, limit - str_off);
        return std::string(s, len);
    }

    bool loaded_ = false;
    std::vector<uint8_t> buf_;
};

inline std::string prop_path_basename(const std::string &path) {
    size_t pos = path.rfind('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

inline int open_prop_backing_file_ro(const std::string &vma_path) noexcept {
    int fd = open(vma_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) return fd;
    if (vma_path.rfind("/__properties__/", 0) == 0) {
        std::string dev_path = "/dev" + vma_path;
        fd = open(dev_path.c_str(), O_RDONLY | O_CLOEXEC);
    }
    return fd;
}

} // namespace mirage
