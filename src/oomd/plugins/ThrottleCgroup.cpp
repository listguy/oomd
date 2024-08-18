#include "oomd/plugins/ThrottleCgroup.h"
#include "oomd/Log.h"
#include "oomd/PluginRegistry.h"
#include "oomd/include/Types.h"
#include "oomd/util/FreezeUtills.h"
#include "oomd/util/Fs.h"
#include "oomd/util/Util.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cerrno>
#include "../../../usr/include/x86_64-linux-gnu/sys/stat.h"

namespace Oomd {

REGISTER_PLUGIN(throttle, ThrottleCgroup::create);

int ThrottleCgroup::init(
    const Engine::PluginArgs& args,
    const PluginConstructionContext& context) {
  argParser_.addArgumentCustom(
      "monitor_cgroup",
      monitor_cgroup_path_,
      [](const std::string& cgroup) { return CGROUP_PATH_PREFIX + cgroup; },
      true);

  argParser_.addArgumentCustom(
      "throttle_cgroup",
      throttle_cgroup_path_,
      [](const std::string& cgroup) { return CGROUP_PATH_PREFIX + cgroup; },
      true);

  if (!argParser_.parse(args)) {
    return 1;
  }
  return 0;
}

void ThrottleCgroup::increaseLevel() {
  changeLevel(+1);
}
void ThrottleCgroup::decreaseLevel() {
  changeLevel(-1);
}

void ThrottleCgroup::changeLevel(int change) {
  level = std::min(MAX_LEVEL, std::max(level + change, 0));
  const std::string quotaAndPeriod = getCurrentQuotaAndPeriod();
  throttle(throttle_cgroup_path_, quotaAndPeriod);
}

std::string ThrottleCgroup::getCurrentQuotaAndPeriod() {
  float relativeQuota;

  switch (level) {
    case 1:
      relativeQuota = LEVEL_1;
      break;
    case 2:
      relativeQuota = LEVEL_2;
      break;
    case 3:
      relativeQuota = LEVEL_3;
      break;
    case MAX_LEVEL:
      relativeQuota = LEVEL_4;
      break;
    default:
      relativeQuota = 1;
      break;
  }

  std::ostringstream valuesAsString;
  valuesAsString << relativeQuota * PERIOD << " " << PERIOD;
  return valuesAsString.str();
}

Engine::PluginRet ThrottleCgroup::run(OomdContext& ctx) {
  auto monitoredCgroupDirFd = Fs::DirFd::open(monitor_cgroup_path_);

  auto totalMemory = Fs::readMemmaxAt(monitoredCgroupDirFd.value());

  auto usedMemory = Fs::readMemcurrentAt(monitoredCgroupDirFd.value());

  double usedMemoryPercentage =
      (double)usedMemory.value() / totalMemory.value() * 100.0;

  // if usage is high (low on memory)
  if (usedMemoryPercentage > 30) {
    increaseLevel();
    OLOG << "low on memory, new throttle level: " << level << ", pausing...";
    return Engine::PluginRet::ASYNC_PAUSED;
  }

  if (usedMemoryPercentage < 30) {
    decreaseLevel();
    OLOG << "memory is fine, new throttle level: " << level << ", pausing...";

    if (level == 0) {
      return Engine::PluginRet::CONTINUE;
    }
    return Engine::PluginRet::ASYNC_PAUSED;
  }
  return Engine::PluginRet::ASYNC_PAUSED;
};

void ThrottleCgroup::throttle(
    const std::string& cgroupPath,
    const std::string& quotaAndPeriod) {
  const std::string path = cgroupPath + THROTTLE_FILE_PATH;

  writeToFile(path, quotaAndPeriod);
}
} // namespace Oomd
