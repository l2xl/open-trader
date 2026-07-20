# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

# Helper invoked by ThorvgBuild.cmake at build time to (re)configure the meson build dir.
# Handles three states:
#   - fresh build dir              -> meson setup ${BUILD_DIR} ${SRC_DIR} <opts>
#   - already configured           -> meson setup --reconfigure ${BUILD_DIR} <opts>  (regenerates build.ninja and applies any option changes)
#   - configured + build.ninja ok  -> still safe to call --reconfigure, idempotent

set(THORVG_MESON_OPTS
                --buildtype=release
                --default-library=static
                -Dengines=cpu
                -Dloaders=ttf
                -Dsavers=
                -Dbindings=
                -Dtools=
                -Dextra=
                -Dthreads=false
                -Dtests=false
                -Dlog=false
)

if(EXISTS "${BUILD_DIR}/meson-info/intro-buildoptions.json")
    execute_process(
        COMMAND "${MESON}" setup --reconfigure "${BUILD_DIR}" ${THORVG_MESON_OPTS}
        WORKING_DIRECTORY "${SRC_DIR}"
        RESULT_VARIABLE rc
    )
else()
    execute_process(
        COMMAND "${MESON}" setup "${BUILD_DIR}" "${SRC_DIR}" ${THORVG_MESON_OPTS}
        RESULT_VARIABLE rc
    )
endif()

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "meson setup failed (exit code ${rc})")
endif()
