#!/bin/bash

set -xveo pipefail

arg1=$1
build="./build"
if [ "$arg1" != "linux" ]; then
	toolchain="cmake/switch.cmake"
	build="./build_switch"
fi

SCRIPTDIR=$(dirname "$0")
BASEDIR=$(realpath "${SCRIPTDIR}/../../")

build_chiaki (){
	pushd "${BASEDIR}"
		#rm -rf ./build
		echo "Base = ${BASEDIR}/build_switch"
	    rm -rf build_switch
		# purge leftover proto/nanopb_pb2.py which may have been created with another protobuf version
		rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py

		cmake_args=(
			-B "${build}"
			-GNinja
			-DCMAKE_PREFIX_PATH=/opt/devkitpro/portlibs/switch/
			-DCHIAKI_USE_SYSTEM_CURL=ON
			-DCHIAKI_SWITCH_RENDERER=DEKO3D
			-DCHIAKI_SWITCH_DEKO3D=ON
			-DCHIAKI_ENABLE_TESTS=OFF
			-DCHIAKI_ENABLE_CLI=OFF
			-DCHIAKI_ENABLE_GUI=OFF
			-DCHIAKI_ENABLE_ANDROID=OFF
			-DCHIAKI_ENABLE_BOREALIS=ON
			-DCHIAKI_LIB_ENABLE_MBEDTLS=ON
			-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF
			-DCHIAKI_ENABLE_STEAM_SHORTCUT=OFF
		)
		if [ -n "${toolchain}" ]; then
			cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="${toolchain}")
		fi

		cmake "${cmake_args[@]}"
			#-DCMAKE_FIND_DEBUG_MODE=OFF 
			# 
			# -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
			# -DCMAKE_FIND_DEBUG_MODE=ON

		ninja -C "${build}"
	popd
}

build_chiaki
