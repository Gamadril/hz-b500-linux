include $(TOPDIR)/rules.mk
PKG_NAME:=hz-ui-fonts
PKG_VERSION:=1.0
PKG_RELEASE:=2
PKG_LICENSE:=OFL-1.1
PKG_FILE_DEPENDS:=$(CURDIR)/files $(CURDIR)/notices
include $(BUILD_DIR)/package.mk

define Package/hz-ui-fonts
  SECTION:=gui
  CATEGORY:=Gui
  TITLE:=HZ UI system fonts (Noto Sans and Font Awesome)
endef
define Build/Compile
endef
define Package/hz-ui-fonts/install
	$(INSTALL_DIR) $(1)/usr/share/fonts/hz-ui $(1)/usr/share/doc/hz-ui-fonts
	$(INSTALL_DATA) ./files/* $(1)/usr/share/fonts/hz-ui/
	$(INSTALL_DATA) ./notices/* $(1)/usr/share/doc/hz-ui-fonts/
endef
$(eval $(call BuildPackage,hz-ui-fonts))
