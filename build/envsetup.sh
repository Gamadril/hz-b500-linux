function hmm() {
cat <<EOF
Invoke ". build/envsetup.sh" from your shell to add the following functions to your environment:

== before all ==
- lunch:        lunch <product_name>-<build_variant>

== build project ==
- m:            Make from the top of the tree.
- mm:           Build package in the current directory, but not their dependencies.
- mma:          Build package in the current directory, and their dependencies.
- mmb:          Clean and build package in the current directory, but not their dependencies.
- p:            Pack from the top of the tree.
- pd:           Pack card0 from the top of the tree.
- mp:           Make and pack from the top of the tree
- mpd:          Make and pack card0 from the top of the tree
- mboot:        Build Boot0/FES for the selected storage.
- mboot0:       Just build boot0.
- mkernel:      Build kernel.
- mlibc:        Build c library.

== jump directory ==
- croot:    Jump to the top of the tree.
- cboot0:   Jump to boot0.
- cdts:     Jump to device tree.
- cbin:     Jump to the Boot0/FES bin directory.
- ckernel:  Jump to kernel.
- cdevice:  Jump to target.
- ccommon:  Jump to platform common.
- cconfigs: Jump to configs of target.
- cout:     Jump to out directory of target.
- ctarget:  Jump to target of compile directory.
- crootfs:  Jump to rootfs of compile directory.
- ctoolchain: Jump to toolchain directory.
- callwinnerpk: Jump to package allwinner directory.
- ctinatest:  Jump to tinateset directory.
- godir:    Go to the directory containing a file.

== grep file ==
- cgrep:    Greps on all local C/C++ files.

Look at the source to view more functions. The complete list is:
EOF
    T=$(gettop)
    local A
    A=""
    for i in `cat $T/build/envsetup.sh | sed -n "/^[ \t]*function /s/function \([a-z_]*\).*/\1/p" | sort | uniq`; do
      A="$A $i"
    done
    echo $A
}

function gettop
{
    local TOPFILE=build/envsetup.sh
    if [ -n "$TINA_TOP" -a -f "$TINA_TOP/$TOPFILE" ] ; then
        # The following circumlocution ensures we remove symlinks from TOP.
        (\cd $TINA_TOP; pwd)
    else
        if [ -f $TOPFILE ] ; then
            # The following circumlocution (repeated below as well) ensures
            # that we record the true directory name and not one that is
            # faked up with symlink names.
	    (pwd)
        else
            local here="$(pwd)"
            while [ "${here}" != "/" ]; do
                if [ -f "${here}/${TOPFILE}" ]; then
                    (\cd ${here}; pwd)
                    break
                fi
                here="$(dirname ${here})"
            done
        fi
    fi
}

function add_lunch_combo()
{
    local c
    for c in ${LUNCH_MENU_CHOICES[@]} ; do
        if [ "$1" = "$c" ] ; then
            return
        fi
    done
    LUNCH_MENU_CHOICES=(${LUNCH_MENU_CHOICES[@]} $1)
}

function print_lunch_menu()
{
    local uname=$(uname)
    echo
    echo "You're building on" $uname
    echo
    echo "Lunch menu... pick a combo:"

    local i=1
    local choice
    for choice in ${LUNCH_MENU_CHOICES[@]}
    do
        echo "     $i. $choice"
        i=$(($i+1))
    done
    echo
}

# check to see if the supplied product is one we can build
function check_platform()
{
    local T=$(gettop)
    [ -d "$T/target/allwinner" ] || return 1

    local v
    for v in ${PLATFORM_CHOICES}
    do
        [ "$v" = "$1" ] && return 0
    done
    return 1
}

function check_product()
{
    T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    TARGET_PRODUCT=$1 \
    TARGET_BUILD_VARIANT= \
    TARGET_BUILD_TYPE= \
    TARGET_BUILD_APPS= \
    get_build_var TARGET_DEVICE > /dev/null
}

# check to see if the supplied variant is valid
function check_variant()
{
    for v in ${VARIANT_CHOICES}
    do
        [ "$v" = "$1" ] && return 0
    done
    return 1
}

# check whether parameters of platform is missing.
function check_parameters()
{
    chip=${TARGET_CHIP}
    if [ "x$chip" = "x" ]; then
        echo "platform($TARGET_PLATFORM) not support"
        echo "Please check whether device is missing!"
        return 1
    fi
    return 0
}

# Get the exact value of a build variable.
function get_build_var()
{
    T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    (\cd $T; CALLED_FROM_SETUP=true BUILD_SYSTEM=build \
      command make --no-print-directory -f build/config.mk dumpvar-$1)
}

