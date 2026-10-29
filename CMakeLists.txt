cmake_minimum_required(VERSION 3.16)
project(decision_ap VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED)

add_library(planning_core STATIC
  src/Ecu_Machine/DecisionAP/src/planning_evaluator/planning_evaluator_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_dynamic_drivable_area.cpp
  src/Ecu_Machine/DecisionAP/src/costmap_generator/costmap_generator_core.cpp
  src/Ecu_Machine/DecisionAP/src/freespace_planner/freespace_planner_core.cpp
  src/Ecu_Machine/DecisionAP/src/remaining_distance_time_calculator/remaining_distance_time_core.cpp
  src/Ecu_Machine/DecisionAP/src/mission_planner/mission_planner_core.cpp
  src/Ecu_Machine/DecisionAP/src/mission_planner/planning_route_coordinator.cpp
  src/Ecu_Machine/DecisionAP/src/mission_planner/mission_reroute_safety.cpp
  src/Ecu_Machine/DecisionAP/src/mission_planner/route_selector_core.cpp
  src/Ecu_Machine/DecisionAP/src/scenario_selector/scenario_selector.cpp
  src/Ecu_Machine/DecisionAP/src/pipeline/planning_final_pipeline_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/stop_line_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/crosswalk/crosswalk_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/walkway_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/intersection/intersection_stuck_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/intersection/merge_from_private_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/intersection/intersection_collision_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/intersection/intersection_occlusion_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/detection_area_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/speed_bump_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/no_drivable_lane_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/run_out/run_out_state.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/run_out/run_out_collision.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/run_out/run_out_obstacle_creator.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/trajectory_stop.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/virtual_traffic_light_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/blind_spot_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/crosswalk/crosswalk_collision.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/crosswalk/crosswalk_braking.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/crosswalk/crosswalk_object_state.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/crosswalk/crosswalk_occlusion.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/occlusion_spot_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/modules/occlusion_path_resample.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/traffic_light/traffic_light_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/traffic_light/traffic_light_signal.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_path_postprocess.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_path_shift.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_path_lane_safety.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_path_object_safety.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_path_prediction.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_lane_change_safety.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_candidate_callback.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/core/behavior_velocity_perception_bridge.cpp
  src/Ecu_Machine/DecisionAP/src/pipeline/planning_pipeline_cruise_bridge.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_lane_change_route.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_static_avoidance.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_route_action.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_frenet_sampling.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_dynamic_avoidance.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_pull_over_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_freespace_pull_over.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_freespace_rrtstar.cpp
  lib/autoware_reeds_shepp/src/reeds_shepp.cpp
  lib/autoware_reeds_shepp/src/rrtstar_core.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_goal_refinement.cpp
  src/Ecu_Machine/DecisionAP/src/behavior_path_planner/behavior_goal_safety.cpp
  src/Ecu_Machine/DecisionAP/src/path_smoother/elastic_band_core.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_model.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_qp.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_solution.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_reference_geometry.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_bounds.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_reference_data.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_core.cpp
  src/Ecu_Machine/DecisionAP/src/path_optimizer/path_optimizer_reset.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/motion_velocity_core.cpp
  src/Ecu_Machine/DecisionAP/src/obstacle_cruise_planner/obstacle_cruise_stop_core.cpp
  src/Ecu_Machine/DecisionAP/src/obstacle_cruise_planner/obstacle_cruise_optimization.cpp
  src/Ecu_Machine/DecisionAP/src/obstacle_cruise_planner/obstacle_cruise_slowdown_core.cpp
  src/Ecu_Machine/DecisionAP/src/obstacle_cruise_planner/obstacle_cruise_pid_core.cpp
  src/Ecu_Machine/DecisionAP/src/obstacle_cruise_planner/obstacle_cruise_candidate.cpp
  src/Ecu_Machine/DecisionAP/src/obstacle_cruise_planner/obstacle_cruise_collision.cpp
  src/Ecu_Machine/DecisionAP/src/obstacle_cruise_planner/obstacle_cruise_pointcloud.cpp
  src/Ecu_Machine/DecisionAP/src/adapters/pointcloud2_adapter.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/dynamic_obstacle_stop_core.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/obstacle_velocity_limiter_core.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/obstacle_velocity_limiter_bicycle.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/obstacle_velocity_limiter_pointcloud.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/out_of_lane_core.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/out_of_lane_geometry.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/out_of_lane_stop_line.cpp
  src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/out_of_lane_object_filter.cpp
  src/Ecu_Machine/DecisionAP/src/external_velocity_limit_selector/velocity_limit_selector.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_filter.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_resample.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_smoother.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_node_policy.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_path_limits.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_external_limit.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/analytical_velocity_smoother.cpp
  src/Ecu_Machine/DecisionAP/src/velocity_smoother/trajectory_precheck.cpp
  src/Ecu_Machine/DecisionAP/src/planning_validator/planning_validator_core.cpp
  src/Ecu_Machine/DecisionAP/src/planning_validator/planning_validator_diagnostics.cpp)
