#ifndef USER_H
#define USER_H

#include "common.h"
#include <grp.h>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>

struct GroupInfo {
    gid_t gid;
    std::string name;
};

struct UserInfo {
    uid_t uid;
    GroupInfo group;
    std::string username;
    std::string home_directory;
    std::string shell;
    std::vector<GroupInfo> groups;

    static UserInfo get_current_user_info();
};

inline UserInfo UserInfo::get_current_user_info() {
    uid_t uid = getuid();
    gid_t gid = getgid();

    long pw_buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (pw_buf_size == -1) {
        pw_buf_size = 1024; // fallback
    }
    long gr_buf_size = sysconf(_SC_GETGR_R_SIZE_MAX);
    if (gr_buf_size == -1) {
        gr_buf_size = 1024; // fallback
    }

    std::vector<char> pw_buffer(static_cast<size_t>(pw_buf_size));
    struct passwd pwd{};
    struct passwd* pwd_result = nullptr;

    int pw_status =
        getpwuid_r(uid, &pwd, pw_buffer.data(), pw_buffer.size(), &pwd_result);
    if (pw_status != 0 || pwd_result == nullptr) {
        throw make_system_error(
            errno, STR("Failed to get passwd struct for UID " << uid));
    }

    std::string username = pwd.pw_name;
    std::string home_directory = pwd.pw_dir;
    std::string shell = pwd.pw_shell;

    std::vector<char> gr_buffer(static_cast<size_t>(gr_buf_size));
    struct group grp{};
    struct group* grp_result = nullptr;

    int gr_status =
        getgrgid_r(gid, &grp, gr_buffer.data(), gr_buffer.size(), &grp_result);
    if (gr_status != 0 || grp_result == nullptr) {
        throw make_system_error(
            errno, STR("Failed to get group struct for GID " << gid));
    }

    GroupInfo primary_group = {.gid = gid, .name = grp.gr_name};

    int n_groups = 0;
    if (getgrouplist(username.c_str(), gid, nullptr, &n_groups) == -1 &&
        n_groups == 0) {
        throw make_system_error(
            errno, "Failed to determine group list size for user " + username);
    }

    std::vector<gid_t> group_ids(n_groups);
    if (getgrouplist(username.c_str(), gid, group_ids.data(), &n_groups) ==
        -1) {
        throw make_system_error(errno, "Failed to get group list for user " +
                                           username);
    }

    std::vector<GroupInfo> groups;
    groups.reserve(static_cast<size_t>(n_groups));

    for (gid_t group_id : group_ids) {
        struct group temp_grp{};
        struct group* temp_result = nullptr;

        gr_status = getgrgid_r(group_id, &temp_grp, gr_buffer.data(),
                               gr_buffer.size(), &temp_result);
        if (gr_status == 0 && temp_result != nullptr) {
            std::string grp_name = (temp_grp.gr_name ? temp_grp.gr_name : "");
            groups.push_back({.gid = group_id, .name = grp_name});
        } else {
            throw make_system_error(
                errno, STR("Failed to get group info for GID: " << group_id));
        }
    }

    return UserInfo{.uid = uid,
                    .group = primary_group,
                    .username = username,
                    .home_directory = home_directory,
                    .shell = shell,
                    .groups = groups};
}

static std::optional<std::string> get_system_timezone() {
    // 1. try TZ environment
    if (char const* tz_env = std::getenv("TZ")) {
        return {tz_env};
    }

    // 2. try reading from /etc/timezone
    if (std::ifstream tz_file("/etc/timezone"); tz_file) {
        std::string timezone;
        std::getline(tz_file, timezone);
        return timezone;
    }

    // 3. timedatectl
    auto out = Command("timedatectl")
                   .arg("show")
                   .arg("--property=Timezone")
                   .arg("--value")
                   .output();

    if (out.exit_code == 0) {
        return out.stdout_data;
    }

    return {};
}

#endif // USER_H
