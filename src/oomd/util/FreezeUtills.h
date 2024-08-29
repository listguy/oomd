#ifndef FREEZE_UTILL_H
#define FREEZE_UTILL_H



#include <string>

namespace Oomd {
#define MY_FREEZER_PATH "/sys/fs/cgroup/freezer/my_freezer"
#define FREEZER_PATH "/sys/fs/cgroup/freezer"
#define SYSCALL_FAILED -1
#define CGROUP_PATH_PREFIX "/sys/fs/cgroup/"
// Method to write a string value to a file
void writeToFile(const std::string& path, const std::string& value);

// Function to log errors
void logError(const std::string& message);
}

#endif // FREEZE_UTILL_H