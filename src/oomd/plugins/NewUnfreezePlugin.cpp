#include "oomd/plugins/NewUnfreezePlugin.h"
#include "oomd/Log.h"
#include "oomd/PluginRegistry.h"
#include "oomd/include/Types.h"
#include "oomd/util/FreezeUtills.h"
#include "oomd/util/Fs.h"
#include "oomd/util/Util.h"

#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
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
#include <unordered_set>
#include <vector>

namespace Oomd {

REGISTER_PLUGIN(new_unfreeze, NewUnfreezePlugin::create);

void NewUnfreezePlugin::ologKillTarget(
    OomdContext& ctx,
    const CgroupContext& target,
    const std::vector<OomdContext::ConstCgroupContextRef>& /* unused */) {}

std::vector<OomdContext::ConstCgroupContextRef>
NewUnfreezePlugin::rankForKilling(
    OomdContext& ctx,
    const std::vector<OomdContext::ConstCgroupContextRef>& cgroups) {
  return OomdContext::sortDescWithKillPrefs(
      cgroups, [this](const CgroupContext& cgroup_ctx) {
        return cgroup_ctx.current_usage().value_or(0);
      });
}

void NewUnfreezePlugin::unfreezeCgroup(const std::string& cgroupPath) {
  std::string freezeFilePath = cgroupPath + "/cgroup.freeze";
  std::ofstream freezeFile(freezeFilePath);

  if (!freezeFile.is_open()) {
    logError("Error opening freeze file: " + freezeFilePath);
    return;
  }

  freezeFile << UNFREEZE;
  freezeFile.close();

  OLOG << "Unfroze cgroup: " << cgroupPath;
}

std::vector<MemoryRegion> NewUnfreezePlugin::getSwappedRegions(pid_t pid) {
  std::vector<MemoryRegion> regions;
  std::unordered_set<unsigned long> seenAddresses;

  std::string smapsPath = "/proc/" + std::to_string(pid) + "/smaps";
  std::ifstream smapsFile(smapsPath);
  if (!smapsFile.is_open()) {
    logError("Failed to open " + smapsPath);
    return regions;
  }

  std::string line;
  MemoryRegion currentRegion = {0, 0, 0};
  while (std::getline(smapsFile, line)) {
    try {
      if (line.find("Swap:") != std::string::npos) {
        std::istringstream iss(line);
        std::string key, swapSizeStr;
        iss >> key >> swapSizeStr; // "Swap:" and the swap size
        size_t swapSize = std::stoul(swapSizeStr);
        currentRegion.swapSize = swapSize;
        if (swapSize > 0) {
          if (seenAddresses.find(currentRegion.start) == seenAddresses.end()) {
            regions.push_back(currentRegion);
            seenAddresses.insert(currentRegion.start);
          }
        }
      } else if (line.find('-') != std::string::npos) {
        size_t pos = line.find('-');
        currentRegion.start = std::stoul(line.substr(0, pos), nullptr, 16);
        currentRegion.end = std::stoul(
            line.substr(pos + 1, line.find(' ') - pos - 1), nullptr, 16);
        currentRegion.swapSize = 0;
      }
    } catch (const std::invalid_argument& e) {
      logError("Invalid argument in line: " + line);
    } catch (const std::out_of_range& e) {
      logError("Out of range error in line: " + line);
    }
  }

  smapsFile.close();
  return regions;
}

void NewUnfreezePlugin::pageInMemory(int pid) {
  std::vector<MemoryRegion> regions = getSwappedRegions(pid);

  std::sort(
      regions.begin(),
      regions.end(),
      [](const MemoryRegion& a, const MemoryRegion& b) {
        return a.swapSize > b.swapSize;
      });

  for (const auto& region : regions) {
    int pidfd = syscall(SYS_pidfd_open, pid, 0);
    if (pidfd == -1) {
      std::ostringstream oss;
      oss << "Failed to open pidfd for PID " << pid << strerror(errno);
      logError(oss.str());
      return;
    }

    size_t length = region.end - region.start;
    struct iovec iov;
    iov.iov_base = reinterpret_cast<void*>(region.start);
    iov.iov_len = length;

    int ret = syscall(SYS_process_madvise, pidfd, &iov, 1, MADV_WILLNEED, 0);
    if (ret == -1) {
      std::ostringstream oss;
      oss << "process_madvise failed for region " << std::hex << region.start
          << "-" << region.end << ": " << strerror(errno);
      logError(oss.str());
    } else {
      OLOG << "Memory region " << std::hex << region.start << "-" << region.end
           << " advised successfully.";
    }
    close(pidfd);
  }
}

int NewUnfreezePlugin::init(
    const Engine::PluginArgs& args,
    const PluginConstructionContext& context) {

  argParser_.addArgumentCustom(
      "monitor_cgroup",
      monitor_cgroup_path_,
      [](const std::string& cgroup) { return CGROUP_PATH_PREFIX + cgroup; },
      true);

  argParser_.addArgumentCustom(
      "mem_to_unfreeze_in_precentage",
      mem_to_unfreeze_in_precentage,
      [](const std::string& str) { return std::stof(str); },
      true);
  BaseKillPlugin::init(args, context);
  return 0;
}

Engine::PluginRet NewUnfreezePlugin::run(OomdContext& ctx) {
  auto monitoredCgroupDirFd = Fs::DirFd::open(monitor_cgroup_path_);

  auto totalMemory = Fs::readMemmaxAt(monitoredCgroupDirFd.value());

  auto usedMemory = Fs::readMemcurrentAt(monitoredCgroupDirFd.value());

  double usedMemoryPercentage =
      (double)usedMemory.value() / totalMemory.value() * 100.0;

  // If used memory is above treshold , return ASYNC_PAUSED
  if (usedMemoryPercentage > mem_to_unfreeze_in_precentage) {
    OLOG << "Used memory is above treshold (" << usedMemoryPercentage
         << "%), pausing...";
    return Engine::PluginRet::ASYNC_PAUSED;
  }

  return BaseKillPlugin::run(ctx);
}

int NewUnfreezePlugin::tryToKillPids(const std::vector<int>& procs) {
  for (auto pid : procs) {
    pageInMemory(pid);
    OLOG << "Prefetched memory" << pid;
  }
  return 0;
}

void NewUnfreezePlugin::reportKillCompletionToXattr(
      const std::string& cgroupPath,
      int numProcsKilled) {
        unfreezeCgroup(cgroupPath);
      }

} // namespace Oomd
