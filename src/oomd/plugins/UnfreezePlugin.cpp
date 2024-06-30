#include "oomd/plugins/UnfreezePlugin.h"
#include "oomd/Log.h"
#include "oomd/PluginRegistry.h"
#include "oomd/include/Types.h"
#include "oomd/util/Util.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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
#include "../../../usr/include/x86_64-linux-gnu/sys/stat.h"

#define CGROUP_PATH "/sys/fs/cgroup/freezer/my_freezer"

struct MemoryRegion
{
    unsigned long start;
    unsigned long end;
    size_t swapSize;
};

std::vector<MemoryRegion> getSwappedRegions(pid_t pid);
void unfreezeProcess(int pid);
void writeToFile(const std::string& path, const std::string& value);
void pageInMemory(pid_t pid);

namespace Oomd {

REGISTER_PLUGIN(unfreeze, UnfreezePlugin<>::create);

template <typename Base>
int UnfreezePlugin<Base>::init(
    const Engine::PluginArgs& args,
    const PluginConstructionContext& context) {
  return BaseKillPlugin::init(args, context);
}

template <typename Base>
int UnfreezePlugin<Base>::tryToKillPids(const std::vector<int>& procs) {
for(auto pid : procs) {
      pageInMemory(pid);
    unfreezeProcess(pid);
}
  return 0;
}

template <typename Base>
std::vector<OomdContext::ConstCgroupContextRef> UnfreezePlugin<Base>::rankForKilling(
    OomdContext& ctx,
    const std::vector<OomdContext::ConstCgroupContextRef>& cgroups) {
  return OomdContext::sortDescWithKillPrefs(
      cgroups,
      [this](const CgroupContext& cgroup_ctx) {
          return cgroup_ctx.current_usage().value_or(0);
      });
}

template <typename Base>
void UnfreezePlugin<Base>::ologKillTarget(
    OomdContext& ctx,
    const CgroupContext& target,
    const std::vector<OomdContext::ConstCgroupContextRef>& /* unused */) {
    OLOG << "Nitzan and Guy unfreezed \"" << target.cgroup().relativePath() << "\" ("
         << target.current_usage().value_or(0) / 1024 / 1024
         << "MB) based on swap usage at "
         << target.swap_usage().value_or(0) / 1024 / 1024 << "MB;";
}

} // namespace Oomd

std::vector<MemoryRegion> getSwappedRegions(pid_t pid)
{
    std::vector<MemoryRegion> regions;
    std::unordered_set<unsigned long> seenAddresses;

    std::string smapsPath = "/proc/" + std::to_string(pid) + "/smaps";
    std::ifstream smapsFile(smapsPath);
    if (!smapsFile.is_open())
    {
        std::cerr << "Failed to open " << smapsPath << std::endl;
        return regions;
    }

    std::string line;
    MemoryRegion currentRegion = {0, 0, 0};
    while (std::getline(smapsFile, line))
    {
        try
        {
            if (line.find("Swap:") != std::string::npos)
            {
                // Extract the last token which should be the swap size
                std::istringstream iss(line);
                std::string key, swapSizeStr;
                iss >> key >> swapSizeStr; // "Swap:" and the swap size
                size_t swapSize = std::stoul(swapSizeStr);
                currentRegion.swapSize = swapSize;
                if (swapSize > 0)
                {
                    if (seenAddresses.find(currentRegion.start) == seenAddresses.end())
                    {
                        regions.push_back(currentRegion);
                        seenAddresses.insert(currentRegion.start);
                    }
                }
            }
            else if (line.find('-') != std::string::npos)
            {
                size_t pos = line.find('-');
                currentRegion.start = std::stoul(line.substr(0, pos), nullptr, 16);
                currentRegion.end = std::stoul(line.substr(pos + 1, line.find(' ') - pos - 1), nullptr, 16);
                currentRegion.swapSize = 0;
            }
        }
        catch (const std::invalid_argument &e)
        {
            std::cerr << "Invalid argument in line: " << line << std::endl;
        }
        catch (const std::out_of_range &e)
        {
            std::cerr << "Out of range error in line: " << line << std::endl;
        }
    }

    smapsFile.close();
    return regions;
}

void pageInMemory(pid_t pid)
{
    // Get the swapped memory regions
    std::vector<MemoryRegion> regions = getSwappedRegions(pid);

    // Sort the regions by swap size in descending order
    std::sort(regions.begin(), regions.end(), [](const MemoryRegion &a, const MemoryRegion &b)
              { return a.swapSize > b.swapSize; });

    // Page in the memory regions
    for (const auto &region : regions)
    {
        std::string memFilePath = "/proc/" + std::to_string(pid) + "/mem";
        int memFile = open(memFilePath.c_str(), O_RDONLY);
        if (memFile == -1) {
            std::cerr << "Failed to open " << memFilePath << ": " << strerror(errno) << std::endl;
            return;
        }

        for (const auto& region : regions) {
            size_t length = region.end - region.start;
            std::vector<char> buffer(length);

            if (pread(memFile, buffer.data(), length, region.start) == -1) {
                std::cerr << "Failed to read memory region " << std::hex << region.start << "-" << region.end << ": " << strerror(errno) << std::endl;
            } else {
                std::cout << "Memory region " << std::hex << region.start << "-" << region.end << " read successfully." << std::endl;
                // Process the memory content as needed
            }
        }

        close(memFile);
    }
}

void writeToFile(const std::string& path, const std::string& value) {
  std::ofstream file(path);
  if (!file.is_open()) {
    OLOG << "Error opening file: " << path << " - " << strerror(errno);
    throw std::runtime_error("Failed to open file: " + path);
  }
  file << value;
  if (file.fail()) {
    OLOG << "Error writing to file: " << path << " - " << strerror(errno);
    throw std::runtime_error("Failed to write to file: " + path);
  }
  file.close();
  OLOG << "Successfully wrote to file: " << path;
}

void unfreezeProcess(int pid) {
  // Add the process to the cgroup
  if (pid <= 0) {
    OLOG << "Invalid PID: " << pid;
    return;
  }

  char tasks_path[256];
  snprintf(tasks_path, sizeof(tasks_path), "%s/tasks", CGROUP_PATH);
  char pid_str[16];
  snprintf(pid_str, sizeof(pid_str), "%d", pid);
  writeToFile(tasks_path, pid_str);

  // Freeze the process
  char state_path[256];
  snprintf(state_path, sizeof(state_path), "%s/freezer.state", CGROUP_PATH);
  writeToFile(state_path, "THAWED");
  OLOG << "process: " << pid << "is now thawed!";
}