#!/bin/bash

################################################################################
# step:                                                                        #
# 1. get all progs in rootfs, and get libraries on which these progs depend.   #
# 2. get libraries on which these libraries(from 1st step) depend.             #
# 3. get soft-link libraries if exist.                                         #
# 4. remove libraries that have not been used.                                 #
################################################################################

SH_NAME=$0
OP_FLAG=$1
ROOT_DIR=${2%/}    # remove '/' at the end of of a directory path if it has.

PROGS=$ROOT_DIR/.progs.list
LIBS=$ROOT_DIR/.libs.list
PARSED_FILES=$ROOT_DIR/.parsed-files.list
DEP_LIBS_REAL=$ROOT_DIR/.depend-libs-real.list
DEP_LIBS_REAL_TMP=$ROOT_DIR/.depend-libs-real-tmp.list
WARNING_FILES=$ROOT_DIR/.warning_files.list
SPECIAL_LIBS=$ROOT_DIR/.special_libs.list

help_info()
{
    # get the basename of this scripts
    local shell_name=`basename $SH_NAME`

    echo -e "v1.1\n"
    echo -e "Downsize the rootfs or check the downsizing result!\n"
    echo -e "\033[32mUsage:\n\t./$shell_name <op_flag> <root_dir>\033[0m\n"
    echo -e "\t<op_flag>: operation."
    echo -e "\t\t d - downsize rootfs"
    echo -e "\t\t c - check the downsizing result"
    echo -e "\t<root_dir>: the relative directory of your rootfs.\n"
}

################################################################################
#                                                                              #
#                               LOG FUNCTIONS                                  #
#                                                                              #
################################################################################

function pr_error()
{
    echo -e "\033[47;31mERROR: $*\033[0m"
}

function pr_warn()
{
#    echo -e "\033[47;34mWARN: $*\033[0m"
    echo -e "\033[33mWARNING \033[0m $*"
}

function pr_info()
{
#    echo -e "\033[47;30mINFO: $*\033[0m"
    echo -e "\033[32mINFO \033[0m $*"
}

get_lib_readelf()
{
    readelf -d $1 | grep NEEDED | sed -r 's/.*\[(.*)\].*/\1/'
}

get_lib_strings()
{
    strings -a $1 | grep -e "\.so\." -e "\.so$" \
            | sed 's/ /\n/g' | sed -r 's/.*\/(.*)/\1/' | grep '\.so'
}

