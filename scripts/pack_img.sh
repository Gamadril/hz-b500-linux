#!/bin/bash
#
# pack/pack
# (c) Copyright 2013 - 2016 Allwinner
# Allwinner Technology Co., Ltd. <www.allwinnertech.com>
# James Deng <csjamesdeng@allwinnertech.com>
# Trace Wong <wangyaliang@allwinnertech.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

############################ Notice #####################################
# a. Some config files priority is as follows:
#    - xxx_${platform}.{cfg|fex} > xxx.{cfg|fex}
#    - ${chip}/${board}/*.{cfg|fex} > ${chip}/default/*.{cfg|fex}
#    - ${chip}/default/*.cfg > common/imagecfg/*.cfg
#    - ${chip}/default/*.fex > common/partition/*.fex
#  e.g. sun8iw7p1/configs/perf/image_linux.cfg > sun8iw7p1/configs/default/image_linux.cfg
#       > common/imagecfg/image_linux.cfg > sun8iw7p1/configs/perf/image.cfg
#       > sun8iw7p1/configs/default/image.cfg > common/imagecfg/image.cfg
#
# b. Switch uart port
#    - Need to add your chip configs into ${LONGAN_COMMON_DIR}/debug/card_debug_pin
#    - Call pack with 'debug' parameters

enable_pause=0

function get_char()
{
	SAVEDSTTY=`stty -g`
	stty -echo
	stty cbreak
	dd if=/dev/tty bs=1 count=1 2> /dev/null
	stty -raw
	stty echo
	stty $SAVEDSTTY
}

function pause()
{
	if [ "x$1" != "x" ] ;then
		echo "$1"
	fi
	if [ $enable_pause -eq 1 ] ; then
		echo "Press any key to continue!"
		char=`get_char`
	fi
}

function pack_error()
{
	echo -e "\033[47;31mERROR: $*\033[0m"
}

function pack_warn()
{
	echo -e "\033[47;34mWARN: $*\033[0m"
}

function pack_info()
{
	echo -e "\033[47;30mINFO: $*\033[0m"
}

source scripts/shflags

# define option, format:
#   'long option' 'default value' 'help message' 'short option'
DEFINE_string 'chip' '' 'chip to build, e.g. sun7i' 'c'
DEFINE_string 'platform' '' 'platform to build, e.g. linux, android, camdroid' 'p'
DEFINE_string 'board' '' 'board to build, e.g. evb' 'b'
DEFINE_string 'kernel' '' 'kernel to build, e.g. linux-3.4, linux-3.10' 'k'
DEFINE_string 'debug_mode' 'uart0' 'config debug mode, e.g. uart0, card0' 'd'
DEFINE_string 'signture' 'none' 'pack boot signture to do secure boot' 's'
DEFINE_string 'secure' 'none' 'pack secure boot with -v arg' 'v'
DEFINE_string 'mode' 'normal' 'pack dump firmware' 'm'
DEFINE_string 'function' 'android' 'pack private firmware' 'f'
DEFINE_string 'topdir' 'none' 'sdk top dir' 't'
#create img for emmc programmer
DEFINE_string 'programmer' '' 'creat programmer img or not' 'w'
#create img for nand programmer
DEFINE_string 'tar_image' '' 'creat downloadfile img .tar.gz or not' 'i'

# parse the command-line
FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

PACK_CHIP=${FLAGS_chip}
PACK_PLATFORM=${FLAGS_platform}
PACK_BOARD=${FLAGS_board}
PACK_PLAN=${PACK_BOARD##*-}
PACK_KERN=${FLAGS_kernel}
PACK_DEBUG=${FLAGS_debug_mode}
PACK_SIG=${FLAGS_signture}
PACK_SECURE=${FLAGS_secure}
PACK_MODE=${FLAGS_mode}
PACK_FUNC=${FLAGS_function}
PACK_PROGRAMMER=${FLAGS_programmer}
PACK_TAR_IMAGE=${FLAGS_tar_image}
PACK_TOPDIR=${FLAGS_topdir}

# Only the HZ-B500 raw SD image is packed. Secure boot (TOC0/signed TOC1),
# the Phoenix/dragon container and the tar download image are not supported.
if [ "x${PACK_BOARD}" != "xd1s-hz-b500_sd" ]; then
	pack_error "pack supports only d1s-hz-b500_sd, not '${PACK_BOARD}'"
	exit 1
fi
for option in "signature:${PACK_SIG}" "secure:${PACK_SECURE}" "tar_image:${PACK_TAR_IMAGE}"; do
	case "${option#*:}" in
		""|none) ;;
		*) pack_error "unsupported pack option ${option%%:*}=${option#*:}"; exit 1 ;;
	esac
done
# One raw image for dd, with or without the legacy -w.
PACK_PROGRAMMER=programmer

function show_args
{
	echo --==========--
	echo "PACK_CHIP        " ${FLAGS_chip}
	echo "PACK_PLATFORM    " ${FLAGS_platform}
	echo "PACK_BOARD       " ${FLAGS_board}
	echo "PACK_KERN        " ${FLAGS_kernel}
	echo "PACK_DEBUG       " ${FLAGS_debug_mode}
	echo "PACK_SIG         " ${FLAGS_signture}
	echo "PACK_SECURE      " ${FLAGS_secure}
	echo "PACK_MODE        " ${FLAGS_mode}
	echo "PACK_FUNC        " ${FLAGS_function}
	echo "PACK_PROGRAMMER  " ${PACK_PROGRAMMER}
	echo "PACK_TAR_IMAGE   " ${FLAGS_tar_image}
	echo "PACK_TOPDIR      " ${FLAGS_topdir}
	echo --==========--
}
show_args

ROOT_DIR=${PACK_TOPDIR}/out/${PACK_BOARD}
# Preserve the SDK geometry for other media/profiles. SD uses its card size.
PACK_CARD_SECTORS=15269888
if [ "x${PACK_BOARD}" = "xd1s-hz-b500_sd" ]; then
	PACK_CARD_SECTORS=$(bash "${PACK_TOPDIR}/scripts/sd_card_sectors.sh" \
		"${PACK_TOPDIR}/device/config/chips/d1s/configs/hz-b500_sd/sd-card-sectors") || exit 1
	pack_info "SD card capacity limit: ${PACK_CARD_SECTORS} sectors"
