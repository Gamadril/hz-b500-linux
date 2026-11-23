# Source inputs are read in place; all objects stay in BUILD_DIR.
SOURCE_DIR ?= .
BUILD_DIR ?= build
CC ?= cc
AR ?= ar
HZ_SOURCE := $(SOURCE_DIR)/src/hmi
DASH_SOURCE := $(SOURCE_DIR)/src/dashboard
override CPPFLAGS += -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DHZ_TARGET_UI -DHZ_COMBINED_UI -DHAVE_VEHICLE_UBUS -DHZ_AUDIO_PLAYBACK
override CPPFLAGS += -I$(HZ_SOURCE) -I$(HZ_SOURCE)/lvgl -I$(DASH_SOURCE) -I$(SOURCE_DIR)/host -I$(SOURCE_DIR)/target
override CPPFLAGS += -I$(STAGING_DIR)/usr/include -I$(STAGING_DIR)/usr/include/freetype2 -I$(STAGING_DIR)/usr/include/vehicle
override CPPFLAGS += -DHZ_VIDEO_PLAYBACK -DHZ_VIDEO_DYNAMIC -I$(STAGING_DIR)/usr/include/allwinner -I$(STAGING_DIR)/usr/include/allwinner/include
override CPPFLAGS += -DHZ_USE_G2D -DHZ_NATIVE_CLOCK
override CFLAGS += -std=gnu11 -ffunction-sections -fdata-sections
override LDFLAGS += -Wl,--gc-sections -Wl,--wrap=system -Wl,--wrap=fopen
LDLIBS += -lvehicle-client -lvehicle-core -lubus -lubox -luci -lfreetype -lmpg123 -lout123 -luapi -ldl -lpthread -lm
UI_SOURCES := $(shell find $(HZ_SOURCE)/lvgl/src $(DASH_SOURCE)/UI $(DASH_SOURCE)/obj -name '*.c' -type f)
UI_SOURCES += $(HZ_SOURCE)/mode/Car_Launcher.c $(HZ_SOURCE)/mode/Car_Dashboard.c
UI_SOURCES += $(addprefix $(SOURCE_DIR)/host/,navigation.c vehicle_body.c page_header.c settings.c settings_store.c gear_calibration.c gear_calibration_store.c static_scene.c transition.c page_art.c art_codec.c system_fonts.c vehicle_ui.c pdc_view.c music_player.c video_player.c video_ui.c rk_offline.c preview_guard.c bt_music.c android_auto.c)
UI_SOURCES += $(SOURCE_DIR)/target/main.c $(SOURCE_DIR)/target/linux_display.c $(SOURCE_DIR)/target/g2d_display.c $(SOURCE_DIR)/target/video_plane.c $(SOURCE_DIR)/target/settings_device.c
UI_OBJECTS := $(patsubst $(SOURCE_DIR)/%.c,$(BUILD_DIR)/%.o,$(UI_SOURCES))
.PHONY: all
all: $(BUILD_DIR)/hz-ui
$(BUILD_DIR)/hz-ui: $(UI_OBJECTS) $(STAGING_DIR)/usr/lib/libvehicle-client.a $(STAGING_DIR)/usr/lib/libvehicle-core.a
	$(CC) $(UI_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(UI_OBJECTS:.o=.d)
