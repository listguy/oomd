#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>
#include "oomd/include/CgroupPath.h"
#include "oomd/plugins/BaseKillPlugin.h"

namespace Oomd {

#define UNFREEZE 0
#define DEFAULT_MONITOR_CGROUP_PATH "/sys/fs/cgroup/user.slice"
#define DEFAULT_MEM_TO_UNFREEZE_PRECENTAGE 20
struct MemoryRegion {
  unsigned long start;
  unsigned long end;
  size_t swapSize;
};

class NewUnfreezePlugin : public BaseKillPlugin {
 public:
  int init(
      const Engine::PluginArgs& args,
      const PluginConstructionContext& context) override;

  static NewUnfreezePlugin* create() {
    return new NewUnfreezePlugin();
  }

  Engine::PluginRet run(OomdContext& ctx) override;

  ~NewUnfreezePlugin() = default;

 protected:
  std::vector<OomdContext::ConstCgroupContextRef> rankForKilling(
      OomdContext& ctx,
      const std::vector<OomdContext::ConstCgroupContextRef>& cgroups) override;

  void ologKillTarget(
      OomdContext& ctx,
      const CgroupContext& target,
      const std::vector<OomdContext::ConstCgroupContextRef>& peers) override;

  int tryToKillPids(const std::vector<int>& procs) override;

  void reportKillCompletionToXattr(
      const std::string& cgroupPath,
      int numProcsKilled);

 private:
  std::unordered_set<CgroupPath> cgroups_;
  float mem_to_unfreeze_;
  std::string monitor_cgroup_;
  std::vector<MemoryRegion> getSwappedRegions(pid_t pid);
  void unfreezeCgroup(const std::string& cgroupPath);
  void pageInMemory(int pid);
  std::string monitor_cgroup_path_ = DEFAULT_MONITOR_CGROUP_PATH;
  float mem_to_unfreeze_in_precentage = DEFAULT_MEM_TO_UNFREEZE_PRECENTAGE;
};

} // namespace Oomd