function get_chip
{
    local T=$(gettop)
    [ -z "$T" ] && return -1
    [ -z "${TARGET_PLATFORM}" ] && return -1
    [ -z "${TARGET_KERNEL_VERSION}" ] && return -1

    local plan="${TARGET_PLAN:-${TARGET_BOARD#${TARGET_PLATFORM}-}}"
    local longan_kernel_config=$T/target/allwinner/${TARGET_BOARD}/config-${TARGET_KERNEL_VERSION}
    local tina_kernel_config=$T/device/config/chips/${TARGET_PLATFORM}/configs/${plan}/linux/config-${TARGET_KERNEL_VERSION}
    local tina_new_kernel_config=$T/device/config/chips/${TARGET_PLATFORM}/configs/${plan}/linux-${TARGET_KERNEL_VERSION}/config-${TARGET_KERNEL_VERSION}

    [ -e "${longan_kernel_config}" ] && awk -F'[_=]' '/CONFIG_ARCH_SUN.*P.*=y/{print tolower($3)}' "${longan_kernel_config}" | head -n 1 && return
    [ -e "${tina_new_kernel_config}" ] && awk -F'[_=]' '/CONFIG_ARCH_SUN.*W.*=y/{print tolower($3"p1")}' "${tina_new_kernel_config}" | head -n 1 && return
    [ -e "${tina_kernel_config}" ] && awk -F'[_=]' '/CONFIG_ARCH_SUN.*P.*=y/{print tolower($3)}' "${tina_kernel_config}" | head -n 1 && return

    return -1
}

function get_kernel
{
    local T=$(gettop)
    [ -z "$T" ] && return -1
    [ -z "${TARGET_BOARD}" ] && return -1
    local f="$T/target/allwinner/${TARGET_BOARD}/Makefile"
    [ -f "$f" ] || return -1

    awk -F":=" '/KERNEL_PATCHVER/{print $2}' $f
}

function get_arch
{
    local T=$(gettop)
    [ -z "$T" ] && return -1
    [ -z "${TARGET_BOARD}" ] && return -1
    local f="$T/target/allwinner/${TARGET_BOARD}/Makefile"
    [ -f "$f" ] || return -1

    awk -F":=" '/ARCH/{print $2}' $f
}

function parse_boardconfig()
{
    local special_config="$T/device/config/chips/${TARGET_PLATFORM}/configs/${TARGET_PLAN}/BoardConfig.mk"
    local default_config="$T/device/config/chips/${TARGET_PLATFORM}/configs/default/BoardConfig.mk"
    local config_list=($default_config $special_config)

    local fetch_list=""
    local fpare_list=""
    for f in ${config_list[@]}; do
        if [ -f $f ]; then
            fetch_list=(${fetch_list[@]} $f)
            fpare_list="$fpare_list -f $f"
        fi
    done

    local cfgkeylist=(
        LICHEE_BUSSINESS
        LICHEE_BRANDY_DEFCONF
        LICHEE_BRANDY_SPL
        LICHEE_BOARD
        LICHEE_REDUNDANT_ENV_SIZE
        )

    local cfgkey=""
    local cfgval=""
    for cfgkey in ${cfgkeylist[@]}; do
        #don't use current value in env
        export $cfgkey=""
        if [ -n "$fpare_list" ]; then
            cfgval="$(echo '__unique:;@echo ${'"$cfgkey"'}' | command make -f - $fpare_list --no-print-directory __unique)"
        else
            cfgval=""
        fi
        export $cfgkey=$cfgval
    done

}

function fix_after_lunch
{
    local T="$(gettop)"
    cd $T/device/config/chips/${TARGET_PLATFORM}/configs/${TARGET_PLAN}
    [ -e linux-${TARGET_KERNEL_VERSION}/board.dts ] && {
        rm -f board.dts
        ln -s linux-${TARGET_KERNEL_VERSION}/board.dts board.dts
    }
    [ -L linux ] && rm -f linux
    [ -e linux ] || {
        ln -s linux-${TARGET_KERNEL_VERSION} linux
    }
}