target_sources(planning_core PRIVATE src/Ecu_Machine/DecisionAP/src/pipeline/planning_pipeline_core.cpp)
target_sources(planning_core PRIVATE src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_qp_problem.cpp src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_qp_solver.cpp)
set(PROXSUITE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/proxsuite-devel")
option(PLANNING_USE_PROXQP "제공된 ProxSuite 소스를 사용해 ProxQP로 풉니다" ON)
set(PLANNING_HAS_PROXQP OFF)
if(PLANNING_USE_PROXQP AND
   EXISTS "${PROXSUITE_SOURCE_DIR}/include/proxsuite/proxqp/sparse/sparse.hpp")
  set(PLANNING_HAS_PROXQP ON)
  file(READ "${PROXSUITE_SOURCE_DIR}/package.xml" PROXSUITE_PACKAGE_XML)
  string(REGEX MATCH "<version>([0-9]+)\\.([0-9]+)\\.([0-9]+)</version>"
    PROXSUITE_VERSION_MATCH "${PROXSUITE_PACKAGE_XML}")
  if(NOT PROXSUITE_VERSION_MATCH)
    message(FATAL_ERROR "ProxSuite 버전을 package.xml에서 읽을 수 없습니다")
  endif()
  set(PROXSUITE_VERSION_MAJOR "${CMAKE_MATCH_1}")
  set(PROXSUITE_VERSION_MINOR "${CMAKE_MATCH_2}")
  set(PROXSUITE_VERSION_PATCH "${CMAKE_MATCH_3}")
  configure_file(cmake/proxsuite_config.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/proxsuite/config.hpp" @ONLY)
  target_sources(planning_core PRIVATE src/Ecu_Machine/DecisionAP/src/velocity_smoother/jerk_proxqp_solver.cpp)
  target_include_directories(planning_core PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${PROXSUITE_SOURCE_DIR}/include")
  target_compile_definitions(planning_core PRIVATE PLANNING_HAS_PROXQP=1)
endif()
target_include_directories(planning_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/Ecu_Machine/DecisionAP/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(planning_core PRIVATE ${Boost_INCLUDE_DIRS}
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/autoware_reeds_shepp/include")
target_link_libraries(planning_core PUBLIC Eigen3::Eigen)
target_compile_features(planning_core PUBLIC cxx_std_17)
target_compile_options(planning_core PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS planning_core EXPORT planning_core_targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/Ecu_Machine/DecisionAP/include/planning DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT planning_core_targets
  FILE planning_core_targets.cmake
  NAMESPACE planning::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/planning_core)
configure_package_config_file(
  cmake/planning_core_config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/planning_core-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/planning_core)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/planning_core-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/planning_core-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/planning_core-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/planning_core)

option(PLANNING_BUILD_GRID_ADAPTER
  "OpenCV가 있으면 점유 격자 장애물 어댑터를 빌드합니다" ON)
if(PLANNING_BUILD_GRID_ADAPTER)
  find_package(OpenCV QUIET COMPONENTS core imgproc)
else()
  set(OpenCV_FOUND OFF)
endif()
if(OpenCV_FOUND)
  add_library(planning_grid_adapter STATIC
    src/Ecu_Machine/DecisionAP/src/motion_velocity_planner/obstacle_velocity_limiter_grid.cpp)
  target_include_directories(planning_grid_adapter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Ecu_Machine/DecisionAP/include ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(planning_grid_adapter PUBLIC
    planning_core ${OpenCV_LIBS})
  target_compile_features(planning_grid_adapter PUBLIC cxx_std_17)
  target_compile_options(planning_grid_adapter PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(PLANNING_BUILD_OSM_ADAPTER
  "TinyXML2가 있으면 OSM 지도 어댑터를 빌드합니다" ON)
if(PLANNING_BUILD_OSM_ADAPTER)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(TINYXML2 QUIET IMPORTED_TARGET tinyxml2)
  endif()
else()
  set(TINYXML2_FOUND OFF)
endif()
if(TINYXML2_FOUND)
  add_library(planning_osm_adapter STATIC src/Ecu_Machine/DecisionAP/src/adapters/lanelet_osm_adapter.cpp)
  target_include_directories(planning_osm_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/Ecu_Machine/DecisionAP/include)
  target_link_libraries(planning_osm_adapter PUBLIC planning_core PkgConfig::TINYXML2)
  target_compile_features(planning_osm_adapter PUBLIC cxx_std_17)
  target_compile_options(planning_osm_adapter PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(ROSLESS_LANELET2_SOURCE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/Rosless-Lanelet2"
  CACHE PATH "저장소에 포함된 Rosless-Lanelet2 소스 위치")
set(PLANNING_LANELET_BIN_DEFAULT OFF)
if(EXISTS "${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_core/CMakeLists.txt" AND
   EXISTS "${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_io/include/lanelet2_io/io_handlers/Serialize.h")
  find_package(Boost QUIET COMPONENTS serialization)
  if(Boost_FOUND)
    set(PLANNING_LANELET_BIN_DEFAULT ON)
  endif()
endif()
option(PLANNING_BUILD_LANELET_BIN_ADAPTER
  "저장소의 Rosless-Lanelet2 소스로 이진 지도 어댑터를 빌드합니다"
  ${PLANNING_LANELET_BIN_DEFAULT})
if(PLANNING_BUILD_LANELET_BIN_ADAPTER)
  if(NOT EXISTS "${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_core/CMakeLists.txt" OR
     NOT EXISTS "${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_io/include/lanelet2_io/io_handlers/Serialize.h")
    message(FATAL_ERROR "Rosless-Lanelet2 소스를 찾을 수 없습니다")
  endif()
  if(NOT Boost_FOUND)
    find_package(Boost REQUIRED COMPONENTS serialization)
  endif()
  add_subdirectory("${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_core"
    "${CMAKE_CURRENT_BINARY_DIR}/lanelet2_core" EXCLUDE_FROM_ALL)
  add_subdirectory("${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_traffic_rules"
    "${CMAKE_CURRENT_BINARY_DIR}/lanelet2_traffic_rules" EXCLUDE_FROM_ALL)
  add_subdirectory("${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_routing"
    "${CMAKE_CURRENT_BINARY_DIR}/lanelet2_routing" EXCLUDE_FROM_ALL)
  add_library(planning_lanelet_bin_adapter STATIC
    src/Ecu_Machine/DecisionAP/src/costmap_generator/costmap_map_adapter.cpp
    src/Ecu_Machine/DecisionAP/src/planning_evaluator/planning_evaluator_map_adapter.cpp
    src/Ecu_Machine/DecisionAP/src/adapters/lanelet_bin_adapter.cpp
    src/Ecu_Machine/DecisionAP/src/mission_planner/mission_route_map_adapter.cpp
    src/Ecu_Machine/DecisionAP/src/remaining_distance_time_calculator/remaining_distance_map_adapter.cpp
    src/Ecu_Machine/DecisionAP/src/behavior_velocity_planner/core/behavior_velocity_map_planner.cpp
    src/Ecu_Machine/DecisionAP/src/pipeline/planning_pipeline_map_adapter.cpp)
  target_include_directories(planning_lanelet_bin_adapter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Ecu_Machine/DecisionAP/include)
  target_include_directories(planning_lanelet_bin_adapter PRIVATE
    "${ROSLESS_LANELET2_SOURCE_DIR}/lanelet2_io/include")
  target_link_libraries(planning_lanelet_bin_adapter PUBLIC
    planning_core lanelet2_core lanelet2_routing Boost::serialization)
  target_compile_features(planning_lanelet_bin_adapter PUBLIC cxx_std_17)
  target_compile_options(planning_lanelet_bin_adapter PRIVATE
    -Wall -Wextra -Wpedantic)
endif()
