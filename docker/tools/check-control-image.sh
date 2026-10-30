#!/usr/bin/env bash
# Run inside the Control image. Print the resolved launch share path on success.
set -eo pipefail
source /opt/ros/humble/setup.bash
source /opt/autoware/setup.bash
if [[ -f /opt/autoware-system/setup.bash ]]; then
  source /opt/autoware-system/setup.bash
fi
ros2 pkg prefix tier4_system_launch >/dev/null
ros2 pkg prefix autoware_mrm_handler >/dev/null
control_share="$(ros2 pkg prefix --share obigo_launch)"
grep -q 'name="launch_system" value="true"' \
  "${control_share}/launch/sample_adsw_control_run.launch.xml"
printf '%s\n' "${control_share}"
