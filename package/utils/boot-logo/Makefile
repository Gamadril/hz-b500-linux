include $(TOPDIR)/rules.mk
PKG_NAME:=boot-logo
PKG_VERSION:=1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_FILE_DEPENDS:=$(CURDIR)/src $(TOPDIR)/device/config/chips/d1s/hz-b500-splash.h
include $(BUILD_DIR)/package.mk

define Package/boot-logo
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=HZ-B500 SD boot artwork converter and partition updater
  DEPENDS:=+libjpeg +libpng
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/boot-logo.c $(TOPDIR)/device/config/chips/d1s/hz-b500-splash.h $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=gnu11 -Wall -Wextra \
		-I$(STAGING_DIR)/usr/include $(PKG_BUILD_DIR)/boot-logo.c \
		$(TARGET_LDFLAGS) -ljpeg -lpng -lz -o $(PKG_BUILD_DIR)/boot-logo
endef

define Package/boot-logo/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/boot-logo $(1)/usr/bin/
endef
$(eval $(call BuildPackage,boot-logo))
