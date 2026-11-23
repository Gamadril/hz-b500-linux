#
# Copyright (C) 2006-2012 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=lz4
PKG_VERSION:=1.9.2
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/lz4/lz4/archive/refs/tags/
PKG_MD5SUM:=3898c56c82fb3d9455aefd48db48eaad

HOST_FIXUP:=autoreconf

HOST_BUILD_PARALLEL:=1

include $(BUILD_DIR)/host-build.mk

define Download/default
  FILE:=$(PKG_SOURCE)
  URL:=$(PKG_SOURCE_URL)
  URL_FILE:=v$(PKG_VERSION).tar.gz
  MD5SUM:=$(PKG_MD5SUM)
endef

HOST_CONFIGURE_ARGS += \
		--prefix=$(STAGING_DIR_HOST) \
		--enable-shared \
		--enable-static \

define Host/Install
	DESTDIR=$(STAGING_DIR_HOST) PREFIX= $(MAKE) -C $(HOST_BUILD_DIR) install
endef

define Host/Clean
	DESTDIR=$(STAGING_DIR_HOST) PREFIX= $(MAKE) -C $(HOST_BUILD_DIR) uninstall
endef

$(eval $(call HostBuild))