# Keep every link and the real file, resolving absolute links inside the rootfs.
# The library list uses ROOT_DIR paths, so retain that spelling after resolving.
keep_lib_path()
{
    local root_real current resolved target relative hops=0
    root_real=$(realpath -e -- "$ROOT_DIR") || return 1
    current=$(realpath -ms -- "$1") || return 1

    while true; do
        case "$current" in
            "$root_real"/*) ;;
            *) pr_error "library path escapes rootfs: $1"; return 1 ;;
        esac
        relative=${current#"$root_real"/}
        printf '%s/%s\n' "$ROOT_DIR" "$relative" >> "$DEP_LIBS_REAL"
        [ -L "$current" ] || break
        hops=$((hops + 1))
        if [ "$hops" -gt 40 ]; then
            pr_error "library symlink loop: $1"
            return 1
        fi
        target=$(readlink -- "$current") || return 1
        case "$target" in
            /*) current="$root_real$target" ;;
            *) current="${current%/*}/$target" ;;
        esac
        current=$(realpath -ms -- "$current") || return 1
    done

    # A directory in the path may itself be a symlink (e.g. lp64d -> ../lib).
    resolved=$(readlink -f -- "$current")
    case "$resolved" in
        "$root_real"/*) ;;
        *) pr_error "unresolved library path: $1"; return 1 ;;
    esac
    if [ ! -f "$resolved" ]; then
        pr_error "missing library target: $1"
        return 1
    fi
    relative=${resolved#"$root_real"/}
    printf '%s/%s\n' "$ROOT_DIR" "$relative" >> "$DEP_LIBS_REAL"
}

################################################################################
#                                                                              #
#                                GET LIBRARIES                                 #
#                                                                              #
################################################################################

# get_libs: get the library dependency of source file
#   $1    the source file.
get_libs()
{
    grep $1$ $PARSED_FILES > /dev/null
    if [ $? -eq 0 ]; then
        return
    fi

    echo "$1" >> $PARSED_FILES

#    readelf -s $ROOT_DIR/$prog_name | grep "\<UND dlopen\>" > /dev/null 2>&1
    grep -e dlopen -e LD_PRELOAD $1 > /dev/null
    if [ $? -eq 0 ]; then
        grep $1$ $SPECIAL_LIBS > /dev/null

        if [ $? -eq 0 ]; then
            local fdep_libs=$(get_lib_readelf $1)
        else
            local fdep_libs_readelf=$(get_lib_readelf $1)
            local fdep_libs=$(get_lib_strings $1)
        fi
    else
        local fdep_libs=$(get_lib_readelf $1)
    fi

    for ol in $fdep_libs
    do
        grep $ol$ $DEP_LIBS_REAL > /dev/null
        if [ $? -eq 0 ]; then
            continue
        fi

        local ol_real=`grep $ol$ $LIBS`
        if [ -z "$ol_real" ]; then
            if [ "x$OP_FLAG" == "xd" ]; then
                echo " "$fdep_libs_readelf" " | grep " $ol " > /dev/null
                if [ $? -eq 0 ]; then
                    pr_error "file: $1， missing lib: $ol in $ROOT_DIR"
                    rm -f $ROOT_DIR/.*.list
                    exit
                else
                    grep "^$1 $ol$" $WARNING_FILES> /dev/null
                    if [ $? -ne 0 ]; then
                        echo "$1 $ol" >> $WARNING_FILES
                        pr_info "file: $1，MAY missing dlopen/preload lib: $ol"
                    fi
                fi
            fi
        else
            while IFS= read -r lib_path; do
                keep_lib_path "$lib_path" || exit 1
            done <<< "$ol_real"

            # case: $link_lib -> $ol, add the $link_lib library.
            local link_lib=`ls -l ${ol_real%/*} | grep "^l.*$ol$" | \
                                awk -F '->' '{print $1}' | awk '{print $NF}'`
            if [ -n "$link_lib" ]; then
                echo $ol_real | sed "s#$ol#$link_lib#g" >> $DEP_LIBS_REAL
            fi
        fi
    done
}

# get_progs_depend_libs: get libraries on which all programs depends
get_progs_depend_libs() {
    file -F '' `find $ROOT_DIR` | \
            awk '/dynamically linked/ {print $1}' | grep -v '\.so' > $PROGS
    find $ROOT_DIR -name "*.so*" -o -name "*.a"> $LIBS

    while read prog_name
    do
        get_libs $prog_name
        # PT_INTERP is needed by execve even when absent from DT_NEEDED.
        local interpreter
        interpreter=$(LC_ALL=C readelf -l -- "$prog_name" 2>/dev/null |
            sed -n 's/.*Requesting program interpreter: \([^]]*\)].*/\1/p')
        if [ -n "$interpreter" ]; then
            keep_lib_path "$ROOT_DIR$interpreter" || exit 1
        fi
    done < $PROGS
}

################################################################################
#                                                                              #
#                FOR SPECIFIC LIBRARIES                                        #
#                                                                              #
################################################################################

# get_used_libcedarx: get used cedarx libraries
get_used_libcedarx()
{
    if [ -f $ROOT_DIR/etc/cedarx.conf ]; then
        local used_libcedarx=`grep "^l.*.so$" \
                    $ROOT_DIR/etc/cedarx.conf | awk '{print $3}'`

        for libcedarx_name in $used_libcedarx
        do
            local libcedarx_name_real=`grep $libcedarx_name$ $LIBS`

            if [ ! -f "$libcedarx_name_real" ]; then
                pr_warn "can't find $libcedarx_name (in cedarx.conf) in rootfs."
            else
                echo "$libcedarx_name_real" >> $DEP_LIBS_REAL
            fi
        done
    fi

    if [ -f $ROOT_DIR/etc/xdg/gstomx.conf ]; then
        find $ROOT_DIR/usr/lib -maxdepth 1 -name "libOmx*.so" >> $DEP_LIBS_REAL
    fi

}

