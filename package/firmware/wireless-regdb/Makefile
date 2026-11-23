include $(TOPDIR)/rules.mk

PKG_NAME:=wireless-regdb
PKG_VERSION:=2026.09.03
PKG_RELEASE:=1
PKG_LICENSE:=ISC
PKG_CONFIG_DEPENDS:=CONFIG_WIRELESS_REGDB_COUNTRY
PKG_FILE_DEPENDS:=$(CURDIR)/files

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.xz
PKG_SOURCE_URL:=https://mirrors.edge.kernel.org/pub/software/network/wireless-regdb/
PKG_HASH:=b22e0901227b820cd1c280abe681a15b773a5103a5e10dc442e94ebb34cbf58d

include $(BUILD_DIR)/package.mk

define Package/wireless-regdb
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Wireless regulatory database
  DEPENDS:=+iw
  URL:=https://wireless.wiki.kernel.org/en/developers/regulatory/wireless-regdb
endef

define Package/wireless-regdb/description
  The upstream regulatory.db that cfg80211 loads for country rules. Without it
  only the world domain applies and 5 GHz channels are passive (no AP).
  A boot script reloads it (built-in cfg80211 tries before the rootfs is
  mounted) and sets the configured country.
endef

define Package/wireless-regdb/config
  config WIRELESS_REGDB_COUNTRY
    string "Wi-Fi country code (ISO 3166-1 alpha-2)"
    depends on PACKAGE_wireless-regdb
    default "DE"
    help
      Regulatory country, set at boot (iw reg set) and in the hostapd example
      (country_code). It decides the permitted channels and transmit power,
      e.g. DE: 5 GHz channels 36-48 indoors without DFS.
endef

# Upstream ships the built database; nothing to compile.
define Build/Compile
endef

define Package/wireless-regdb/install
	$(INSTALL_DIR) $(1)/lib/firmware $(1)/usr/share/doc/wireless-regdb $(1)/etc/init.d
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/regulatory.db $(1)/lib/firmware/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/LICENSE $(1)/usr/share/doc/wireless-regdb/
	sed 's/@WIFI_COUNTRY@/$(call qstrip,$(CONFIG_WIRELESS_REGDB_COUNTRY))/g' ./files/regdb.init > $(1)/etc/init.d/regdb
	chmod 0755 $(1)/etc/init.d/regdb
endef

$(eval $(call BuildPackage,wireless-regdb))
