#include "head.h"


static lv_timer_t* left_turn_signal_timer = NULL;
static lv_timer_t* right_turn_signal_timer = NULL;

static void set_light_state_by_checked(lv_obj_t* light_obj, bool state) {
  if (light_obj == NULL) return;
  if (state) {
    lv_obj_add_state(light_obj, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(light_obj, LV_STATE_CHECKED);
  }
}

static void toggle_light_by_checked(lv_obj_t* obj) {
  if (!obj) return;
  bool now_on = lv_obj_has_state(obj, LV_STATE_CHECKED);
  set_light_state_by_checked(obj, !now_on);
}

static void set_light_state_by_hidden(lv_obj_t* light_obj, bool state) {
  if (light_obj == NULL) return;
  if (state) {
    lv_obj_clear_flag(light_obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(light_obj, LV_OBJ_FLAG_HIDDEN);
  }
}

static void set_all_lights_state(bool state) {
  set_light_state_by_checked(ui_High_beam, state);
  set_light_state_by_checked(ui_seat_belt, state);
  set_light_state_by_checked(ui_Engine, state);
  set_light_state_by_checked(ui_Low_engine_oil, state);

  set_light_state_by_hidden(ui_TempWarning, state);

  lv_obj_set_style_opa(ui_Left, state ? LV_OPA_COVER : 50, 0);
  lv_obj_set_style_opa(ui_Right, state ? LV_OPA_COVER : 50, 0);
}

static void manual_flash_cb(lv_timer_t* timer) {
  lv_obj_t* signal_icon = (lv_obj_t*)timer->user_data;
  if (signal_icon) {
    lv_opa_t current_opa = lv_obj_get_style_opa(signal_icon, 0);
    if (current_opa == 255) {
      lv_obj_set_style_opa(signal_icon, 50, 0);
    } else {
      lv_obj_set_style_opa(signal_icon, 255, 0);
    }
  }
}

static void control_manual_flashing(lv_obj_t* signal_icon, bool state,
                                    lv_timer_t** timer_p) {
  if (state) {
    if (*timer_p == NULL) {
      printf("Turn signal flashing enabled\n");
      lv_obj_set_style_opa(signal_icon, LV_OPA_COVER, 0);
      *timer_p = lv_timer_create(manual_flash_cb, 500, signal_icon);
    }
  } else {
    if (*timer_p != NULL) {
      printf("Turn signal flashing disabled\n");
      lv_timer_del(*timer_p);
      *timer_p = NULL;
      lv_obj_set_style_opa(signal_icon, 50, 0);
    }
  }
}

static void startup_selftest_off_cb(lv_timer_t* timer) {
  set_all_lights_state(false);
  lv_timer_del(timer);
}

void init_all_lights_test(void) {
  set_all_lights_state(true);
  lv_timer_create(startup_selftest_off_cb, 2000, NULL);
}

static void process_command(char* cmd) {
  char target[50];
  char action[20] = {0};

  int items = sscanf(cmd, "%49s %19s", target, action);
  if (items == 1) {
    if (strcmp(target, "turn-left") == 0) {
      bool turning_on = (left_turn_signal_timer == NULL);
      if (right_turn_signal_timer != NULL) {
        control_manual_flashing(ui_Right, false, &right_turn_signal_timer);
      }
      control_manual_flashing(ui_Left, turning_on, &left_turn_signal_timer);
      return;
    } else if (strcmp(target, "turn-right") == 0) {
      bool turning_on = (right_turn_signal_timer == NULL);
      if (left_turn_signal_timer != NULL) {
        control_manual_flashing(ui_Left, false, &left_turn_signal_timer);
      }
      control_manual_flashing(ui_Right, turning_on, &right_turn_signal_timer);
      return;
    } else if (strcmp(target, "hazards") == 0) {
      bool turning_on =
          (left_turn_signal_timer == NULL && right_turn_signal_timer == NULL);
      control_manual_flashing(ui_Left, turning_on, &left_turn_signal_timer);
      control_manual_flashing(ui_Right, turning_on, &right_turn_signal_timer);
      return;
    } else if (strcmp(target, "high-beam") == 0) {
      toggle_light_by_checked(ui_High_beam);
      return;
    } else if (strcmp(target, "low-beam") == 0) {
      toggle_light_by_checked(ui_Low_beam);
      return;
    } else if (strcmp(target, "seat-belt") == 0) {
      toggle_light_by_checked(ui_seat_belt);
      return;
    }
  }

  if (items != 2) {
    printf(
        "Invalid command. Use: turn-left/turn-right/hazards/high-beam/low-beam/seat-belt\n"
        "\t\tor speed/rpm/temperature/fuel <value>\n");
    return;
  }

  if (strcmp(target, "hazards") == 0) {
    bool is_on = (strcmp(action, "on") == 0);
    control_manual_flashing(ui_Left, is_on, &left_turn_signal_timer);
    control_manual_flashing(ui_Right, is_on, &right_turn_signal_timer);
  } else if (strcmp(target, "speed") == 0) {
    parse_and_set_state_value(action, 0, 220, &g_vehicle_state.speed, "speed");
  } else if (strcmp(target, "rpm") == 0) {
    parse_and_set_state_value(action, 0, 8000, &g_vehicle_state.rpm, "rpm");
  } else if (strcmp(target, "temperature") == 0) {
    parse_and_set_state_value(action, 0, 150, &g_vehicle_state.water_temp,
                              "temperature");
  } else if (strcmp(target, "fuel") == 0) {
    parse_and_set_state_value(action, 0, 100, &g_vehicle_state.fuel_level,
                              "fuel");
  } else {
    printf("Unknown target: %s\n", target);
  }
}

void handle_console_input(void) {
  fd_set fds;
  struct timeval tv;
  int ret;

  FD_ZERO(&fds);
  FD_SET(0, &fds);

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  ret = select(1, &fds, NULL, NULL, &tv);

  if (ret > 0) {
    char buffer[100];
    ssize_t count = read(0, buffer, sizeof(buffer) - 1);
    if (count > 0) {
      buffer[count] = '\0';
      buffer[strcspn(buffer, "\n")] = 0;
      process_command(buffer);
    }
  }
}
