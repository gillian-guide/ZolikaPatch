#include "zpatch/vehicle_animation.hpp"
#include <cmath>
namespace zpatch {
namespace {
bool equal_bone_name(std::string_view a, std::string_view b) noexcept {
  if(a.size()!=b.size())return false;
  for(std::size_t i=0;i<a.size();++i) {
    auto lower=[](unsigned char c){ return c>='A' && c<='Z' ? c+32u : static_cast<unsigned>(c); };
    if(lower(static_cast<unsigned char>(a[i]))!=lower(static_cast<unsigned char>(b[i])))return false;
  }
  return true;
}
}
const NamedVehicleBone* find_vehicle_bone(const NamedVehicleBone* records,
    std::size_t count, std::string_view name) noexcept {
  if(!records)return nullptr;
  for(std::size_t i=0;i<count;++i)
    if(equal_bone_name(records[i].name,name))return records+i;
  return nullptr;
}
void reset_vehicle_bone_rotation(std::array<float,16>& matrix) noexcept {
  matrix[0]=1.0f; matrix[1]=0.0f; matrix[2]=0.0f;
  matrix[4]=0.0f; matrix[5]=1.0f; matrix[6]=0.0f;
  matrix[8]=0.0f; matrix[9]=0.0f; matrix[10]=1.0f;
}
bool update_engine_animation(EngineAnimationState& state, float timestep,
    float rpm, std::uint32_t flags, float& rotation) noexcept {
  if (!std::isfinite(timestep) || !std::isfinite(rpm) ||
      !std::isfinite(state.phase) || !std::isfinite(state.previous_timestep)) return false;
  auto next=state;
  if (next.previous_timestep!=timestep) {
    const double step=next.descending ? -0.5 : 0.5;
    next.phase=static_cast<float>(static_cast<double>(timestep)*step+next.phase);
    if (static_cast<double>(next.phase)>0.01) next.descending=true;
    if (static_cast<double>(next.phase)<-0.01) next.descending=false;
  }
  float scale=(flags&8u) ? static_cast<float>(static_cast<double>(rpm)*0.75+0.25) : 0.0f;
  if (flags&0x10u) scale=1.0f;
  const float result=scale*next.phase;
  if (!std::isfinite(next.phase) || !std::isfinite(result)) return false;
  next.previous_timestep=timestep;
  state=next;
  rotation=result;
  return true;
}

bool calculate_suspension_commands(bool enabled, int vehicle_type,
    float timestep, float suspension_motion, float pitch, float roll, float heave,
    SuspensionCommands& output) noexcept {
  if (!enabled || vehicle_type!=5 || !std::isfinite(timestep) ||
      !std::isfinite(suspension_motion) || !std::isfinite(pitch) ||
      !std::isfinite(roll) || !std::isfinite(heave)) return false;
  const float travel=timestep*suspension_motion*100.0f;
  if (!std::isfinite(travel)) return false;
  // pitch/roll/heave are opaque source fields at +0x1f08/+0x1f04/+0x1f00;
  // descriptive names do not assert fully recovered physics semantics.
  SuspensionCommands result{{
    {0x59u,4,travel,false}, {0x5bu,4,travel,false},
    {0x5au,4,travel,false}, {0x5cu,4,travel,false},
    {0x60u,3,static_cast<float>(static_cast<double>(pitch)*0.5),true},
    {0x61u,3,static_cast<float>(static_cast<double>(pitch)*-0.5),true},
    {0x5eu,3,static_cast<float>(static_cast<double>(roll)*-0.5),true},
    {0x5fu,3,static_cast<float>(static_cast<double>(roll)*-0.5),true},
    {0x5du,5,static_cast<float>(static_cast<double>(heave)*0.5),true}
  }};
  output=result;
  return true;
}
}
