# ============================================================================
# Exe_ServiceCreator — ROS 2(Autoware) ↔ AUTOSAR AP
#
# 생성기가 만든 구현(aa/**)은 쓰지 않는다. 다중 채널에서 동작하지 않기 때문이다
# (스레드 풀 크기가 태스크 수와 정확히 같아야 하고, 보낼 값을 채우는 경로가 없다).
# 생성물 중 계속 쓰는 것은 **include/oss/** 의 skeleton·proxy·타입과 manifest/** 다.
#
# 나머지 16개 앱과 달리 이 앱만 ROS 2 를 링크한다.
# ============================================================================

add_executable(${PARA_APP_NAME})

# ---- AP 측 ----------------------------------------------------------------
# Para_Target 이 PARA 라이브러리 링크 · 생성 헤더 경로(${PARA_APP_GEN_DIR}/include)
# · OUTPUT_NAME · 배포 install 규칙을 붙인다. include/ 아래에 생성물 oss/ 와
# 손으로 쓴 servicecreator/ 가 함께 있으므로 헤더 경로는 이것 하나로 충분하다.
Para_Target(${PARA_APP_NAME}
            PARA_LIBS_PRIVATE core log exec com
            PARA_GEN_DIR ${PARA_APP_GEN_DIR}
            OUTPUT_NAME ${PARA_APP_NAME}
)
target_include_directories(${PARA_APP_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Exe_routeselector/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Exe_scenarioselector/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Exe_motionvelocityplanner/include")

# ---- ROS 2 측 --------------------------------------------------------------
find_package(rclcpp REQUIRED)
set(SC_MSG_DEPS
    rosgraph_msgs geometry_msgs nav_msgs sensor_msgs
    autoware_map_msgs autoware_perception_msgs autoware_planning_msgs
    autoware_adapi_v1_msgs autoware_common_msgs tier4_planning_msgs
    autoware_vehicle_msgs autoware_internal_msgs tier4_metric_msgs)
foreach(dep IN LISTS SC_MSG_DEPS)
    find_package(${dep} REQUIRED)
endforeach()

# ---- 타깃 ------------------------------------------------------------------
# ROS 2 Humble 은 C++17 을 요구한다. PARA 매크로가 디렉터리 스코프의
# CMAKE_CXX_STANDARD 를 되돌리므로 타깃 수준에서 못박는다.
target_compile_features(${PARA_APP_NAME} PRIVATE cxx_std_17)
target_sources(${PARA_APP_NAME}
               PRIVATE
               main.cpp
               bridge_config.cpp
               servicecreator_node.cpp
               ap_channel_para.cpp)
target_link_libraries(${PARA_APP_NAME} PRIVATE rclcpp::rclcpp pthread)
foreach(dep IN LISTS SC_MSG_DEPS)
    target_link_libraries(${PARA_APP_NAME} PRIVATE ${${dep}_TARGETS})
endforeach()
target_compile_definitions(${PARA_APP_NAME} PRIVATE SERVICECREATOR_WITH_PARA=1)
