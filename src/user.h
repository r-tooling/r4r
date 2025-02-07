#ifndef USER_H
#define USER_H

#include <grp.h>
#include <pwd.h>
#include <stdexcept>
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

    static UserInfo get_current_user_info() {
        uid_t uid = getuid();
        gid_t gid = getgid();

        passwd* pwd = getpwuid(uid);
        if (pwd == nullptr) {
            throw std::runtime_error("Failed to get passwd struct for UID " +
                                     std::to_string(uid));
        }

        std::string username = pwd->pw_name;
        std::string home_directory = pwd->pw_dir;
        std::string shell = pwd->pw_shell;

        // primary group information
        ::group* grp = getgrgid(gid);
        if (grp == nullptr) {
            throw std::runtime_error("Failed to get group struct for GID " +
                                     std::to_string(gid));
        }
        GroupInfo primary_group = {gid, grp->gr_name};

        // groups
        int n_groups = 0;
        getgrouplist(username.c_str(), gid, nullptr,
                     &n_groups); // Get number of groups

        std::vector<gid_t> group_ids(n_groups);
        if (getgrouplist(username.c_str(), gid, group_ids.data(), &n_groups) ==
            -1) {
            throw std::runtime_error("Failed to get group list for user " +
                                     username);
        }

        // get gids
        std::vector<GroupInfo> groups;
        for (gid_t group_id : group_ids) {
            ::group* g = getgrgid(group_id);
            if (g != nullptr) {
                groups.push_back({group_id, g->gr_name});
            }
        }

        return {uid, primary_group, username, home_directory, shell, groups};
    }
};

#endif // USER_H