function lunch
{
    # get last platfrom as default platform
    local T="$(gettop)"
    local last
    if [ -f "$T/.config" ]; then
        #last="$(awk -F[=_] '/CONFIG_TARGET_[a-z_0-9]*[^_]=y/{print $3 "_" $4; exit}' ${T}/.config)"
        last="$(sed -n -r '/CONFIG_TARGET_[a-zA-Z_0-9]*[^_]=y/{s/CONFIG_TARGET_([a-zA-Z_0-9]*[^_])=y/\1/;p;q}' ${T}/.config)"
        local last_chip="$(sed -n -r '/CONFIG_PLATFORM_[a-zA-Z_0-9].*[^_]=y/{s/CONFIG_PLATFORM_([a-zA-Z_0-9].*[^_])=y/\1/;p;q}' ${T}/.config)"
        if [[ ${last_chip} =~ "-" ]]; then
            last=${last/_/-}
        fi
    fi

    # select platform
    local select
    if [ "$1" ] ; then
        select=$1
    else
        print_lunch_menu
        echo -n "Which would you like?"
        [ -n "${last}" ] && echo -n " [Default ${last}]"
        echo -n ": "
        read select
    fi
    if [ -z "${select}" ]; then
        select="${last}-tina"
    elif (echo -n $select | grep -q -e "^[0-9][0-9]*$"); then
        [ $select -le ${#LUNCH_MENU_CHOICES[@]} ] \
            && select=${LUNCH_MENU_CHOICES[$(($select-1))]}
    elif (echo -n $select | grep -q -e "^[^\-][^\-]*-[^\-][^\-]"); then
        select="$select"
    else
        echo
        echo "Invalid lunch combo: $select" >&2
        return 1
    fi

    # check platform
    local platform=$(echo -n $select | sed -e "s/_.*$//")
    check_platform ${platform}
    if [ $? -ne 0 ]; then
        echo
        echo "** Don't have a platform spec for: '$platform'" >&2
        echo "** Must be one of ${PLATFORM_CHOICES}" >&2
        echo "** Do you have the right repo manifest?" >&2
        platform=
    fi

    # check product
    local product=$(echo -n $select | sed -e "s/-$VARIANT_CHOICES//")
    check_product $product
    if [ $? -ne 0 ]
    then
        echo
        echo "** Don't have a product spec for: '$product'" >&2
        echo "** Do you have the right repo manifest?" >&2
        product=
    fi

    local variant
    echo -n $select | grep "\-$VARIANT_CHOICES" > /dev/null
    if [ $? = 0 ]; then
        variant=$VARIANT_CHOICES
    fi
    check_variant $variant
    if [ $? -ne 0 ]
    then
        echo
        echo "** Invalid variant: '$variant'" >&2
        echo "** Must be one of ${VARIANT_CHOICES}" >&2
        variant=
    fi

    [ -z "$product" -o -z "$variant" -o -z "$platform" ] && return 1

    export TARGET_PRODUCT=$product
    export TARGET_PLATFORM=$platform
    export TARGET_BOARD=$(get_build_var TARGET_DEVICE)
    export TARGET_PLAN=${TARGET_BOARD#$TARGET_PLATFORM-}
    export TARGET_BUILD_VARIANT=$variant
    export TARGET_BUILD_TYPE=release
    export TARGET_KERNEL_VERSION=$(get_kernel)
    export TARGET_CHIP=$(get_chip)
    export TINA_TARGET_ARCH=$(get_arch)
    export TINA_BUILD_TOP=$(gettop)

    export LICHEE_ARISC_PATH=${TINA_BUILD_TOP}/lichee/arisc
    export LICHEE_CHIP_CONFIG_DIR=${TINA_BUILD_TOP}/device/config/chips/${TARGET_PLATFORM}
    export LICHEE_BOARD_CONFIG_DIR=${LICHEE_CHIP_CONFIG_DIR}/configs/${TARGET_PLAN}

    #for plat_config.sh
    parse_boardconfig
    export LICHEE_PACK_OUT_DIR=${TINA_BUILD_TOP}/out/${TARGET_BOARD}/image
    export LICHEE_PLAT_OUT=${TINA_BUILD_TOP}/out/${TARGET_BOARD}
    echo "============================================"
    echo "TINA_BUILD_TOP=${TINA_BUILD_TOP}"
    echo "TINA_TARGET_ARCH=${TINA_TARGET_ARCH}"
    echo "TARGET_PRODUCT=${TARGET_PRODUCT}"
    echo "TARGET_PLATFORM=${TARGET_PLATFORM}"
    echo "TARGET_BOARD=${TARGET_BOARD}"
    echo "TARGET_PLAN=${TARGET_PLAN}"
    echo "TARGET_BUILD_VARIANT=${TARGET_BUILD_VARIANT}"
    echo "TARGET_BUILD_TYPE=${TARGET_BUILD_TYPE}"
    echo "TARGET_KERNEL_VERSION=${TARGET_KERNEL_VERSION}"
    echo "TARGET_CHIP=${TARGET_CHIP}"
    echo "============================================"

    export BUILD_ENV_SEQUENCE_NUMBER=10
    # With this environment variable new GCC can apply colors to warnings/errors
    export GCC_COLORS='error=01;31:warning=01;35:note=01;36:caret=01;32:locus=01:quote=01'

    rm -rf tmp

    [ -e $T/target/allwinner/${TARGET_BOARD}/${TARGET_PRODUCT}-setup.sh ] \
        && source $T/target/allwinner/${TARGET_BOARD}/${TARGET_PRODUCT}-setup.sh

    check_parameters

    (fix_after_lunch)

    # after lunch other platform, restart buildserver
    clbuildserver
    prepare_buildserver
}

# Tab completion for lunch.
function _lunch
{
    local cur prev
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    COMPREPLY=( $(compgen -W "${LUNCH_MENU_CHOICES[*]}" -- ${cur}) )
    return 0
}

# Tab completion for m.
function _m
{
    local T=$(gettop)
    [ -z "$T" ] && return
    [ -f "$T/build/toplevel.mk" ] || return

    local cur prev list
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    list="$(grep "[^\.].*config:" $T/build/toplevel.mk | awk -F: '{print $1}')"
    COMPREPLY=( $(compgen -W "${list}" -- ${cur}) )
}

function envsetup
{
    if [ "x$SHELL" != "x/bin/bash" ]; then
        case `ps -o command -p $$` in
            *bash*)
                ;;
            *)
                echo -n "WARNING: Only bash is supported, "
                echo "use of other shell would lead to erroneous results"
                ;;
        esac
    fi

    # check top of SDK
    if [ ! -f "$(pwd)/build/envsetup.sh" ]; then
        echo "ERROR: Please source envsetup.sh in the root of SDK"
        return -1
    else
        export TINA_TOP="$(pwd)"
    fi

    # reset these variables.
    # LUNCH_MENU_CHOICES will be built up again when the vendorsetup.sh are included
    unset LUNCH_MENU_CHOICES
    export LUNCH_MENU_CHOICES
    export VARIANT_CHOICES=tina
    export PLATFORM_CHOICES="$(ls $(gettop)/target/allwinner 2>/dev/null \
        | grep common | sort | uniq | sed 's/-common//g')"

    # Execute the contents of any vendorsetup.sh files we can find.
    local vendors vendor awchips
    if [ -d "$(pwd)/device/config/chips" ]; then
	# only show platfroms with chip configuration.
	awchips="$(ls $(pwd)/device/config/chips | awk '{print " -e "$1"-"}')"
	if [ $? -eq 0 ]; then
		verdors="$(find -L target -maxdepth 4 -name 'vendorsetup.sh' | grep ${awchips} 2>/dev/null | sort)"
	else
		verdors="$(find -L target -maxdepth 4 -name 'vendorsetup.sh' 2>/dev/null | sort)"
	fi
    else
	verdors="$(find -L target -maxdepth 4 -name 'vendorsetup.sh' 2>/dev/null | sort)"
    fi
    for verdor in ${verdors}
    do
        source ${verdor}
    done
    echo "Setup env done! Please run lunch next."

    # completion
    complete -F _lunch lunch
    complete -F _m m
}

function m
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    (\cd $T && make $@)
}

