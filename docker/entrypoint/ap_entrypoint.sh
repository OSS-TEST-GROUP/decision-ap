#!/usr/bin/env bash
# AP 런타임 컨테이너 진입점. EM 이 Exe_ServiceCreator와 CM을 기동한다.
#
# 하는 일은 환경을 갖추는 것뿐이다.
set -e

# 설정 트리. 기본은 설치된 것을 그대로 쓴다.
AP_ETC=/opt/ap/etc

# ── SOME/IP 주소 ────────────────────────────────────────────────────────────
# 생성 매니페스트의 unicast-address 는 192.168.100.92 로 고정돼 있고 CM 이 그
# 주소로 바인딩한다. 전용 네트워크에서는 그 주소를 컨테이너 lo 에 붙이면 되므로
# 매니페스트를 고칠 일이 없다(아래).
#
# 호스트 네트워크로 띄우면 그 lo 가 호스트의 것이다. 그때 AP_SOMEIP_ADDRESS 를
# 주면 설정 사본을 만들어 주소를 바꾼다 — 호스트에 주소를 붙이지 않는다.
# 설치 트리는 마운트 모드에서 호스트의 빌드 산출물이므로 원본은 건드리지 않는다.
# SOME/IP 는 AP 프로세스끼리 쓰는 것이라 주소를 바깥에 낼 필요가 없다.
# run-katech-stack.sh 가 호스트 네트워크에서 이 값을 넣는다.
if [ -n "${AP_SOMEIP_ADDRESS:-}" ]; then
    case "${AP_SOMEIP_ADDRESS}" in
        *[!0-9.]*|'') echo "[ap] FATAL: AP_SOMEIP_ADDRESS 는 IPv4 주소입니다 (받은 값: ${AP_SOMEIP_ADDRESS})" >&2; exit 1 ;;
    esac
    AP_ETC=/run/ap-etc
    rm -rf "${AP_ETC}"
    cp -a /opt/ap/etc "${AP_ETC}" || {
        echo "[ap] FATAL: 설정 사본을 만들지 못했습니다: ${AP_ETC}" >&2; exit 1; }
    mapfile -t _targets < <(grep -rl '192\.168\.100\.92' "${AP_ETC}" 2>/dev/null)
    if [ "${#_targets[@]}" -eq 0 ]; then
        echo "[ap] FATAL: 매니페스트에서 192.168.100.92 를 찾지 못했습니다" >&2; exit 1
    fi
    sed -i "s/192\.168\.100\.92/${AP_SOMEIP_ADDRESS}/g" "${_targets[@]}"
    echo "[ap] SOME/IP 주소 ${AP_SOMEIP_ADDRESS} — 매니페스트 ${#_targets[@]}개를 고친 사본을 씁니다 (${AP_ETC})"
fi

# CM 이 바인딩할 주소가 컨테이너 안에 있어야 한다. 없으면 lo 에 붙인다.
AP_MANIFEST="${AP_ETC}/com/CM_CmInstantiation.json"
AP_ADDR=$(awk '/sd-endpoints/{exit} /"ip-address"/{gsub(/[^0-9.]/,""); print; exit}' \
          "${AP_MANIFEST}" 2>/dev/null)
if [ -z "${AP_ADDR}" ]; then
    echo "[ap] FATAL: ${AP_MANIFEST} 에서 unicast-address 를 읽지 못했습니다" >&2
    exit 1
fi
if ip -o addr show 2>/dev/null | grep -q " ${AP_ADDR}/"; then
    echo "[ap] unicast-address ${AP_ADDR} — 이미 있습니다"
elif ip addr add "${AP_ADDR}/32" dev lo 2>/dev/null; then
    echo "[ap] unicast-address ${AP_ADDR} — lo 에 붙였습니다"
else
    echo "[ap] FATAL: ${AP_ADDR} 를 붙이지 못했습니다. 이대로면 CM 이 뜨지 못합니다." >&2
    echo "[ap]        주소를 붙이려면 CAP_NET_ADMIN 이 필요합니다. 셋 중 하나로 하십시오:" >&2
    echo "[ap]          1) docker run 에 --cap-add NET_ADMIN 을 줍니다" >&2
    echo "[ap]          2) 그 주소를 이미 가진 네트워크에 컨테이너를 붙입니다" >&2
    echo "[ap]             (예: docker network create --subnet 192.168.100.0/24 <net>" >&2
    echo "[ap]                  docker run --network <net> --ip ${AP_ADDR} ...)" >&2
    echo "[ap]          3) AP_SOMEIP_ADDRESS 로 이미 있는 주소를 지정합니다 (예: 127.0.0.1)" >&2
    exit 1
fi

source "/opt/ros/${ROS_DISTRO}/setup.bash"
# AP 트리도 source 한다. 메시지 정의(share/)가 여기 있어 없으면 ros2 CLI 가 타입을
# 찾지 못한다. 실행 자체는 para-exec.sh 가 LD_LIBRARY_PATH 로 잡으므로 무관하다.
source /opt/ap/setup.bash 2>/dev/null || true

# MMF 연동. adsw_manager 는 EM 이 기동 중 직접 fork/exec 하고 EM 의 환경을 그대로
# 물려받는다 — 머신 매니페스트로는 도달하지 않으므로 여기서 export 해야 한다.
export MMF_DDS_DOMAIN_ID="${MMF_DDS_DOMAIN_ID:-42}"
echo "[ap] MMF_DDS_DOMAIN_ID=${MMF_DDS_DOMAIN_ID}"

# para-exec.sh 는 PARA_CONF 와 PARA_LOG_LEVEL 을 스크립트 안에서 정한다. 설정 사본을
# 쓰거나 로그 수준을 지정한 경우에는 그 스크립트가 하는 일을 재현해 EM 을 직접 부른다.
if [ -n "${PARA_LOG_LEVEL:-}" ] || [ "${AP_ETC}" != /opt/ap/etc ]; then
    export PARA_CORE=/opt/ap
    export PARA_CONF="${AP_ETC}"
    export PARA_DATA=/opt/ap/var
    export PARA_APPL=/opt/ap/opt
    export PARA_LOG_LEVEL="${PARA_LOG_LEVEL:-error}"
    export LD_LIBRARY_PATH="${PARA_CORE}/lib:${PARA_CORE}/external/lib:${LD_LIBRARY_PATH}"
    mkdir -p "${PARA_DATA}"
    echo "[ap] PARA_CONF=${PARA_CONF} PARA_LOG_LEVEL=${PARA_LOG_LEVEL} (EM 직접 기동)"
    exec /opt/ap/bin/EM "$@"
fi

exec /opt/ap/para-exec.sh "$@"
