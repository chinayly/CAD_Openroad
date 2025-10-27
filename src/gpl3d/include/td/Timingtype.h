#pragma once
namespace gpl3d::td {

struct StaSummary {
  double WNS_setup{0.0}; // ns
  double TNS_setup{0.0}; // ns
  double WNS_hold{0.0};  // ns
  double TNS_hold{0.0};  // ns
};

} // namespace gpl3d::td