function mm
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    $T/scripts/mm.sh $T $*
}

function mma
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    $T/scripts/mma.sh $T $*
}

function mmb
{
    mm -B
}

function p
{
	local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    (\cd $T && pack $@)
}

function pd
{
    p -d
}

function mp
{
    m $@ && p
}

function mpd
{
    m $@ && pd
}

# Build Boot0/FES for the selected board.
function build_boot()
{
	local T=$(gettop)
	local chip=${TARGET_CHIP}
	local cmd=$1
	local o_option=$2
	local platform
	local bin_dir

	local f="$T/device/config/chips/${TARGET_PLATFORM}/configs/${TARGET_PLAN}/sys_config.fex"
	local B="$( awk -F"=" '/^storage_type/{print $2}' $f | sed 's/^[ \t]*//g' )"
	if [ x"$B" = x"3" ]; then
		export LICHEE_FLASH="nor"
	else
		export LICHEE_FLASH="default"
	fi

	echo $TARGET_PRODUCT $TARGET_PLATFORM $TARGET_BOARD
	if [ -z "$TARGET_BOARD" -o -z "$TARGET_PLATFORM" ]; then
		echo "Please use lunch to select a target board before build boot."
		return 1
	fi

	if [ "x$chip" = "x" ]; then
		echo "platform($TARGET_PLATFORM) not support"
		return 1
	fi

	platform=${chip}

	bin_dir="device/config/chips/${TARGET_PLATFORM}/bin"
	\cd "$T/lichee/brandy-2.0/" || return 1
	if [ x"$o_option" == "xboot0" ]; then
		if [ x"${LICHEE_BRANDY_SPL}" != x ]; then
			o_option=${LICHEE_BRANDY_SPL}
		else
			o_option=spl
		fi

		if [ ! -f "spl-pub/Makefile" ]; then
			o_option=spl
		fi

		if [ ! -f "spl/Makefile" ]; then
			o_option=spl-pub
		fi
	fi

	echo "build_boot platform:$platform o_option:$o_option"
	if [ x"$o_option" != "x" ]; then
		TARGET_BIN_DIR=${bin_dir} ./build.sh -p $platform -o $o_option -b ${TARGET_PLATFORM}
	else
		TARGET_BIN_DIR=${bin_dir} ./build.sh -p $platform
	fi
	if [ $? -ne 0 ]; then
		echo "$cmd stop for build error in brandy, Please check!"
		\cd - 1>/dev/null
		return 1
	fi
	\cd - 1>/dev/null
	echo "$cmd success!"
	return 0
}

function mkarisc
{
	echo "=== Start build scp.bin ==="

	local arisc_cfg;

	if [ x"mr813" != x${TARGET_PLATFORM} -a x"r818" != x${TARGET_PLATFORM} ];
	then echo "Platform ${TARGET_PLATFORM} Skip marisc." && return 0
	fi

	if [ -f ${LICHEE_BOARD_CONFIG_DIR}/../default/arisc.config ]
	then
		arisc_cfg=${LICHEE_BOARD_CONFIG_DIR}/../default/arisc.config
	fi

	if [ -f ${LICHEE_CHIP_CONFIG_DIR}/tools/arisc_config_parse.sh ]
	then
		${LICHEE_CHIP_CONFIG_DIR}/tools/arisc_config_parse.sh
	fi

	if [ -f ${LICHEE_BOARD_CONFIG_DIR}/arisc.config ]
	then
		arisc_cfg=${LICHEE_BOARD_CONFIG_DIR}/arisc.config
	fi

	if [ ! -e ${arisc_cfg} ]
	then
		return 1;
	fi

	if [ ! -d ${LICHEE_ARISC_PATH} ];
	then
		echo "arisc project lost, use repo sync to get it"
		return 1;
	fi

	cp ${arisc_cfg} ${LICHEE_ARISC_PATH}/.config
	command make -C ${LICHEE_ARISC_PATH}

	[ $? -ne 0 ] && print_red "Error to build scp.bin" && return 1

	[ ! -e ${LICHEE_CHIP_CONFIG_DIR}/bin ] && print_red "Error Not exist bin dir." && return 1
	echo "cp ${LICHEE_ARISC_PATH}/ar100s/scp.bin to ${LICHEE_CHIP_CONFIG_DIR}/bin/scp.bin"
	cp ${LICHEE_ARISC_PATH}/ar100s/scp.bin  ${LICHEE_CHIP_CONFIG_DIR}/bin/scp.bin
	[ $? -ne 0 ] && print_red 'Error to cp scp.bin' && return 1

	echo "make arisc success!"
	return 0
}