# get_used_tslib: get used tslib
get_used_tslib()
{
    if [ -f $ROOT_DIR/etc/tslib-env.sh ]; then
        local used_tslib_path=$ROOT_DIR/`grep TSLIB_PLUGINDIR \
            $ROOT_DIR/etc/tslib-env.sh | awk -F = '{print $2}' | sed 's#/$##g'`
        local used_tslib=`ls -1 $used_tslib_path `

        if [ -n "$used_tslib" ]; then
            echo "$used_tslib" | \
                    sed "s#^#$used_tslib_path/#g" >> $DEP_LIBS_REAL

            echo `grep libts.so.0 $LIBS` | sed 's/ /\n/g' >> $SPECIAL_LIBS
        fi
    fi
}

# get_used_alsa_lib: get used alsa-lib plugins
get_used_alsa_lib()
{
    if [ -f $ROOT_DIR/usr/lib/libasound.so.2 \
                    -a -d $ROOT_DIR/usr/lib/alsa-lib ]; then
        local used_alsa_lib=`ls $ROOT_DIR/usr/lib/alsa-lib`

        if [ -n "$used_alsa_lib" ]; then
            echo "$used_alsa_lib" | \
                  sed "s#^#$ROOT_DIR/usr/lib/alsa-lib/#g" >> $DEP_LIBS_REAL

            echo `grep usr/lib/libasound.so.2 $LIBS` | \
                        sed 's/ /\n/g' >> $SPECIAL_LIBS
        fi
    fi
}

# get_used_directfb_lib: get used directfb libs
get_used_directfb_lib()
{
    if [ -f $ROOT_DIR/usr/lib/libdirectfb.so \
                -a -d $ROOT_DIR/usr/lib/directfb* ]; then
        local used_directfb_lib=`find $ROOT_DIR/usr/lib/directfb* -name "*.so*"`

        if [ -n "$used_directfb_lib" ]; then
            echo `grep usr/lib/libdirectfb $LIBS` | \
                            sed 's/ /\n/g' >> $DEP_LIBS_REAL
            echo "$used_directfb_lib" >> $DEP_LIBS_REAL

            echo `grep usr/lib/libdirectfb $LIBS` | \
                            sed 's/ /\n/g' >> $SPECIAL_LIBS
        fi
    fi
}

# get_used_tinatest_lib: get used tinatest libs
get_used_tinatest_lib()
{
    if [ -f $ROOT_DIR/usr/bin/tinatest -a -d $ROOT_DIR/usr/lib/tt-module ]; then
        local used_tinatest_lib=`find $ROOT_DIR/usr/lib/tt-module -name "*.so*"`

        if [ -n "$used_tinatest_lib" ]; then
            echo "$used_tinatest_lib" >> $DEP_LIBS_REAL
        fi
    fi
}

# get_used_gstreamer_lib: get used gstreamer libs
get_used_gstreamer_lib()
{
    find $ROOT_DIR/usr/lib -maxdepth 1 -name libgst* >> $DEP_LIBS_REAL

    if [ -d $ROOT_DIR/usr/lib/gstreamer-1.0 ]; then
        local used_gstreamer_lib=`ls $ROOT_DIR/usr/lib/gstreamer-1.0`

        if [ -n "$used_gstreamer_lib" ]; then
            echo "$used_gstreamer_lib" | \
                  sed "s#^#$ROOT_DIR/usr/lib/gstreamer-1.0/#g" >> $DEP_LIBS_REAL
        fi
    fi
}

# get_specific_used_libs: get the specific used libs
get_specific_used_libs()
{
    # for glibc, add /lib/libnss_* and libresolv*.
    # refer to https://sourceware.org/ml/libc-help/2009-05/msg00046.html
    find $ROOT_DIR/lib -maxdepth 1 -name libnss_* \
                        -o -name libresolv* >> $DEP_LIBS_REAL

    get_used_libcedarx
    get_used_tslib
    get_used_alsa_lib
    get_used_directfb_lib
    get_used_tinatest_lib
    get_used_gstreamer_lib
}

################################################################################
#                                                                              #
#                    RECURSIVE TO GET ALL NEEDED LIBRARIES                     #
#                                                                              #
################################################################################

