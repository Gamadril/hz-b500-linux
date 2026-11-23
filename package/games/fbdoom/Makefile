include $(TOPDIR)/rules.mk
PKG_NAME:=fbdoom
PKG_VERSION:=local
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-or-later
# Local fbDOOM checkout. Empty: no binary and no HMI launcher icon.
FBDOOM_SOURCE:=$(call qstrip,$(CONFIG_FBDOOM_SOURCE))
PKG_CONFIG_DEPENDS:=CONFIG_FBDOOM_SOURCE
PKG_FILE_DEPENDS:=$(CURDIR)/patches $(if $(FBDOOM_SOURCE),$(FBDOOM_SOURCE)/fbdoom)
include $(BUILD_DIR)/package.mk

define Package/fbdoom
  SECTION:=games
  CATEGORY:=Games
  TITLE:=fbDOOM framebuffer DOOM, launched from the HMI
  DEPENDS:=@TARGET_d1s_hz_b500_sd
endef

define Package/fbdoom/description
  Builds fbDOOM from a local checkout with evdev keyboard input.
  WAD files in the checkout root are installed to /usr/share/games/doom.
endef

define Package/fbdoom/config
  config FBDOOM_SOURCE
    string "Local fbDOOM checkout (empty: not integrated)"
    depends on PACKAGE_fbdoom
    default ""
endef

ifeq ($(FBDOOM_SOURCE),)
define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
endef
Build/Compile:=
Package/fbdoom/install:=true
else
# Build a copy: the checkout stays untouched.
define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) $(FBDOOM_SOURCE)/fbdoom/*.c $(FBDOOM_SOURCE)/fbdoom/*.h $(FBDOOM_SOURCE)/fbdoom/Makefile $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) NOSDL=1 CROSS_COMPILE="$(TARGET_CROSS)" CC="$(TARGET_CC)"
endef

define Package/fbdoom/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/share/games/doom
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fbdoom $(1)/usr/bin/
	for wad in $(FBDOOM_SOURCE)/*.[wW][aA][dD]; do \
		[ ! -f "$$$$wad" ] || $(INSTALL_DATA) "$$$$wad" \
			$(1)/usr/share/games/doom/$$$$(basename "$$$$wad" | tr A-Z a-z); \
	done
endef
endif
$(eval $(call BuildPackage,fbdoom))
