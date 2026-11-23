include $(TOPDIR)/rules.mk

PKG_NAME:=android-auto
PKG_VERSION:=0.3.8
PKG_RELEASE:=22
PKG_LICENSE:=LGPL-3.0-or-later
PKG_FILE_DEPENDS:=$(CURDIR)/src $(CURDIR)/files
PKG_CONFIG_DEPENDS:=CONFIG_WIRELESS_REGDB_COUNTRY
# Keep the Rust/CMake caches specific to this board and its toolchain.
PKG_BUILD_DIR:=$(COMPILE_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(BUILD_DIR)/package.mk

define Package/android-auto
  SECTION:=multimedia
  CATEGORY:=Multimedia
  TITLE:=Android Auto USB and AB5301B wireless receiver
  URL:=https://github.com/uglyoldbob/android-auto
  DEPENDS:=@TARGET_d1s_hz_b500_sd +libpthread +libgcc +libcedarx +alsa-lib +wireless-regdb
endef

define Package/android-auto/description
  Experimental USB and stock AB5301B wireless Android Auto receiver with Cedar H.264 decoding,
  LCD output, ALSA media/speech/system playback and Goodix evdev touch input.
  Microphone capture is not implemented.
  Launch cedar-headunit manually; no boot service is installed.
endef

define Package/android-auto/config
  config PACKAGE_android-auto
    select TPLAYER
    select CEDAR_VDECODER_H264
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	CEDAR_SDK_ROOT="$(TOPDIR)" CROSS_COMPILE="$(TARGET_CROSS)" \
		CARGO_TARGET_DIR="$(PKG_BUILD_DIR)/cargo-build" \
		bash $(PKG_BUILD_DIR)/tools/build-hz-b500.sh
endef

define Package/android-auto/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/share/doc/android-auto $(1)/usr/share/android-auto $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/cargo-build/riscv64gc-unknown-linux-gnu/release/cedar-headunit $(1)/usr/bin/
	$(INSTALL_BIN) ./files/android-auto-wireless $(1)/usr/bin/
	$(INSTALL_BIN) ./files/android-auto-wlan.init $(1)/etc/init.d/android-auto-wlan
	$(INSTALL_DATA) ./README.md $(1)/usr/share/doc/android-auto/
	$(INSTALL_DATA) ./files/alsa.conf $(1)/usr/share/android-auto/
	$(INSTALL_DATA) ./files/wireless.conf.example $(1)/usr/share/android-auto/
	sed 's/@WIFI_COUNTRY@/$(call qstrip,$(CONFIG_WIRELESS_REGDB_COUNTRY))/' ./files/hostapd.conf.example > $(1)/usr/share/android-auto/hostapd.conf.example
	chmod 0644 $(1)/usr/share/android-auto/hostapd.conf.example
endef

$(eval $(call BuildPackage,android-auto))
