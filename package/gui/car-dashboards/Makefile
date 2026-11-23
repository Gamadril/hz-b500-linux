include $(TOPDIR)/rules.mk
PKG_NAME:=hz-ui
PKG_VERSION:=1.0
PKG_RELEASE:=19
PKG_LICENSE:=GPL-2.0-only MIT
PKG_FILE_DEPENDS:=$(CURDIR)/src $(CURDIR)/host $(CURDIR)/target $(CURDIR)/tools $(CURDIR)/files $(CURDIR)/media $(CURDIR)/assets
PKG_FILE_DEPENDS+=$(TOPDIR)/package/gui/littlevgl-8/lv_drivers/display/g2d_driver_enh.h
PKG_FILE_DEPENDS+=$(TOPDIR)/lichee/linux-5.4/include/video/sunxi_display2.h
PKG_FILE_DEPENDS+=$(CURDIR)/fonts/files
PKG_FILE_DEPENDS+=$(TOPDIR)/package/utils/vehicle/src
# Optional fbDOOM launcher icon, see package/games/fbdoom.
PKG_CONFIG_DEPENDS:=CONFIG_FBDOOM_SOURCE
HZ_UI_DOOM:=$(if $(call qstrip,$(CONFIG_FBDOOM_SOURCE)),-DHZ_DOOM)
include $(BUILD_DIR)/package.mk
TARGET_LDFLAGS += -Wl,-rpath-link,$(STAGING_DIR)/usr/lib

define Package/hz-ui
  SECTION:=gui
  CATEGORY:=Gui
  TITLE:=HZ-B500 HMI, dashboard and automatic CAN parking display
  DEPENDS:=@TARGET_d1s_hz_b500_sd +vehicle +hz-ui-fonts +libfreetype +libpthread +libubus +libubox +libuci +mpg123 +libcedarx +libuapi
endef

define Package/hz-ui/conffiles
/etc/config/hz-ui
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src ./host ./target ./tools ./assets $(PKG_BUILD_DIR)/
	$(CP) $(TOPDIR)/package/gui/littlevgl-8/lv_drivers/display/g2d_driver_enh.h $(PKG_BUILD_DIR)/target/
	$(CP) $(TOPDIR)/lichee/linux-5.4/include/video/sunxi_display2.h $(PKG_BUILD_DIR)/target/
endef

define Build/Configure
endef

define Build/Compile
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/art-tool
	(cd $(PKG_BUILD_DIR)/art-tool; cmake -G "Unix Makefiles" \
		-DCMAKE_C_COMPILER="$(HOSTCC_NOCACHE)" -DCMAKE_C_FLAGS="-O2 $(HZ_UI_DOOM)" \
		-DVEHICLE_SOURCE=$(TOPDIR)/package/utils/vehicle/src $(PKG_BUILD_DIR)/tools/page-art)
	$(MAKE) -C $(PKG_BUILD_DIR)/art-tool -j4
	$(PKG_BUILD_DIR)/art-tool/hz-page-art $(PKG_BUILD_DIR)/art $(CURDIR)/fonts/files
	$(MAKE) -f $(PKG_BUILD_DIR)/target/target.mk \
		SOURCE_DIR=$(PKG_BUILD_DIR) BUILD_DIR=$(PKG_BUILD_DIR)/objects \
		CC="$(TARGET_CC)" STAGING_DIR="$(STAGING_DIR)" \
		CPPFLAGS="$(TARGET_CPPFLAGS) $(HZ_UI_DOOM)" CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/hz-ui/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/libexec $(1)/etc/init.d $(1)/etc/config $(1)/usr/share/doc/hz-ui
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/objects/hz-ui $(1)/usr/bin/
	$(INSTALL_BIN) ./files/hz-ui.init $(1)/etc/init.d/hz-ui
	$(INSTALL_BIN) ./files/hz-ui-start $(1)/usr/libexec/hz-ui-start
	$(INSTALL_DIR) $(1)/etc/procd $(1)/usr/share/hz-ui/art
	touch $(1)/etc/procd/hmi-first
	xargs -r -a $(PKG_BUILD_DIR)/art/art.list -I{} $(INSTALL_DATA) $(PKG_BUILD_DIR)/art/{} $(1)/usr/share/hz-ui/art/
	$(INSTALL_CONF) ./files/hz-ui.config $(1)/etc/config/hz-ui
	$(INSTALL_DATA) ./src/dashboard/LICENSE $(1)/usr/share/doc/hz-ui/dashboard-MIT.txt
	$(INSTALL_DATA) ./src/hmi/LICENSE $(1)/usr/share/doc/hz-ui/hmi-MIT.txt
	$(INSTALL_DATA) ./src/hmi/lvgl/LICENCE.txt $(1)/usr/share/doc/hz-ui/lvgl-MIT.txt
	$(INSTALL_DIR) $(1)/usr/share/sounds/hz-ui
	if [ -f ./media/u_98o9hlkn7r-synthwave.mp3 ]; then \
		$(INSTALL_DATA) ./media/u_98o9hlkn7r-synthwave.mp3 $(1)/usr/share/sounds/hz-ui/; \
	fi
	$(INSTALL_DATA) ./media/README.md $(1)/usr/share/doc/hz-ui/audio-test-track.md
	$(INSTALL_DIR) $(1)/usr/share/videos/hz-ui
	if [ -f ./media/big_buck_bunny_720p_h264.mov ]; then \
		$(INSTALL_DATA) ./media/big_buck_bunny_720p_h264.mov $(1)/usr/share/videos/hz-ui/; \
	fi
endef
$(eval $(call BuildPackage,hz-ui))
