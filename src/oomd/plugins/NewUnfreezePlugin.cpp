#include "oomd/plugins/NewUnfreezePlugin.h"
#include "oomd/Log.h"
#include "oomd/PluginRegistry.h"
#include "oomd/include/Types.h"
#include "oomd/util/FreezeUtills.h"
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

int NewUnfreezePlugin::init(
    const Engine::PluginArgs& args,
    const PluginConstructionContext& context) {
  argParser_.addArgumentCustom(
      "cgroup",
      cgroups_,
      [context](const std::string& cgroupStr) {
        return PluginArgParser::parseCgroup(context, cgroupStr);
      },
      true);
  if (!argParser_.parse(args)) {
    return 1;
  }
  return 0;
}

Engine::PluginRet NewUnfreezePlugin::run(OomdContext& ctx) {
  struct sysinfo memInfo;
  sysinfo(&memInfo);

  long long totalMemory = memInfo.totalram;
  totalMemory *= memInfo.mem_unit;

  long long freeMemory = memInfo.freeram;
  freeMemory *= memInfo.mem_unit;

  double freeMemoryPercentage = (double)freeMemory / totalMemory * 100.0;

  // If free memory is above 70%, return ASYNC_PAUSED
  if (freeMemoryPercentage < 30.0) {
    OLOG << "Free memory is above 30% (" << freeMemoryPercentage
         << "%), pausing...";
    return Engine::PluginRet::ASYNC_PAUSED;
  }

  for (const auto& cgroupPath : cgroups_) {
    std::string path = cgroupPath.absolutePath();

    // Create a thread for each cgroup path and detach it
    std::thread([this, path]() {
      processCgroup(path);
    }).detach(); // Detach the thread so it runs independently
  }

  return Engine::PluginRet::CONTINUE;
}

void processCgroup(const std::string& cgroupPath) {
  std::vector<int> pids;
  const std::string procsFilePath = cgroupPath + "/cgroup.procs";

  // Open the cgroup.procs file
  std::ifstream file(procsFilePath);
  if (!file) {
    std::cerr << "Failed to open " << procsFilePath << std::endl;
    return pids;
  }

  // Read each line (PID) and add it to the vector
  int pid;
  while (file >> pid) {
    pids.push_back(pid);
  }

  for (int pid : pids) {
    pageInMemory(pid);
  }
  unfreezeCgroup();
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

void NewUnfreezePlugin::unfreezeProcess(int pid) {
  if (pid <= 0) {
    logError("Invalid PID: " + std::to_string(pid));
    return;
  }

  char tasks_path[256];
  snprintf(tasks_path, sizeof(tasks_path), "%s/tasks", MY_FREEZER_PATH);
  char pid_str[16];
  snprintf(pid_str, sizeof(pid_str), "%d", pid);
  writeToFile(tasks_path, pid_str);

  char state_path[256];
  snprintf(state_path, sizeof(state_path), "%s/freezer.state", MY_FREEZER_PATH);
  writeToFile(state_path, "THAWED");
}

} // namespace Oomd
