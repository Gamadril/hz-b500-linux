include $(TOPDIR)/rules.mk
PKG_NAME:=vehicle
PKG_VERSION:=1.0
PKG_RELEASE:=8
PKG_LICENSE:=GPL-2.0-only
PKG_FILE_DEPENDS:=$(CURDIR)/src $(CURDIR)/files $(CURDIR)/CMakeLists.txt
include $(BUILD_DIR)/package.mk

define Package/vehicle
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=SocketCAN vehicle state, PDC and wheel events over ubus
  DEPENDS:=+libubus +libubox +libblobmsg-json +ubusd
endef

define Package/vehicle/conffiles
/etc/config/vehicle
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src ./CMakeLists.txt $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

VEHICLE_CFLAGS=$(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=gnu11 -Wall -Wextra -I$(PKG_BUILD_DIR)/src -I$(STAGING_DIR)/usr/include
TARGET_LDFLAGS += -Wl,-rpath-link,$(STAGING_DIR)/usr/lib

define Build/Compile
	$(TARGET_CC) $(VEHICLE_CFLAGS) -c $(PKG_BUILD_DIR)/src/vehicle.c -o $(PKG_BUILD_DIR)/vehicle.o
	$(TARGET_CC) $(VEHICLE_CFLAGS) -c $(PKG_BUILD_DIR)/src/vehicle_calibration.c -o $(PKG_BUILD_DIR)/vehicle_calibration.o
	$(TARGET_CC) $(VEHICLE_CFLAGS) -c $(PKG_BUILD_DIR)/src/vehicle_blob.c -o $(PKG_BUILD_DIR)/vehicle_blob.o
	$(TARGET_CC) $(VEHICLE_CFLAGS) -c $(PKG_BUILD_DIR)/src/vehicle_client.c -o $(PKG_BUILD_DIR)/vehicle_client.o
	$(TARGET_AR) rcs $(PKG_BUILD_DIR)/libvehicle-core.a $(PKG_BUILD_DIR)/vehicle.o $(PKG_BUILD_DIR)/vehicle_calibration.o
	$(TARGET_AR) rcs $(PKG_BUILD_DIR)/libvehicle-client.a $(PKG_BUILD_DIR)/vehicle_blob.o $(PKG_BUILD_DIR)/vehicle_client.o
	$(TARGET_CC) $(VEHICLE_CFLAGS) $(PKG_BUILD_DIR)/src/vehicled.c $(PKG_BUILD_DIR)/src/vehicle_clock.c -L$(PKG_BUILD_DIR) -lvehicle-client -lvehicle-core -lubus -lubox $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/vehicled
	$(TARGET_CC) $(VEHICLE_CFLAGS) $(PKG_BUILD_DIR)/src/vehicle_watch.c -L$(PKG_BUILD_DIR) -lvehicle-client -lvehicle-core -lubus -lubox -lblobmsg_json $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/vehicle-watch
	$(TARGET_CC) $(VEHICLE_CFLAGS) $(PKG_BUILD_DIR)/src/vehicle_slcan.c -L$(PKG_BUILD_DIR) -lvehicle-core $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/vehicle-slcan
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include/vehicle $(1)/usr/lib
	$(CP) ./src/vehicle.h ./src/vehicle_client.h ./src/vehicle_calibration.h $(1)/usr/include/vehicle/
	$(CP) $(PKG_BUILD_DIR)/libvehicle-*.a $(1)/usr/lib/
endef

define Package/vehicle/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/vehicled $(PKG_BUILD_DIR)/vehicle-watch $(PKG_BUILD_DIR)/vehicle-slcan $(1)/usr/bin/
	$(INSTALL_BIN) ./files/vehicle.init $(1)/etc/init.d/vehicle
	$(INSTALL_CONF) ./files/vehicle.config $(1)/etc/config/vehicle
endef
$(eval $(call BuildPackage,vehicle))
