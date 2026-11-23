include $(TOPDIR)/rules.mk

PKG_NAME:=ab5301b
PKG_VERSION:=1.0
PKG_RELEASE:=7
PKG_LICENSE:=GPL-2.0-only
PKG_FILE_DEPENDS:=$(CURDIR)/src $(CURDIR)/files

include $(BUILD_DIR)/package.mk

define Package/ab5301b
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=HZ-B500 AB5301B audio companion controller
endef

define Package/ab5301b/conffiles
/etc/config/ab5301b
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -std=gnu99 -Wall -Wextra \
		-o $(PKG_BUILD_DIR)/ab5301b $(PKG_BUILD_DIR)/ab5301b.c $(TARGET_LDFLAGS)
endef

define Package/ab5301b/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ab5301b $(1)/usr/bin/
	$(INSTALL_BIN) ./files/ab5301b.init $(1)/etc/init.d/ab5301b
	$(INSTALL_CONF) ./files/ab5301b.config $(1)/etc/config/ab5301b
endef

$(eval $(call BuildPackage,ab5301b))