fi
OTA_TEST_NAME="ota_test"
export PATH=${PACK_TOPDIR}/out/host/bin/:$PATH

if [ "x${PACK_CHIP}" = "xsun5i" ]; then
	PACK_BOARD_PLATFORM=unclear
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw5p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw6p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw7p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw8p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw11p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw10p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw15p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw17p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw18p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw20p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw21p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw19p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun3iw1p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun50iw1p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm64
elif [ "x${PACK_CHIP}" = "xsun50iw3p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm64
elif [ "x${PACK_CHIP}" = "xsun8iw12p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun8iw16p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm
elif [ "x${PACK_CHIP}" = "xsun50iw6p1" -o "x${PACK_CHIP}" = "xsun50iw9p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm64
elif [ "x${PACK_CHIP}" = "xsun50iw10p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm64
elif [ "x${PACK_CHIP}" = "xsun50iw11p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	ARCH=arm64
elif [ "x${PACK_CHIP}" = "xsun20iw1p1" ]; then
	PACK_BOARD_PLATFORM=${PACK_BOARD%%-*}
	PACK_PLAN=${PACK_BOARD#*-}
	ARCH=riscv
else
	echo "board_platform($PACK_CHIP) not support"
fi

TINA_CONFIG_DIR="${PACK_TOPDIR}/target/allwinner"
LONGAN_CONFIG_DIR="${PACK_TOPDIR}/device/config/chips/${PACK_BOARD_PLATFORM}/configs"
LONGAN_COMMON_DIR="${PACK_TOPDIR}/device/config/common"
LICHEE_CHIP_CONFIG_DIR="${PACK_TOPDIR}/device/config/chips/${PACK_BOARD_PLATFORM}"

PACK_PLATFORM_IMAGE_CFG=$PACK_PLATFORM
[ x"$PACK_PLATFORM" = x"tina" ] && PACK_PLATFORM_IMAGE_CFG="linux"

tools_file_list=(
${TINA_CONFIG_DIR}/generic/tools/split_xxxx.fex
${TINA_CONFIG_DIR}/generic/tools/usbtool_test.fex
${TINA_CONFIG_DIR}/generic/tools/usbtool_crash.fex
${TINA_CONFIG_DIR}/generic/tools/cardscript.fex
${TINA_CONFIG_DIR}/generic/tools/cardscript_secure.fex
${TINA_CONFIG_DIR}/generic/tools/cardtool.fex
${TINA_CONFIG_DIR}/generic/tools/usbtool.fex
${TINA_CONFIG_DIR}/generic/tools/aultls32.fex
${TINA_CONFIG_DIR}/generic/tools/aultools.fex

${LONGAN_COMMON_DIR}/tools/split_xxxx.fex
${LONGAN_COMMON_DIR}/tools/usbtool_test.fex
${LONGAN_COMMON_DIR}/tools/usbtool_crash.fex
${LONGAN_COMMON_DIR}/tools/cardscript.fex
${LONGAN_COMMON_DIR}/tools/cardscript_secure.fex
${LONGAN_COMMON_DIR}/tools/cardtool.fex
${LONGAN_COMMON_DIR}/tools/usbtool.fex
${LONGAN_COMMON_DIR}/tools/aultls32.fex
${LONGAN_COMMON_DIR}/tools/aultools.fex

${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/split_xxxx.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/usbtool_test.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/usbtool_crash.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/cardscript.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/cardscript_secure.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/cardtool.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/usbtool.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/aultls32.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/aultools.fex

${LONGAN_CONFIG_DIR}/../tools/split_xxxx.fex
${LONGAN_CONFIG_DIR}/../tools/usbtool_test.fex
${LONGAN_CONFIG_DIR}/../tools/usbtool_crash.fex
${LONGAN_CONFIG_DIR}/../tools/cardscript.fex
${LONGAN_CONFIG_DIR}/../tools/cardscript_secure.fex
${LONGAN_CONFIG_DIR}/../tools/cardtool.fex
${LONGAN_CONFIG_DIR}/../tools/usbtool.fex
${LONGAN_CONFIG_DIR}/../tools/aultls32.fex
${LONGAN_CONFIG_DIR}/../tools/aultools.fex
)

configs_file_list=(
${TINA_CONFIG_DIR}/generic/toc/boot_package.fex
${TINA_CONFIG_DIR}/generic/dtb/sunxi.fex
${TINA_CONFIG_DIR}/generic/configs/*.fex
${TINA_CONFIG_DIR}/generic/configs/*.cfg
${TINA_CONFIG_DIR}/generic/version/version_base.mk

${LONGAN_COMMON_DIR}/toc/*.fex
${LONGAN_COMMON_DIR}/hdcp/esm.fex
${LONGAN_COMMON_DIR}/dtb/sunxi.fex
${LONGAN_COMMON_DIR}/imagecfg/*.cfg
${LONGAN_COMMON_DIR}/partition/sys_partition_dump.fex
${LONGAN_COMMON_DIR}/partition/sys_partition_private.fex
${LONGAN_COMMON_DIR}/version/version_base.mk

${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/configs/*.fex
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/configs/*.cfg
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/configs/*.its
${TINA_CONFIG_DIR}/${PACK_BOARD}/configs/*.fex
${TINA_CONFIG_DIR}/${PACK_BOARD}/configs/*.cfg

${LONGAN_CONFIG_DIR}/default/*.fex
${LONGAN_CONFIG_DIR}/default/*.cfg
${LONGAN_CONFIG_DIR}/default/*.its
${LONGAN_CONFIG_DIR}/default/version_base.mk
${LONGAN_CONFIG_DIR}/${PACK_PLAN}/*.fex
${LONGAN_CONFIG_DIR}/${PACK_PLAN}/*.cfg
${LONGAN_CONFIG_DIR}/${PACK_PLAN}/linux/*.fex
${LONGAN_CONFIG_DIR}/${PACK_PLAN}/linux/*.cfg
)

boot_resource_list=(
${TINA_CONFIG_DIR}/generic/boot-resource/boot-resource:image/
${TINA_CONFIG_DIR}/generic/boot-resource/boot-resource.ini:image/
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/boot-resource:image/
${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/boot-resource.ini:image/
${TINA_CONFIG_DIR}/${PACK_BOARD}/configs/*.bmp:image/boot-resource/

${LONGAN_CONFIG_DIR}/default/boot-resource:image/
${LONGAN_CONFIG_DIR}/default/boot-resource.ini:image/
${LONGAN_CONFIG_DIR}/${PACK_PLAN}/configs/*.bmp:image/boot-resource/
)

possible_bin_path=(
bin
configs/${PACK_PLAN}/bin
configs/${PACK_PLAN}/${PACK_PLATFORM_IMAGE_CFG}/bin
)

boot_file_list=(
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/boot0_nand_${PACK_CHIP}.bin:${ROOT_DIR}/image/boot0_nand.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/boot0_sdcard_${PACK_CHIP}.bin:${ROOT_DIR}/image/boot0_sdcard.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/boot0_spinor_${PACK_CHIP}.bin:${ROOT_DIR}/image/boot0_spinor.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/fes1_${PACK_CHIP}.bin:${ROOT_DIR}/image/fes1.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/bl31.bin:${ROOT_DIR}/image/monitor.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/scp.bin:${ROOT_DIR}/image/scp.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/optee_${PACK_CHIP}.bin:${ROOT_DIR}/image/optee.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/opensbi_${PACK_CHIP}.bin:${ROOT_DIR}/image/opensbi.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/boot0_nand_${PACK_CHIP}-${OTA_TEST_NAME}.bin:${ROOT_DIR}/image/boot0_nand-${OTA_TEST_NAME}.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/boot0_sdcard_${PACK_CHIP}-${OTA_TEST_NAME}.bin:${ROOT_DIR}/image/boot0_sdcard-${OTA_TEST_NAME}.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/boot0_spinor_${PACK_CHIP}-${OTA_TEST_NAME}.bin:${ROOT_DIR}/image/boot0_spinor-${OTA_TEST_NAME}.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/dsp0.bin:${ROOT_DIR}/image/dsp0.fex
${LONGAN_CONFIG_DIR}/../\${BIN_PATH}/dsp1.bin:${ROOT_DIR}/image/dsp1.fex
)

function show_boards()
{
	printf "\nAll avaiable chips, platforms and boards:\n\n"
	printf "Chip            Board\n"
	for chipdir in $(find chips/ -mindepth 1 -maxdepth 1 -type d) ; do
		chip=`basename ${chipdir}`
		printf "${chip}\n"
		for boarddir in $(find chips/${chip}/configs/${platform} \
			-mindepth 1 -maxdepth 1 -type d) ; do
			board=`basename ${boarddir}`
			printf "                ${board}\n"
		done
	done
	printf "\nFor Usage:\n"
	printf "     $(basename $0) -h\n\n"
}

function do_platform_pack_hook()
{
	PWD=$(pwd)
	if [ -e ${TINA_CONFIG_DIR}/${PACK_BOARD}/hooks/pack_${1}_hook.sh ] ; then
		pack_info "--Call platform pack ${1} hook now--"
		cd ${TINA_CONFIG_DIR}/${PACK_BOARD}/hooks/
		./pack_${1}_hook.sh
		cd ${PWD}
	fi
}

function call_platform_pack_hook()
{
	hook_type=$1
	do_platform_pack_hook $hook_type
}

function call_platform_pack_prepare_hook()
{
	call_platform_pack_hook prepare
}

function uart_switch()
{
	local DEBUG_DIR="${LONGAN_COMMON_DIR}/debug"

	rm -rf ${ROOT_DIR}/image/awk_debug_card0
	touch ${ROOT_DIR}/image/awk_debug_card0
	TX=`awk  '$0~"'$PACK_CHIP'"{print $2}' ${DEBUG_DIR}/card_debug_pin`
	RX=`awk  '$0~"'$PACK_CHIP'"{print $3}' ${DEBUG_DIR}/card_debug_pin`
	PORT=`awk  '$0~"'$PACK_CHIP'"{print $4}' ${DEBUG_DIR}/card_debug_pin`
	MS=`awk  '$0~"'$PACK_CHIP'"{print $5}' ${DEBUG_DIR}/card_debug_pin`
	CK=`awk  '$0~"'$PACK_CHIP'"{print $6}' ${DEBUG_DIR}/card_debug_pin`
	DO=`awk  '$0~"'$PACK_CHIP'"{print $7}' ${DEBUG_DIR}/card_debug_pin`
	DI=`awk  '$0~"'$PACK_CHIP'"{print $8}' ${DEBUG_DIR}/card_debug_pin`

	BOOT_UART_ST=`awk  '$0~"'$PACK_CHIP'"{print $2}' ${DEBUG_DIR}/card_debug_string`
	BOOT_PORT_ST=`awk  '$0~"'$PACK_CHIP'"{print $3}' ${DEBUG_DIR}/card_debug_string`
	BOOT_TX_ST=`awk  '$0~"'$PACK_CHIP'"{print $4}' ${DEBUG_DIR}/card_debug_string`
	BOOT_RX_ST=`awk  '$0~"'$PACK_CHIP'"{print $5}' ${DEBUG_DIR}/card_debug_string`
	UART0_ST=`awk  '$0~"'$PACK_CHIP'"{print $6}' ${DEBUG_DIR}/card_debug_string`
	UART0_USED_ST=`awk  '$0~"'$PACK_CHIP'"{print $7}' ${DEBUG_DIR}/card_debug_string`
	UART0_PORT_ST=`awk  '$0~"'$PACK_CHIP'"{print $8}' ${DEBUG_DIR}/card_debug_string`
	UART0_TX_ST=`awk  '$0~"'$PACK_CHIP'"{print $9}' ${DEBUG_DIR}/card_debug_string`
	UART0_RX_ST=`awk  '$0~"'$PACK_CHIP'"{print $10}' ${DEBUG_DIR}/card_debug_string`
	UART1_ST=`awk  '$0~"'$PACK_CHIP'"{print $11}' ${DEBUG_DIR}/card_debug_string`
	JTAG_ST=`awk  '$0~"'$PACK_CHIP'"{print $12}' ${DEBUG_DIR}/card_debug_string`
	MS_ST=`awk  '$0~"'$PACK_CHIP'"{print $13}' ${DEBUG_DIR}/card_debug_string`
	CK_ST=`awk  '$0~"'$PACK_CHIP'"{print $14}' ${DEBUG_DIR}/card_debug_string`
	DO_ST=`awk  '$0~"'$PACK_CHIP'"{print $15}' ${DEBUG_DIR}/card_debug_string`
	DI_ST=`awk  '$0~"'$PACK_CHIP'"{print $16}' ${DEBUG_DIR}/card_debug_string`
	MMC0_ST=`awk  '$0~"'$PACK_CHIP'"{print $17}' ${DEBUG_DIR}/card_debug_string`
	MMC0_USED_ST=`awk  '$0~"'$PACK_CHIP'"{print $18}' ${DEBUG_DIR}/card_debug_string`

	echo '$0!~";" && $0~"'$BOOT_TX_ST'"{if(C)$0="'$BOOT_TX_ST' = '$TX'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$BOOT_RX_ST'"{if(C)$0="'$BOOT_RX_ST' = '$RX'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$BOOT_PORT_ST'"{if(C)$0="'$BOOT_PORT_ST' = '$PORT'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	if [ "`grep "auto_print_used" "${ROOT_DIR}/image//sys_config.fex" | grep "1"`" ]; then
		echo '$0!~";" && $0~"'$MMC0_USED_ST'"{if(A)$0="'$MMC0_USED_ST' = 1";A=0} \' >> ${ROOT_DIR}/image/awk_debug_card0
	else
		echo '$0!~";" && $0~"'$MMC0_USED_ST'"{if(A)$0="'$MMC0_USED_ST' = 0";A=0} \' >> ${ROOT_DIR}/image/awk_debug_card0
	fi
	echo '$0!~";" && $0~"\\['$MMC0_ST'\\]"{A=1}  \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$UART0_TX_ST'"{if(B)$0="'$UART0_TX_ST' = '$TX'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$UART0_RX_ST'"{if(B)$0="'$UART0_RX_ST' = '$RX'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"\\['$UART0_ST'\\]"{B=1} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$UART0_USED_ST'"{if(B)$0="'$UART0_USED_ST' = 1"}  \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '/^'$UART0_PORT_ST'/{next} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"\\['$UART1_ST'\\]"{B=0} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"\\['$BOOT_UART_ST'\\]"{C=1} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"\\['$JTAG_ST'\\]"{C=0} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$MS_ST'"{$0="'$MS_ST' = '$MS'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$CK_ST'"{$0="'$CK_ST' = '$CK'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$DO_ST'"{$0="'$DO_ST' = '$DO'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '$0!~";" && $0~"'$DI_ST'"{$0="'$DI_ST' = '$DI'"} \' >> ${ROOT_DIR}/image/awk_debug_card0
	echo '1' >> ${ROOT_DIR}/image/awk_debug_card0

	if [ "`grep "auto_print_used" "${ROOT_DIR}/image/sys_config.fex" | grep "1"`" ]; then
		sed -i -e '/^uart0_rx/a\pinctrl-1=' ${ROOT_DIR}/image/sys_config.fex
		sed -i -e '/^uart0_rx/a\pinctrl-0=' ${ROOT_DIR}/image/sys_config.fex
	fi
	awk -f ${ROOT_DIR}/image/awk_debug_card0 ${ROOT_DIR}/image/sys_config.fex > ${ROOT_DIR}/image/sys_config_debug.fex
	rm -f ${ROOT_DIR}/image/sys_config.fex
	mv ${ROOT_DIR}/image/sys_config_debug.fex ${ROOT_DIR}/image/sys_config.fex
	echo "uart -> card0"
}

function copy_ota_test_file()
{
	printf "ota test bootloader by diff bootlogo\n"
	mv ${ROOT_DIR}/image/boot-resource/bootlogo_ota_test.bmp ${ROOT_DIR}/image/boot-resource/bootlogo.bmp

	printf "copying ota test boot file\n"
	mv ${ROOT_DIR}/image/boot0_nand-${OTA_TEST_NAME}.fex		${ROOT_DIR}/image/boot0_nand.fex
	mv ${ROOT_DIR}/image/boot0_sdcard-${OTA_TEST_NAME}.fex	${ROOT_DIR}/image/boot0_sdcard.fex
}


function make_boottone_fex()
{
	local BOOTTONE_FILE_PATH=${TINA_CONFIG_DIR}/${PACK_BOARD}/configs/boottone_res
	if [ -e $BOOTTONE_FILE_PATH ];then
		echo "make boottone.fex"
		${PACK_TOPDIR}/out/host/bin/make_ext4fs -l 512k -b 1024 -m 0 -J ${ROOT_DIR}/image/boottone.fex $BOOTTONE_FILE_PATH
	fi
}

ENV_SUFFIX=

function get_kernel
{
	local f="${TINA_CONFIG_DIR}/${PACK_BOARD}/Makefile"
	[ -f "$f" ] || return -1

	awk -F":=" '/KERNEL_PATCHVER/{print $2}' $f
}

function do_prepare()
{
	if [ -z "${PACK_CHIP}" -o -z "${PACK_PLATFORM}" -o -z "${PACK_BOARD}" ] ; then
		pack_error "Invalid parameters Chip: ${PACK_CHIP}, \
			Platform: ${PACK_PLATFORM}, Board: ${PACK_BOARD}"
		show_boards
		exit 1
	fi

	[ -d ${TINA_CONFIG_DIR}/${PACK_BOARD}/configs -o -d ${LONGAN_CONFIG_DIR}/${PACK_PLAN}/linux ] || {
		pack_error "Board's directory \
			\"${TINA_CONFIG_DIR}/${PACK_BOARD}/configs or ${LONGAN_CONFIG_DIR}/${PACK_PLAN}/linux\" not exist."
		show_boards
		exit 1
	}

	if [ -z "${PACK_KERN}" ] ; then
		printf "No kernel param, parse it from ${PACK_BOARD_PLATFORM}\n"
		[ "x$(get_kernel)" != "x" ] && {
			PACK_KERN="linux-$(get_kernel)"
			ENV_SUFFIX=$(get_kernel)
		}
		if [ -z "${PACK_KERN}" ] ; then
			pack_error "Failed to parse kernel param from ${PACK_BOARD_PLATFORM}"
			exit 1
		fi
	fi

	# Cleanup
	rm -rf ${ROOT_DIR}/image
	mkdir -p ${ROOT_DIR}/image

	call_platform_pack_prepare_hook

	printf "copying tools file\n"
	for file in ${tools_file_list[@]} ; do
		cp -f $file ${ROOT_DIR}/image/ 2> /dev/null
	done

	if [ "x${PACK_KERN}" = "xlinux-3.4" ]; then
		if [ -f ${LONGAN_COMMON_DIR}/tools/cardscript.fex ]; then
			cp -f ${LONGAN_COMMON_DIR}/tools/cardscript.fex ${ROOT_DIR}/image/ 2> /dev/null
		else
			cp -f ${TINA_CONFIG_DIR}/generic/tools/cardscript.fex ${ROOT_DIR}/image/ 2> /dev/null
		fi
	fi

	printf "copying configs file\n"
	for file in ${configs_file_list[@]} ; do
		cp -f $file ${ROOT_DIR}/image/ 2> /dev/null
	done

	if [ -f ${ROOT_DIR}/image/sys_config_${PACK_KERN}.fex ]; then
		cp ${ROOT_DIR}/image/sys_config_${PACK_KERN}.fex ${ROOT_DIR}/image/sys_config.fex
	fi

	# get storage_type value from sys_config.fex
	if [ ! -f ${ROOT_DIR}/image/sys_config.fex ];then
		echo "sys_config.fex is not exist."
		exit 1
	fi
	# If platform config files exist, we will cover the default files
	# For example, mv out/image_linux.cfg out/image.cfg
	cd ${ROOT_DIR}
	find image/* -type f -a \( -name "*.fex" -o -name "*.cfg" \) -print | \
		sed "s#\(.*\)_${PACK_PLATFORM_IMAGE_CFG}\(\..*\)#mv -fv & \1\2#e" > /dev/null
	cd - > /dev/null

	image_instruction="image is for nand/emmc"

	if [ "x${PACK_MODE}" = "xdump" ] ; then
		cp -vf ${ROOT_DIR}/image/sys_partition_dump.fex ${ROOT_DIR}/image/sys_partition.fex
		cp -vf ${ROOT_DIR}/image/usbtool_test.fex ${ROOT_DIR}/image/usbtool.fex
	elif [ "x${PACK_FUNC}" = "xprvt" ] ; then
		cp -vf ${ROOT_DIR}/image/sys_partition_private.fex ${ROOT_DIR}/image/sys_partition.fex
	fi

	grep "CONFIG_USE_DM_VERITY=y" ${PACK_TOPDIR}/.config > /dev/null
	if [ $? -eq 0 ]; then
		cp -vf ${ROOT_DIR}/image/sys_partition_secure.fex ${ROOT_DIR}/image/sys_partition.fex
	fi

	printf "copying boot resource\n"
	for file in ${boot_resource_list[@]} ; do
		cp -rf `echo $file | awk -F: '{print $1}'` \
			${ROOT_DIR}/`echo $file | awk -F: '{print $2}'` 2>/dev/null
	done

	lzma e ${ROOT_DIR}/image/boot-resource/bootlogo.bmp ${ROOT_DIR}/image/bootlogo.bmp.lzma

	printf "copying boot file\n"
	for d in ${possible_bin_path[@]}; do
		[ ! -d ${LICHEE_CHIP_CONFIG_DIR}/$d ]  && continue
		BIN_PATH=$d
		for file in ${boot_file_list[@]} ; do
			eval cp -f $(echo $file | sed -e 's/:/ /g') 2>/dev/null
		done
	done

	[ -z "${BIN_PATH}" ] &&
	pack_error "No BIN_PATH found!" && exit 1

	# The Boot0/FES1 build writes only to out/<board>; the tracked binaries in
	# the chip bin directory are the fallback when Boot0 was not built.
	for name in boot0_nand boot0_sdcard boot0_spinor fes1; do
		[ -f ${ROOT_DIR}/${name}_${PACK_CHIP}.bin ] &&
			cp -f ${ROOT_DIR}/${name}_${PACK_CHIP}.bin ${ROOT_DIR}/image/${name}.fex
	done

	# If longan platform config use
	if [ -f ${LONGAN_CONFIG_DIR}/../tools/plat_config.sh ] ; then
		${LONGAN_CONFIG_DIR}/../tools/plat_config.sh
	fi
	# If tina platform config use
	if [ -f ${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/plat_config.sh ] ; then
		${TINA_CONFIG_DIR}/${PACK_BOARD_PLATFORM}-common/tools/plat_config.sh
	fi


	# Secure boot is not supported: make sure sys_config does not request it.
	sed -i '/^burn_secure_mod/d' ${ROOT_DIR}/image/sys_config.fex
	sed -i '/^secure_without_OS/d' ${ROOT_DIR}/image/sys_config.fex

	if [ "x${PACK_MODE}" = "xota_test" ] ; then
		printf "copy ota test file\n"
		copy_ota_test_file
	fi

	if [ "x${PACK_PROGRAMMER}" = "xprogrammer" ]; then
		printf "add programmer img info target in sys config\n"
		sed -i -e '/^\[target\]/a\programmer=1' ${ROOT_DIR}/image/sys_config.fex
	fi

	# Here, we can switch uart to card or normal
	if [ "x${PACK_DEBUG}" = "xcard0" -a "x${PACK_MODE}" != "xdump" \
		-a "x${PACK_FUNC}" != "xprvt" ] ; then \
		uart_switch
	else
		sed -i -e '/^auto_print_used/s\1\0\' ${ROOT_DIR}/image/sys_config.fex
	fi

	sed -i 's/\\boot-resource/\/boot-resource/g' ${ROOT_DIR}/image/boot-resource.ini

	IMG_NAME="${PACK_PLATFORM}_${PACK_BOARD}_${PACK_DEBUG}"

	if [ "x${PACK_MODE}" = "xdump" -o "x${PACK_MODE}" = "xota_test" -o "x${PACK_MODE}" = "xcrashdump" ] ; then
		IMG_NAME="${IMG_NAME}_${PACK_MODE}"
	fi

	if [ "x${PACK_FUNC}" = "xprvt" -o "x${PACK_FUNC}" = "xprev_refurbish" ] ; then
		IMG_NAME="${IMG_NAME}_${PACK_FUNC}"
	fi

	IMG_NAME="${IMG_NAME}${IMG_FLASH}.img"
	# The raw SD image is the only output; it keeps the plain image name.
	IMG_PROGRAMMER_NAME="${IMG_NAME}"


	make_boottone_fex
}

function mkdtbo()
{
	local DTBO_DIR=${LONGAN_CONFIG_DIR}/${PACK_PLAN}/dtbo

	[ ! -d "${DTBO_DIR}" ] && DTBO_DIR=${LONGAN_CONFIG_DIR}/../dtbo

	if [ -d $DTBO_DIR ];  then
		echo "build dtbo ..."
		local DTC_FLAGS="-W no-unit_address_vs_reg"
		local DTS_DIR=${DTBO_DIR}
		local DTO_COMPILER=${DTBO_DIR}/dtco

		if [ ! -f $DTO_COMPILER ]; then
			echo "mkdtbo: Can not find dtco compiler."
			exit 1
		fi

		local out_file_name=0
		for dts_file in ${DTS_DIR}/*.dts; do
			out_file_name=${dts_file%.*}
			$DTO_COMPILER ${DTC_FLAGS} -a 4 -@ -O dtb -o ${out_file_name}.dtbo ${dts_file}
			if [ $? -ne 0 ]; then
				echo "mkdtbo:create dtbo file failed"
				exit 1
			fi
		done

		local MKDTIMG=$DTBO_DIR/mkdtimg
		local DTBOIMG_CFG_FILE=$DTBO_DIR/dtboimg.cfg
		local DTBOIMG_OUT_DIR=${ROOT_DIR}/image
		if [ -f ${MKDTIMG} ]; then
			if [ -f ${DTBOIMG_CFG_FILE} ]; then
				echo "mkdtbo: make  dtboimg start."
				cd ${DTS_DIR}
				${MKDTIMG} cfg_create ${DTBOIMG_OUT_DIR}/dtbo.img ${DTBOIMG_CFG_FILE}
				${MKDTIMG} dump ${DTBOIMG_OUT_DIR}/dtbo.img
			else
				echo "mkdtbo: Can not find dtboimg.cfg"
				exit 1
			fi
		else
			echo "mkdtbo: Can not find mkdtimg"
			exit 1
		fi

		mv *.dtbo ${ROOT_DIR}/image/
		cd -
	else
		echo "don't build dtbo ..."
	fi
}

function do_ini_to_dts()
{
	if [ "x${PACK_KERN}" == "xlinux-3.4" ]; then
		return
	fi
	if [ "x${PACK_KERN}" == "xlinux-4.4" -a "x${ARCH}" == "xarm64" ]; then
		local extra_path=sunxi
	elif [ "x${PACK_KERN}" == "xlinux-4.9" -a "x${ARCH}" == "xarm64" ]; then
		local extra_path=sunxi
	elif [ "x${PACK_KERN}" == "xlinux-5.4" -a "x${ARCH}" == "xriscv" ]; then
		local extra_path=sunxi
	else
		local extra_path=
	fi

	local DTC_SRC_PATH=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/${extra_path}/
	local DTC_DEP_FILE=${DTC_SRC_PATH}/.${PACK_CHIP}-${PACK_BOARD}.dtb.d.dtc.tmp
	local DTC_SRC_FILE=${DTC_SRC_PATH}/.${PACK_CHIP}-${PACK_BOARD}.dtb.dts.tmp

	local DTC_DEP_BOARD1=${DTC_SRC_PATH}/.board.dtb.d.dtc.tmp
	local DTC_SRC_BOARD1=${DTC_SRC_PATH}/.board.dtb.dts.tmp
	if [ -f ${DTC_DEP_BOARD1} -a -f ${DTC_SRC_BOARD1} ]; then
		DTC_DEP_FILE=${DTC_DEP_BOARD1}
		DTC_SRC_FILE=${DTC_SRC_BOARD1}
	fi
	local DTC_COMPILER=${PACK_TOPDIR}/lichee/$PACK_KERN/scripts/dtc/dtc
	local DTC_INI_FILE_BASE=${ROOT_DIR}/image/sys_config.fex
	local DTC_INI_FILE=${ROOT_DIR}/image/sys_config_fix.fex
	local DTC_FLAGS=""

	#export symbols
	if [ "x${PACK_KERN}" == "xlinux-4.9" ]; then
		DTC_FLAGS="-@"
	fi

	if [ "x${PACK_KERN}" == "xlinux-5.4" ]; then
		cp ${DTC_SRC_PATH}/board.dtb ${ROOT_DIR}/image/sunxi.dtb
		$DTC_COMPILER  ${DTC_FLAGS} -I dtb -O dts -o ${ROOT_DIR}/image/.sunxi.dts ${ROOT_DIR}/image/sunxi.dtb &> /dev/null
		return
	fi

	cp $DTC_INI_FILE_BASE $DTC_INI_FILE
	sed -i "s/\(\[dram\)_para\(\]\)/\1\2/g" $DTC_INI_FILE
	sed -i "s/\(\[nand[0-9]\)_para\(\]\)/\1\2/g" $DTC_INI_FILE

	if [ ! -f $DTC_COMPILER ]; then
		pack_error "Script_to_dts: Can not find dtc compiler.\n"
		exit 1
	fi
	if [ ! -f $DTC_DEP_FILE ]; then
		printf "Script_to_dts: Can not find [%s-%s.dts]. Will use common dts file instead.\n" ${PACK_CHIP} ${PACK_BOARD}
		if [ "x${PACK_BOARD_PLATFORM}" = "xr6" ] ; then
			DTC_DEP_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/.${PACK_CHIP}-r6-soc.dtb.d.dtc.tmp
			DTC_SRC_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/.${PACK_CHIP}-r6-soc.dtb.dts.tmp
		elif [ "x${PACK_BOARD_PLATFORM}" = "xc200s" -o "x${PACK_BOARD_PLATFORM}" = "xv133" -o "x${PACK_BOARD_PLATFORM}" = "xc600" ] ; then
				DTC_DEP_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/.${PACK_CHIP}-${PACK_BOARD}.dtb.d.dtc.tmp
				DTC_SRC_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/.${PACK_CHIP}-${PACK_BOARD}.dtb.dts.tmp
		else
			if [ "x${PACK_KERN}" == "xlinux-4.4" -a "x${ARCH}" == "xarm64" ]; then
				DTC_DEP_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/sunxi/.${PACK_CHIP}-soc.dtb.d.dtc.tmp
				DTC_SRC_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/sunxi/.${PACK_CHIP}-soc.dtb.dts.tmp
			elif [ "x${PACK_KERN}" == "xlinux-4.9" -a "x${ARCH}" == "xarm64" ]; then
				DTC_DEP_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/sunxi/.${PACK_CHIP}-soc.dtb.d.dtc.tmp
				DTC_SRC_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/sunxi/.${PACK_CHIP}-soc.dtb.dts.tmp
			else
				DTC_DEP_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/.${PACK_CHIP}-soc.dtb.d.dtc.tmp
				DTC_SRC_FILE=${PACK_TOPDIR}/lichee/$PACK_KERN/arch/$ARCH/boot/dts/.${PACK_CHIP}-soc.dtb.dts.tmp
			fi
		fi
		#Disbale noisy checks
		if [[ "x${PACK_KERN}" == "xlinux-4.9" || "x${PACK_KERN}" == "xlinux-5.4" ]]; then
			DTC_FLAGS="-W no-unit_address_vs_reg -@"
		fi
	fi

	if [ "x${PACK_KERN}" == "xlinux-5.4" -a "x${ARCH}" == "xriscv" ]; then
		$DTC_COMPILER ${DTC_FLAGS} -O dtb -o ${ROOT_DIR}/image/sunxi.dtb	\
			-b 0			\
			-i $DTC_SRC_PATH	\
			-d $DTC_DEP_FILE $DTC_SRC_FILE
	else
		$DTC_COMPILER ${DTC_FLAGS} -O dtb -o ${ROOT_DIR}/image/sunxi.dtb	\
			-b 0			\
			-i $DTC_SRC_PATH	\
			-F $DTC_INI_FILE	\
			-d $DTC_DEP_FILE $DTC_SRC_FILE
	fi

	if [ $? -ne 0 ]; then
		pack_error "Conver script to dts failed"
		exit 1
	fi

	printf "Conver script to dts ok.\n"

	# It'is used for debug dtb
	$DTC_COMPILER  ${DTC_FLAGS} -I dtb -O dts -o ${ROOT_DIR}/image/.sunxi.dts ${ROOT_DIR}/image/sunxi.dtb &> /dev/null

	return
}


function do_common()
{
	cd "${ROOT_DIR}/image" || exit 1

	if [ -f sunxi.dtb ]; then
		bash "${PACK_TOPDIR}/scripts/pad_dtb.sh" sunxi.dtb sunxi.fex || exit 1
	fi

	# BROM runs Boot0 before DRAM exists, so Boot0 reads its DRAM, UART and SD
	# settings from its own header. Only the SD Boot0 reaches the card; the
	# vendor script compiler, update_boot0/update_chip/update_fes1, the NAND
	# Boot0 and FES1 (USB flashing) are not needed for the raw SD image.
	[ -f boot0_sdcard.fex ] || { pack_error "boot0_sdcard.fex is missing"; exit 1; }
	"${PACK_TOPDIR}/scripts/hz-pack" boot0-header boot0_sdcard.fex sys_config.fex || exit 1
	echo "Direct SD boot: no boot-resource partition"

	bash "${PACK_TOPDIR}/scripts/mk_direct_payload.sh" "${PACK_BOARD}" || exit 1
	"${PACK_TOPDIR}/scripts/hz-pack" toc1 boot_package.cfg boot_package.fex || exit 1
}

function img_to_programmer()
{
	# Writes the raw SD image, including its boot0/TOC1 prefix and MBR.
	bash "${PACK_TOPDIR}/scripts/mk_minimal_sd_image.sh" "${ROOT_DIR}/image" \
		"$1" "${PACK_CARD_SECTORS}"
}


function do_finish()
{
	# No legacy MBR/GPT (with its automatic UDISK), tinaconf archive or
	# Phoenix/dragon container: mk_minimal_sd_image.sh assembles the raw SD
	# image with its own two-partition MBR.
	do_check_pack_file

	echo "creating SD image..."
	if ! img_to_programmer "${IMG_PROGRAMMER_NAME}"; then
		pack_error "SD image creation failed"
		exit 1
	fi
	if [ ! -f "${IMG_PROGRAMMER_NAME}" ]; then
		pack_error "SD image was not created"
		exit 1
	fi
	mv "${IMG_PROGRAMMER_NAME}" "../${IMG_PROGRAMMER_NAME}" || exit 1
	echo '----------SD image for dd is at----------'
	echo -e '\033[0;31;1m'
	echo ${ROOT_DIR}/${IMG_PROGRAMMER_NAME}
	echo -e '\033[0m'

	cd ..
	printf "pack finish\n"
}

# In order to adapt to multiple platforms,
# some files are allowed to be invalid files when making images
# But some files cannot be invalid files at the same time
# This function is mainly to check the situation that those files
# cannot be invalid files at the same time
function do_check_pack_file()
{
	local boot0_nand_valid_flag=0
	local boot0_sdcard_valid_flag=0
	local boot0_spinor_valid_flag=0
	local boot0_nand_size=0
	local boot0_sdcard_size=0
	local boot0_spinor_size=0
	local valid_file_siez=1024
	if [ -e boot0_nand.fex ]; then
		boot0_nand_size=`ls -l boot0_nand.fex | awk '{ print $5 }'`
		if [ $boot0_nand_size -gt $valid_file_siez ]; then
			boot0_nand_valid_flag=1
		fi
	fi
	if [ -e boot0_sdcard.fex ]; then
		boot0_sdcard_size=`ls -l boot0_sdcard.fex | awk '{ print $5 }'`
		if [ $boot0_sdcard_size -gt $valid_file_siez ]; then
			boot0_sdcard_valid_flag=1
		fi
	fi
	if [ -e boot0_spinor.fex ]; then
		boot0_spinor_size=`ls -l boot0_spinor.fex | awk '{ print $5 }'`
		if [ $boot0_spinor_size -gt $valid_file_siez ]; then
			boot0_spinor_valid_flag=1
		fi
	fi
	if [ $boot0_nand_valid_flag == "0" ] && \
		[ $boot0_sdcard_valid_flag == "0" ] && \
		[ $boot0_spinor_valid_flag == "0" ]; then
		pack_error "boot0 files are all invalid files, now exit"
		exit 1
	fi
}

function do_pack_tina()
{
	printf "packing for tina linux\n"

	rm -rf vmlinux.fex
	rm -rf boot.fex
	rm -rf rootfs.fex
	rm -rf kernel.fex
	rm -rf rootfs_squashfs.fex
	rm -rf usr.fex
	rm -rf recovery.fex
	rm -rf rootfs-extract.fex
	#ln -s ${ROOT_DIR}/vmlinux.tar.bz2 vmlinux.fex
	if [ ! -f vmlinux.fex ]; then
		echo "vmlinux" > vmlinux.fex
	fi


	if [ -f ${ROOT_DIR}/boot_initramfs.img ]; then
		ln -s ${ROOT_DIR}/boot_initramfs.img        boot.fex
	else
		ln -s ${ROOT_DIR}/boot.img        boot.fex
	fi
	ln -s boot.fex        kernel.fex

	if [ -f ${ROOT_DIR}/${PACK_BOARD}-Image.gz ] ; then
		cp ${ROOT_DIR}/${PACK_BOARD}-Image.gz Image.gz
	fi


	ln -s ${ROOT_DIR}/rootfs.img     rootfs.fex
	[ -f dtbo.img ] &&
		ln -s dtbo.img        dtbo.fex

	# Some vendor sys_partition.fex files name rootfs-ubifs.fex.
	ln -s rootfs.fex rootfs-ubifs.fex

	if [ -f ${ROOT_DIR}/usr.img ]; then
		ln -s ${ROOT_DIR}/usr.img    usr.fex
	fi

	if [ -f ${ROOT_DIR}/boot_initramfs_recovery.img ]; then
		ln -s ${ROOT_DIR}/boot_initramfs_recovery.img recovery.fex
	else
		touch recovery.fex
		echo "recovery part not used!" > recovery.fex
	fi

	# add for dm-verity block
	grep "CONFIG_USE_DM_VERITY=y" ${PACK_TOPDIR}/.config > /dev/null
	if [ $? -eq 0 ]; then
		cp -vf ${ROOT_DIR}/rootfs.img rootfs.fex
		${PACK_TOPDIR}/scripts/dm-verity-block.sh ${ROOT_DIR}/image/rootfs.fex
		if [ $? -ne 0 ]; then
			echo "error: generate verity block error!"
			exit
		fi
	fi
}

function do_mkbootimg_add_dtb()
{
	if [ "x${ARCH}" = "xarm64" ]; then
		local LOAD_ADDRESS=0x40080000
		local ENTRY_POINT=0x40080000
		local BASE_ADDRESS=0x40000000
		local KERNEL_OFFSET=0x80000
		local IMAGE_DATA=Image.gz
		local BOOTIMG_IMAGE_DATA=bImage
	elif "x${ARCH}" == "xriscv" ]; then
		local LOAD_ADDRESS=0x40080000
		local ENTRY_POINT=0x40080000
		local BASE_ADDRESS=0x40000000
		local KERNEL_OFFSET=0x0
		local IMAGE_DATA=Image.gz
		local BOOTIMG_IMAGE_DATA=bImage
	else
		local LOAD_ADDRESS=0x40008000
		local ENTRY_POINT=0x40008000
		local BASE_ADDRESS=0x40000000
		local KERNEL_OFFSET=0x8000
		local IMAGE_DATA=zImage
		local BOOTIMG_IMAGE_DATA=zImage
	fi
	local RAMDISK_OFFSET=0x01000000
	local DTB_OFFSET=0x01008000
	local RAMDISK=ramdisk.img
	local DTB=sunxi.dtb
	local UIMAGE_NAME=uImage
	local BOOTIMG_NAME=boot.img
	local KDIR=${ROOT_DIR}/compile_dir/target/linux-${PACK_BOARD}
	# it is a fake ramdisk, because the Android image must have a ramdisk
	local RDIR=${TINA_CONFIG_DIR}/generic/image
	local DDIR=${ROOT_DIR}/image
	# Only version 2 can add dtb
	local HEADER_VERSION=0x2

	echo "====================mkbootimg add dtb start===================="
	if [ -e ${ROOT_DIR}/boot.img ]; then
		rm ${ROOT_DIR}/boot.img
	fi
	echo "mkbootimg --kernel ${KDIR}/${BOOTIMG_IMAGE_DATA} --ramdisk ${RDIR}/${RAMDISK} --dtb ${DTB} --board ${PACK_BOARD} --base ${BASE_ADDRESS} --kernel_offset ${KERNEL_OFFSET} --ramdisk_offse ${RAMDISK_OFFSET} --dtb_offset ${DTB_OFFSET} --header_version ${HEADER_VERSION} -o ${ROOT_DIR}/boot.img"
	mkbootimg --kernel ${KDIR}/${BOOTIMG_IMAGE_DATA} --ramdisk ${RDIR}/${RAMDISK} --dtb ${DTB} --board ${PACK_BOARD} --base ${BASE_ADDRESS} --kernel_offset ${KERNEL_OFFSET} --ramdisk_offse ${RAMDISK_OFFSET} --dtb_offset ${DTB_OFFSET} --header_version ${HEADER_VERSION} -o ${ROOT_DIR}/boot.img
	echo "=====================mkbootimg add dtb end====================="
}


################## command #########################
[ -e ${PACK_TOPDIR}/scripts/.hooks/pre-pack ] &&
	source ${PACK_TOPDIR}/scripts/.hooks/pre-pack

do_prepare
do_ini_to_dts
mkdtbo
do_common

grep "CONFIG_SUNXI_MKBOOTIMG_ADD_DTB=y" ${PACK_TOPDIR}/.config > /dev/null
if [ $? -eq 0 ]; then
	do_mkbootimg_add_dtb
fi

do_pack_${PACK_PLATFORM}
do_finish


if [ -e ${PACK_TOPDIR}/scripts/.hooks/post-pack ]; then
	source ${PACK_TOPDIR}/scripts/.hooks/post-pack
fi
