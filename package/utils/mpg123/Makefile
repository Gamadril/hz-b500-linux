include $(TOPDIR)/rules.mk

PKG_NAME:=mpg123
PKG_VERSION:=1.33.7
PKG_RELEASE:=1
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=https://www.mpg123.de/download
PKG_MD5SUM:=46ceab9bfb0f04e739f010b88de5943d
PKG_HASH:=31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee
PKG_LICENSE:=LGPL-2.1
PKG_LICENSE_FILES:=COPYING
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(BUILD_DIR)/package.mk

define Package/mpg123
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=MP3 playback with ALSA and remote control
  URL:=https://www.mpg123.de/
  DEPENDS:=+alsa-lib +libpthread
endef

CONFIGURE_ARGS += --disable-modules --with-audio=alsa --with-cpu=generic_fpu \
	--disable-network --disable-ipv6 --disable-static

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/include/{mpg123,out123,fmt123,syn123}.h $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/lib{mpg123,out123,syn123}.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/lib{mpg123,out123,syn123}.pc $(1)/usr/lib/pkgconfig/
endef

define Package/mpg123/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/lib $(1)/usr/share/doc/mpg123
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/mpg123 $(1)/usr/bin/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libmpg123.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libout123.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libsyn123.so* $(1)/usr/lib/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/COPYING $(PKG_BUILD_DIR)/AUTHORS \
		$(PKG_BUILD_DIR)/doc/README.remote $(1)/usr/share/doc/mpg123/
endef

$(eval $(call BuildPackage,mpg123))
