#include <string>
#include <vector>
#include "oomd/engine/BasePlugin.h"

#include <chrono>

namespace Oomd {

#define THROTTLE_FILE_PATH "/cpu.max"
#define MAX_LEVEL 4
#define PERIOD 100000
#define LEVEL_1 0.65
#define LEVEL_2 0.3
#define LEVEL_3 0.15
#define LEVEL_4 0.05

class ThrottleCgroup : public Engine::BasePlugin {
 public:
  int init(
      const Engine::PluginArgs& args,
      const PluginConstructionContext& context) override;

  static ThrottleCgroup* create() {
    return new ThrottleCgroup();
  }

  void decreaseLevel();

  void increaseLevel();

  void changeLevel(int change);

  void throttle(
      const std::string& cgroupPath,
      const std::string& quotaAndPeriod);

  std::string getCurrentQuotaAndPeriod();

  Engine::PluginRet run(OomdContext& ctx) override;

  ~ThrottleCgroup() = default;

 private:
  int level = 0;
  std::string monitor_cgroup_path_;
  std::string throttle_cgroup_path_;
};

} // namespace Oomd