function mboot
{
    mboot0
}

function mboot0
{
    (build_boot mboot0 boot0)
}

function mkernel
{
    m target/allwinner/install $@
}

function mlibc() {
	local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

	$T/scripts/mlibc.sh $T $*
}

function print_red(){
    echo -e '\033[0;31;1m'
    echo $1
    echo -e '\033[0m'
}

function print_green(){
    echo -e '\033[0;32;1m'
    echo $1
    echo -e '\033[0m'
}

get_uart_debug_port()
{
    local f="$T/device/config/chips/${TARGET_PLATFORM}/configs/${TARGET_PLAN}/sys_config.fex"
    local key_name="uart_debug_port"
    local count=`cat $f | grep  "$key_name" -c`
    local uart_port=0

    while true
    do
        if [ $count = 0 ]; then
            break;
        fi
        str=`cat $f |grep  "$key_name" |awk 'NR=='$count`
        count=$(($count-1))
        if [ "x${str:0:1}" = "x;" ]; then
            continue;
        fi
        uart_port=`echo $str |tr -d ' '|awk -F "=" '{print $2}'`
    done
    echo "uart$uart_port"
}

pack_usage()
{
	printf "Usage: pack [-d] [-m] [-w] [-h]
	Packs the raw HZ-B500 SD image (d1s-hz-b500_sd only).
	-d pack firmware with debug info output to card0
	-m pack dump firmware
	-w accepted for compatibility; the SD image is always raw
	-h print this help message
"
}

function pack() {
	local T=$(gettop)
	local chip=sun5i
	local platform=$(get_build_var TARGET_BUILD_VARIANT)
	local board_platform=$(get_build_var TARGET_BOARD_PLATFORM)
	local board=$(get_build_var TARGET_BOARD)
	local debug=$(get_uart_debug_port)
	local mode=normal
	local programmer=none
	unset OPTIND
	while getopts "dmwh" arg
	do
		case $arg in
			d)
				debug=card0
				;;
			m)
				mode=dump
				;;
			w)
				programmer=programmer
				;;
			h)
				pack_usage
				return 0
				;;
			?)
			return 1
			;;
		esac
	done

	check_parameters
	[ "$?" -eq 1 ] && return

	$T/scripts/pack_img.sh -c $chip -p $platform -b $board \
		-d $debug -m $mode -w $programmer -t $T
}

function croot()
{
    T=$(gettop)
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    \cd $T
}

function cboot0()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    \cd "$T/lichee/brandy-2.0/${LICHEE_BRANDY_SPL:-spl}"
}

function copensbi()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    \cd $T/lichee/brandy-2.0/opensbi
}

function cbin()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    local target_bin_dir="$T/device/config/chips/${TARGET_PLATFORM}/configs/${TARGET_PLAN}/bin"
    local tina_bin_dir="$T/target/allwinner/${TARGET_BOARD%-*}-common/bin"
    local longan_bin_dir="$T/device/config/chips/${TARGET_PLATFORM}/bin"

    [ -e $longan_bin_dir -a -e $tina_bin_dir ] || \
        [ -e $target_bin_dir -a -e $tina_bin_dir ] && {
        print_red "warning: both longan and tina bin dir exist, default jump to longan"
        print_red "tina: $tina_bin_dir"
    }

    [ -e $longan_bin_dir -a -e $target_bin_dir ] && {
        echo -e	"\033[0;31;1mwarning: There are longan bin dir for both platform and target\033[0m"
        echo -e "\033[0;31;1mplease input 1 or 2 to select a dir to enter:\033[0m"
        echo "1.platform: $longan_bin_dir"
        echo "2.target: $target_bin_dir"
        local num
        read num
        if [ $num -eq 1 ]; then
            \cd $longan_bin_dir && return
        elif [ $num -eq 2 ]; then
            \cd $target_bin_dir && return
        else
            print_red "Syntax error, please input '1' or '2'" && return
        fi
    }

    [ -e $longan_bin_dir ] && \cd $longan_bin_dir && return
    [ -e $target_bin_dir ] && \cd $target_bin_dir && return
    [ -e $tina_bin_dir ] && \cd $tina_bin_dir && return
}

