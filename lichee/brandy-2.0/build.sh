#!/bin/bash

#
#build.sh for Boot0/FES
#wangwei@allwinnertech
#

TOP_DIR=$(cd `dirname $0`;pwd;cd - >/dev/null 2>&1)
BRANDY_SPL_DIR=$TOP_DIR/spl
BRANDY_SPL_PUB_DIR=$TOP_DIR/spl-pub
SPL_OLD=$(grep  ".module.common.mk"  ${BRANDY_SPL_DIR}/Makefile)
set -e

show_help()
{
	printf "\nbuild.sh - Top level build scritps\n"
	echo "Valid Options:"
	echo "  -h  Show help message"
	echo "  -o build,e.g. spl,clean"
	echo "  -p <platform> platform, e.g. sun8iw18p1,  sun5i, sun6i, sun8iw1p1, sun8iw3p1, sun9iw1p1"
	echo "  -m mode,e.g. nand,mmc,nor"
	echo "  -c copy,e.g. any para"
	echo "example:"
	echo "./build.sh -o spl -p sun8iw18p1"
	echo "./build.sh -o spl -p sun8iw18p1 -m nand"
	echo "./build.sh -o spl -p sun8iw18p1 -m nand -c copy"
	printf "\n\n"
}

function build_clean()
{
	(cd $TOP_DIR/spl; make distclean)
	if [ -d "$TOP_DIR/spl-pub" ]; then
		(cd "$TOP_DIR/spl-pub"; make distclean)
	fi
}

function build_spl-pub_once()
{
	board=$1
	mode=$2
	path=$3
	(
	cd ${path}

	if [ "x${mode}" = "xall" ];then
		echo --------build for mode:${mode} board:${board}-------------------
		make distclean
		make b=${board}  ${CP}
		make -j ${CP}
	else
		echo --------build for mode:${mode} board:${board}-------------------
		make distclean
		make b=${board} m=${mode}  ${CP}
		case ${mode} in
			nand | mmc | spinor)
				make boot0 -j ${CP}
				;;
			sboot_nor)
				echo "Neednot build sboot_nor ..."
				;;
			*)
				make ${mode} -j ${CP}
				;;
		esac
	fi
	)
}

function build_spl-pub()
{
	if [ "x${BOARD}" = "xall" ];then
		for board in `ls $TOP_DIR/$1/board`;do
			if [ "x${MODE}" = "xall" ];then
				build_spl-pub_once ${board} all $1
			else
				build_spl-pub_once ${board} ${MODE} $1
			fi
		done
	elif [ "x${MODE}" = "xall" ];then
		build_spl-pub_once ${BOARD} all $1
	else
		build_spl-pub_once ${BOARD} ${MODE} $1
	fi
}

function build_spl_once()
{
	platform=$1
	mode=$2
	path=$3
	support_board_exit=`cat $TOP_DIR/${path}/board/${platform}/common.mk | grep -w "SUPPORT_BOARD" | awk -F= '{printf $2}'`
	if [ "x${BOARD}" != "xall" ];then
		suport_board=${BOARD}
	elif [ "x${support_board_exit}" = "x" ];then
		suport_board=null
	else
		suport_board=`cat $TOP_DIR/${path}/board/${platform}/common.mk | grep -w "SUPPORT_BOARD" | awk -F= '{printf $2}'`
	fi
	echo "suport_board:${suport_board}"
	(
	cd ${path}

	for board in ${suport_board};do
		if [ "x${mode}" = "xall" ];then
			echo --------build for platform:${platform} mode:${mode} board:${board}-------------------
			make distclean
			make p=${platform} b=${board} ${CP}
			make -j b=${board} ${CP}
		else
			echo --------build for platform:${platform} mode:${mode} board:${board}-------------------
			make distclean
			make p=${platform} m=${mode} b=${board} ${CP}
			case ${mode} in
				nand | mmc | spinor)
					make boot0 -j b=${board} ${CP}
					;;
				sboot_nor)
					echo "Neednot build sboot_nor ..."
					;;
				*)
					make ${mode} -j b=${board} ${CP}
					;;
			esac
		fi
	done
	)
}

function build_spl()
{
	if [ "x${PLATFORM}" = "xall" ]; then
		for platform in "$TOP_DIR/$1/board/"*; do
			build_spl_once "${platform##*/}" "${MODE}" "$1"
		done
	else
		[ -d "$TOP_DIR/$1/board/${PLATFORM}" ] || {
			echo "Unsupported Boot0 platform: ${PLATFORM}" >&2
			return 1
		}
		build_spl_once "${PLATFORM}" "${MODE}" "$1"
	fi
}

function build_spl_old()
{
	if [ "x${PLATFORM}" = "xall" ];then
		for platform in `ls $TOP_DIR/spl/board`;do
			if [ "x${MODE}" = "xall" ];then
				for mode in `ls ${TOP_DIR}/spl/board/${platform}`;do
					if [[ $mode =~ .*\.mk$ ]]  \
					&& [ "x$mode" != "xcommon.mk" ];then
						mode=${mode%%.mk*}
						build_spl_once ${platform} ${mode} $1
					fi
				done
			else
				build_spl_once $platform ${MODE} $1
			fi
		done
	elif [ "x${MODE}" = "xall" ];then
		for mode in `ls ${TOP_DIR}/spl/board/${PLATFORM}`;do
			if [[ $mode =~ .*\.mk$ ]]  \
			&& [ "x$mode" != "xcommon.mk" ];then
				mode=${mode%%.mk*}
				build_spl_once ${PLATFORM} ${mode} $1
			fi
		done
	else
		build_spl_once ${PLATFORM} ${MODE} $1
	fi
}

function build_all()
{

	if [ -d ${BRANDY_SPL_DIR} ] && [ "x${SPL_OLD}" != "x" ] ; then
			[ "x${LICHEE_BRANDY_SPL}" != "x" ] && build_${LICHEE_BRANDY_SPL} ${LICHEE_BRANDY_SPL} || build_spl spl
	elif [ -d ${BRANDY_SPL_DIR} ] && [ "x${SPL_OLD}" = "x" ] ; then
		build_spl_old spl
	elif [ -d ${BRANDY_SPL_PUB_DIR} ];then
		build_spl-pub spl-pub
	fi
}

[ -e ${TOP_DIR}/../../.buildconfig ] && source ${TOP_DIR}/../../.buildconfig

while getopts ho:p:m:c:b: OPTION; do
	case $OPTION in
	h)
		show_help
		exit 0
		;;
	o)
		case "$OPTARG" in
			spl|spl-pub|clean) ;;
			*) echo "Unsupported build target: $OPTARG" >&2; exit 2 ;;
		esac
		if [ "x${SPL_OLD}" = "x" ] && [ "x$OPTARG" = "xspl" ]; then
			command="build_spl_old $OPTARG"
		else
			command="build_$OPTARG $OPTARG"
		fi
		;;

	p)
		PLATFORM=$OPTARG
		;;
	m)
		MODE=$OPTARG
		;;
	c)
		CP=C\=$OPTION
		;;
	b)
		BOARD=$OPTARG
		;;
	*)
		show_help
		exit $?
		;;
	esac
done
if [ "x${PLATFORM}" = "x" ];then
	PLATFORM=all
fi
if [ "x${MODE}" = "x" ];then
	MODE=all
fi
if [ "x${BOARD}" = "x" ];then
	BOARD=all
fi
# echo "PLATFORM:${PLATFORM}  MODE:${MODE}  CP:${CP}  BOARD:${BOARD} "
if [ "x$command" != "x" ];then
	$command
else
	build_all
fi
exit $?

