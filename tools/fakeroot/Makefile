#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk

PKG_NAME:=fakeroot
PKG_VERSION:=1.20.2

PKG_SOURCE:=$(PKG_NAME)_$(PKG_VERSION).orig.tar.bz2
PKG_SOURCE_URL:=http://snapshot.debian.org/archive/debian/20141005T221953Z/pool/main/f/fakeroot
PKG_HASH:=7c0a164d19db3efa9e802e0fc7cdfeff70ec6d26cdbdc4338c9c2823c5ea230c

include $(BUILD_DIR)/host-build.mk

HOST_CFLAGS += -std=gnu17

HOST_CONFIGURE_VARS += \
	ac_cv_header_sys_capability_h=no \
	ac_cv_func_capset=no \
	ac_cv_func___xstat=no \
	ac_cv_func__xstat=no \
	ac_cv_func___stat13=no \
	ac_cv_func___lxstat=no \
	ac_cv_func__lxstat=no \
	ac_cv_func___lstat13=no \
	ac_cv_func___fxstat=no \
	ac_cv_func__fxstat=no \
	ac_cv_func___fstat13=no \
	ac_cv_func___fxstatat=no \
	ac_cv_func__fxstatat=no \
	ac_cv_func___fstatat13=no \
	ac_cv_func___xstat64=no \
	ac_cv_func__xstat64=no \
	ac_cv_func___stat64=no \
	ac_cv_func___lxstat64=no \
	ac_cv_func__lxstat64=no \
	ac_cv_func___lstat64=no \
	ac_cv_func___fxstat64=no \
	ac_cv_func__fxstat64=no \
	ac_cv_func___fstat64=no \
	ac_cv_func___fxstatat64=no \
	ac_cv_func__fxstatat64=no \
	ac_cv_func___fstatat64=no

$(eval $(call HostBuild))