function cdts()
{
    local T=$(gettop)
    local S=
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    local K="linux-${TARGET_KERNEL_VERSION}"
    local A="${TINA_TARGET_ARCH}"
    [ -z "$K" -o -z "$A" ] && "Please lunch your combo firstly" && return
    [ "$A" = "aarch64" ] && {
        S=sunxi
        A=arm64
    }
    [ "$A" = "riscv" ] && {
        S=sunxi
        A=riscv
    }

    \cd $T/lichee/$K/arch/$A/boot/dts/$S
}

function ckernel
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    \cd $T/lichee/linux-${TARGET_KERNEL_VERSION}/
}

function carisc
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    \cd $T/lichee/arisc/ar100s
}


function cgeneric
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    \cd $T/target/allwinner/generic
}

function callwinnerpk
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    \cd $T/package/allwinner
}

function ctinatest
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    \cd $T/package/testtools/tinatest
}

function cdevice
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    \cd $T/target/allwinner/${TARGET_BOARD}
}

function ccommon
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    \cd $T/target/allwinner/${TARGET_PLATFORM}-common
}

function cconfigs
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    local tina_config_dir="$T/target/allwinner/${TARGET_BOARD}/configs"
    local longan_config_dir="$T/device/config/chips/${TARGET_PLATFORM}/configs/${TARGET_BOARD##*-}/linux"

    [ -e $longan_config_dir -a -e $tina_config_dir ] && {
        print_red "warning: both longan and tina configs dir exist"
        print_red "tina: $tina_config_dir"
        print_red "longan: $longan_config_dir"
    }

    [ -e $tina_config_dir ] && \cd $tina_config_dir && return
    [ -e $longan_config_dir ] && \cd $longan_config_dir && return
}

function ctoolchain()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    local A=${TINA_TARGET_ARCH}
    local C="$(awk -F\" '/CONFIG_LIBC=/{print $2}' \
        $T/target/allwinner/${TARGET_BOARD}/defconfig)"

    if [ "$TARGET_PLATFORM" = "r6" -o "$TARGET_PLATFORM" = "c200s" -o "$TARGET_PLATFORM" = "v133" -o "$TARGET_PLATFORM" = "c600" ]; then
	    C=arm9-$C
    fi

    if [ "$A" = "riscv" ];then
        \cd $T/prebuilt/gcc/linux-x86/$A/toolchain-thead-$C/riscv*
    else
        \cd $T/prebuilt/gcc/linux-x86/$A/toolchain-sunxi-$C/toolchain
    fi
}

function crootfs()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    \cd $T/out/${TARGET_BOARD}/compile_dir/target/rootfs
}

function cout()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    \cd $T/out/${TARGET_BOARD}
}

function cgrep()
{
    find . -name .repo -prune -o -name .git -prune -o -name out -prune -o -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 grep --color -n "$@"
}

function mgrep()
{
    find . -name .repo -prune -o -name .git -prune -o -path ./out -prune -o \( -regextype posix-egrep -iregex '(.*\/Makefile|.*\/Makefile\..*|.*\.make|.*\.mak|.*\.mk|.*\.bp)' -o -regextype posix-extended -regex '(.*/)?soong/[^/]*.go' \) -type f \
        -exec grep --color -n "$@" {} +
}


function ctarget()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    [ -z "${TARGET_BOARD}" ] && "Please lunch your combo firstly" && return

    \cd $T/out/${TARGET_BOARD}/compile_dir/target
}

