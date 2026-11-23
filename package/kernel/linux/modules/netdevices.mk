#
# Copyright (C) 2006-2011 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

NETWORK_DEVICES_MENU:=Network Devices

define KernelPackage/ifb
  SUBMENU:=$(NETWORK_DEVICES_MENU)
  TITLE:=Intermediate Functional Block support
  KCONFIG:= \
	CONFIG_IFB \
	CONFIG_NET_CLS=y
  FILES:=$(LINUX_DIR)/drivers/net/ifb.ko
  AUTOLOAD:=$(call AutoLoad,34,ifb)
endef

define KernelPackage/ifb/description
  The Intermediate Functional Block
endef

$(eval $(call KernelPackage,ifb))

# Keep the slow SDIO firmware probe out of the HZ-B500 SD boot critical path.
# No AUTOLOAD: the service below waits for the first displayed HMI frame.
define KernelPackage/ssv6x5x
  SUBMENU:=$(NETWORK_DEVICES_MENU)
  TITLE:=HZ-B500 SSV6x5x SDIO Wi-Fi (deferred startup)
  DEPENDS:=@TARGET_d1s_hz_b500_sd
  KCONFIG:=CONFIG_SSV6X5X
  FILES:=$(LINUX_DIR)/drivers/net/wireless/ssv6x5x/ssv6x5x.ko
endef

define KernelPackage/ssv6x5x/install
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/usr/libexec
	$(INSTALL_BIN) ./files/ssv6x5x.init $(1)/etc/init.d/ssv6x5x
	$(INSTALL_BIN) ./files/ssv6x5x-start $(1)/usr/libexec/ssv6x5x-start
endef

$(eval $(call KernelPackage,ssv6x5x))