# get_lib_depend_libs: get the library dependency of librires.
get_lib_depend_libs()
{
    #keep every library only once
    sort -u    $DEP_LIBS_REAL -o $DEP_LIBS_REAL

    #get lib based lib
    while true
    do
        cp -f $DEP_LIBS_REAL $DEP_LIBS_REAL_TMP

        while read ol
        do
            get_libs $ol
        done < $DEP_LIBS_REAL_TMP

        sort -u $DEP_LIBS_REAL -o $DEP_LIBS_REAL

        diff $DEP_LIBS_REAL $DEP_LIBS_REAL_TMP > /dev/null
        if [ $? -eq 0 ]; then
            break
        fi
    done
}

################################################################################
#                                                                              #
#                              REMOVE FUNCTIONS                                #
#                                                                              #
################################################################################

downsize_prepare() {
    rm -rf $ROOT_DIR/.*.list
    touch $PROGS $LIBS $PARSED_FILES $DEP_LIBS_REAL \
            $DEP_LIBS_REAL_TMP $WARNING_FILES $SPECIAL_LIBS
}

# remove_unused_libs: remove unused libs in /lib and /usr/lib
remove_unused_libs()
{
    find $ROOT_DIR/lib -maxdepth 1 -name "*.so*" -o -name "*.a" > $LIBS
    find $ROOT_DIR/usr/lib -maxdepth 1 -name "*.so*" -o -name "*.a" >> $LIBS

    if [ "x$OP_FLAG" == "xd" ]; then
        rm -rf `grep -F -v -f $DEP_LIBS_REAL $LIBS | sort | uniq -u`
    fi

    if [ "x$OP_FLAG" == "xc" ]; then
        # for case $OP_FLAG is equal to 'c'
        local diff_lib=`grep -F -v -f $DEP_LIBS_REAL $LIBS | sort | uniq -u`

        if [ -n "$diff_lib" ]; then
            pr_error "following libraries have not been used:"
            echo -e "$diff_lib"
        else
            echo -e "Check result: \033[32m==RIGHT==\033[0m"
        fi
    fi
}

# remove_all_unused_libs: remove all unused libs in rootfs
remove_all_unused_libs()
{
    if [ "x$OP_FLAG" == "xd" ]; then
        rm -rf `cat $DEP_LIBS_REAL $LIBS | sort | uniq -u`
    fi

    if [ "x$OP_FLAG" == "xc" ]; then
        # for case $OP_FLAG is equal to 'c'
        local diff_lib=`cat $DEP_LIBS_REAL $LIBS | sort | uniq -u`

        if [ -n "$diff_lib" ]; then
            pr_error "following libraries have not been used:"
            echo -e "$diff_lib"
        else
            echo -e "Check result: \033[32m==RIGHT==\033[0m"
        fi
    fi
}

# downsize_rootfs: remove unused libraries
downsize_rootfs()
{
    # remove_unused_all_libs
    remove_unused_libs

    rm -rf $ROOT_DIR/.*.list
}

################################################################################
#                                                                              #
#                                   MAIN                                       #
#                                                                              #
################################################################################

if [ $# -ne 2 ]; then
    help_info
    exit
fi

if [ "x$OP_FLAG" != "xd" ]; then
    if [ "x$OP_FLAG" != "xc" ]; then
        help_info
        pr_error " <op_flag>: '$OP_FLAG' should be 'd' or 'c'"
        exit
    fi
fi

if [ ! -d "$ROOT_DIR" ]; then
    help_info
    pr_error " <root_dir>: '$ROOT_DIR' does not exist!"
    exit
fi

if [ "x$OP_FLAG" == "xd" ]; then
    if [ -d "${ROOT_DIR}-tmp" ]; then
        echo "Directory ${ROOT_DIR}-tmp is exist!"
        cp -rf $ROOT_DIR/. ${ROOT_DIR}-tmp
    else
        echo "Directory ${ROOT_DIR}-tmp is not exist, back it up!"
        cp -narf $ROOT_DIR ${ROOT_DIR}-tmp
    fi
fi

downsize_prepare

get_progs_depend_libs

get_specific_used_libs

get_lib_depend_libs

downsize_rootfs