function add-rootfs-demo()
{
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return
    cp -rf $(gettop)/package/add-rootfs-demo/*  $(gettop)/out/${TARGET_BOARD}/compile_dir/target/rootfs
    rm $(gettop)/out/${TARGET_BOARD}/compile_dir/target/rootfs/README
    $(gettop)/out/host/bin/mksquashfs4          $(gettop)/out/${TARGET_BOARD}/compile_dir/target/rootfs \
                    $(gettop)/out/${TARGET_BOARD}/root.squashfs -noappend -root-owned -comp xz -b 256k \
                                        -p '/dev d 755 0 0' -p '/dev/console c 600 0 0 5 1' -processors 1
    rm  $(gettop)/out/${TARGET_BOARD}/rootfs.img
    dd if=$(gettop)/out/${TARGET_BOARD}/root.squashfs of=$(gettop)/out/${TARGET_BOARD}/rootfs.img bs=128k conv=sync
}

function prepare_buildserver()
{
    $T/tools/build/buildserver --path $T 2>/dev/null 1>&2 &
}

function clbuildserver()
{
    lsof $T/tools/build/buildserver 2> /dev/null | grep -q buildserver
    if [ $? -eq 0 ]; then
        echo "clean buildserver"
        lsof $T/tools/build/buildserver 2> /dev/null | grep buildserver | awk '{ print $2}' | xargs kill -9
    else
        echo "no buildserver to clean"
    fi
}

function mkrootfs_squashfs4()
{
	kernelfs_formate=`grep CONFIG_SQUASHFS=y $(gettop)/lichee/*/.config | cut -d ":" -f 2`
	echo -e "\033[31m$kernelfs_formate\033[0m"
	if [ -z $kernelfs_formate ];then
		echo -e "\033[31m run -make kernel_menuconfig- choice "squashfs" first!\033[0m"
	else
		compression=`grep ^CONFIG_KERNEL.*y$ $(gettop)/.config | awk 'NR==1{print}' | sed -r 's/.*_(.*)=.*/\1/' | tr '[A-Z]' '[a-z]'`
		if [ -n "$compression" ];then
			$(gettop)/out/host/bin/mksquashfs4  $(gettop)/out/${TARGET_BOARD}/compile_dir/target/rootfs  $(gettop)/out/${TARGET_BOARD}/root.squashfs \
									-noappend -root-owned -comp $compression -b 256k -p '/dev d 755 0 0' -p '/dev/console c 600 0 0 5 1' -processors 1
		else
			$(gettop)/out/host/bin/mksquashfs4  $(gettop)/out/${TARGET_BOARD}/compile_dir/target/rootfs $(gettop)/out/${TARGET_BOARD}/root.squashfs  \
											-noappend -root-owned -comp xz -b 256k -p '/dev d 755 0 0' -p '/dev/console c 600 0 0 5 1' -processors 1
		fi
		rm  $(gettop)/out/${TARGET_BOARD}/rootfs.img
		dd if=$(gettop)/out/${TARGET_BOARD}/root.squashfs of=$(gettop)/out/${TARGET_BOARD}/rootfs.img bs=128k conv=sync
	fi
}

function mkrootfs_ext4()
{
	kernelfs_formate=`grep CONFIG_EXT4_FS=y $(gettop)/lichee/*/.config | cut -d ":" -f 2`
	echo -e "\033[31m $kernelfs_formate\033[0m"
	if [ -z $kernelfs_formate ];then
		echo -e "\033[31m run -make kernel_menuconfig- choice "ext4fs" first!\033[0m"
	else
		$(gettop)/out/host/bin/make_ext4fs -l 50331648 -b 4096 -i 6000 -m 0 -J $(gettop)/out/${TARGET_BOARD}/root.ext4  $(gettop)/out/${TARGET_BOARD}/compile_dir/target/rootfs
		rm  $(gettop)/out/${TARGET_BOARD}/rootfs.img
		dd if=$(gettop)/out/${TARGET_BOARD}/root.ext4 of=$(gettop)/out/${TARGET_BOARD}/rootfs.img bs=128k conv=sync
	fi
}

function mkrootfs_jffs2()
{
	kernelfs_formate=`grep CONFIG_JFFS2_FS=y $(gettop)/lichee/*/.config | cut -d ":" -f 2`
	echo -e "\033[31m$kernelfs_formate\033[0m"
	if [ -z $kernelfs_formate ];then
		echo -e "\033[31m run -make kernel_menuconfig- choice "jffs2fs" first!\033[0m"
	else
		$(gettop)/out/host/bin/mkfs.jffs2  --little-endian --squash-uids -v -X rtime -x zlib -x lzma -D $(gettop)/build/device_table.txt \
					-e 128KiB -o $(gettop)/out/${TARGET_BOARD}/root.jffs2-128k -d $(gettop)/out/${TARGET_BOARD}/compile_dir/target/rootfs \
																										-v 2>&1 1>/dev/null | awk '/^.+$/'
		rm  $(gettop)/out/${TARGET_BOARD}/rootfs.img
		dd if=$(gettop)/out/${TARGET_BOARD}/root.jffs2-128k of=$(gettop)/out/${TARGET_BOARD}/rootfs.img bs=128k conv=sync
	fi
}

function recomp_rootfs()
{
    T=$(gettop)
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    file_formate=`grep ^CONFIG_TARGET_ROOTFS.*y $(gettop)/.config | cut -d "_" -f 4 | grep "=" | sed -r 's/(.*)=.*/\1/'`
    echo -e "\033[31m $file_formate\033[0m"
    if [ -z $file_formate ];then
        echo -e "\033[31m run -make menuconfig- choice fs_formate of target images!\033[0m"
    else
        [ x$file_formate = x"SQUASHFS" ] && mkrootfs_squashfs4
        [ x$file_formate = x"EXT4FS" ] && mkrootfs_ext4
        [ x$file_formate = x"JFFS2" ] && mkrootfs_jffs2
    fi
}

function tina_update_all_config()
{
    T=$(gettop)
    local T=$(gettop)
    [ -z "$T" ] \
        && echo "Couldn't locate the top of the tree.  Try setting TOP." \
        && return

    filter=$1

    time=$(date "+%Y%m%d-%H%M%S")
    echo "${time}"
    for platform in $(ls $T/target/allwinner/)
    do
        if [[ $platform =~ $filter ]]; then
            [ -e $T/target/allwinner/$platform/BoardConfig.mk ] && {
                # echo ${platform//-/_}-tina
                lunch ${platform//-/_}-tina
                make oldconfig
            }
        fi
    done
    time=$(date "+%Y%m%d-%H%M%S")
    echo "${time}"
}

function godir ()
{
    if [[ -z "$1" ]]; then
        echo "Usage: godir <regex>"
        return
    fi
    local T=$(gettop)
    if [[ ! -f $T/filelist ]]; then
        echo -n "Creating index..."
        (\cd $T; find . -wholename ./out -prune -o -wholename ./.repo -prune -o -type f > filelist)
        echo " Done"
        echo ""
    fi
    local lines
    lines=($(\grep "$1" $T/filelist | sed -e 's/\/[^/]*$//' | sort | uniq))
    if [[ ${#lines[@]} = 0 ]]; then
        echo "Not found"
        return
    fi
    local pathname
    local choice
    if [[ ${#lines[@]} > 1 ]]; then
        while [[ -z "$pathname" ]]; do
            local index=1
            local line
            for line in ${lines[@]}; do
                printf "%6s %s\n" "[$index]" $line
                index=$(($index + 1))
            done
            echo
            echo -n "Select one: "
            unset choice
            read choice
            if [[ $choice -gt ${#lines[@]} || $choice -lt 1 ]]; then
                echo "Invalid choice"
                continue
        fi
            pathname=${lines[$(($choice-1))]}
        done
    else
        pathname=${lines[0]}
    fi
    \cd $T/$pathname
}

function get_newest_file()
{
	# find the newest file in $1
	find $1 -type f -and -not -path "*/.git*" -and -not -path ".*" -and -not -path "*:*" -and -not -path "*\!*" -and -not -path "* *" -and -not -path "*\#*" -and -not -path "*/.*_check" -and -not -path "*/.*.swp" -and -not -path "*/.newest-*.patch" -printf "%T@   %Tc   %p\n" | sort -n | tail -n 1
}

function make()
{
	local T=$(gettop)
	local start_time=$(date +"%s")

	if [ $T = $(pwd) ] ; then
		echo "===This's tina environment.==="
		if echo "$@" | grep -q -E "\-npq|prereq|clean|download|prepare|compile|install|config|update|refresh"; then
			echo "special target, skip mboot,marisc"
		else
			if [ -d "$T/lichee/brandy-2.0/spl" ]; then
				if [ x"$LICHEE_BRANDY_SPL" == x -o ! -f $T/lichee/brandy-2.0/$LICHEE_BRANDY_SPL/Makefile ]; then
					LICHEE_BRANDY_SPL=spl
				fi
				BOOT0_NEWEST_FILE=`get_newest_file $T/lichee/brandy-2.0/$LICHEE_BRANDY_SPL`
				BOOT0_REBUILD_FLAG=`echo "$BOOT0_NEWEST_FILE $TARGET_BOARD" | md5sum | awk '{print ".newest-"$1".patch"}'`
				# if there is no modification in boot0, do not rebuild boot0
				if [ ! -f $T/lichee/brandy-2.0/$LICHEE_BRANDY_SPL/$BOOT0_REBUILD_FLAG ]; then
					mboot0

					if [ "$?" -ne 0 ]; then
						echo -e "\033[31m#### make boot0 failed!\033[0m"
						return 1
					fi
					rm -rf $T/lichee/brandy-2.0/$LICHEE_BRANDY_SPL/.newest-*.patch
					BOOT0_NEWEST_FILE=`get_newest_file $T/lichee/brandy-2.0/$LICHEE_BRANDY_SPL`
					BOOT0_REBUILD_FLAG=`echo "$BOOT0_NEWEST_FILE $TARGET_BOARD" | md5sum | awk '{print ".newest-"$1".patch"}'`
					touch $T/lichee/brandy-2.0/$LICHEE_BRANDY_SPL/$BOOT0_REBUILD_FLAG
				else
					echo -e "no need rebuild boot0, the newest file is: \n\t$BOOT0_NEWEST_FILE"
				fi
			else
				echo "Boot0 sources unavailable"
			fi

			mkarisc
			[ $? -ne 0 ] && exit 1

		fi

		check_parameters
		[ "$?" -eq 1 ] && return
	else
		echo "===This not's tina environment, use command make.==="
	fi

	if [[ x"$@" = x"" ]] || [[ "$@" =~ "-j" ]] ; then
		prepare_buildserver
	elif [[ x"$@" = x"clean" ]] || [[ x"$@" = x"distclean" ]]; then
		clbuildserver
	fi

	command make V=s "$@"

	local ret=$?
	local end_time=$(date +"%s")
	local tdiff=$(($end_time-$start_time))
	local hours=$(($tdiff / 3600 ))
	local mins=$((($tdiff % 3600) / 60))
	local secs=$(($tdiff % 60))

	echo
	if [ $ret -eq 0 ] ; then
		echo -n -e "#### make completed successfully "
	else
		echo -n -e "#### make failed to build some targets "
	fi

	if [ $hours -gt 0 ] ; then
		printf "(%02g:%02g:%02g (hh:mm:ss))" $hours $mins $secs
	elif [ $mins -gt 0 ] ; then
		printf "(%02g:%02g (mm:ss))" $mins $secs
	elif [ $secs -gt 0 ] ; then
		printf "(%s seconds)" $secs
	fi
	echo -e " ####"
	echo

	return $ret
}

[ -e ./build/.hooks/expand_func ] && source ./build/.hooks/expand_func

#### MAIN ####
envsetup
