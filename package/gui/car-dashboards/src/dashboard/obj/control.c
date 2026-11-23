#include "head.h"
#include "speedometer.h"
#include "coolant_gauge.h"


VehicleState g_vehicle_state = {
    .speed = 0,
    .rpm = 0,
    .water_temp = 40,
    .fuel_level = 50,

    .total_mileage = 16863.0f,
    .trip_mileage = 0.0f,
};

static inline int clamp_i(int v, int min_v, int max_v) {
  if (v < min_v) return min_v;
  if (v > max_v) return max_v;
  return v;
}
static inline float clamp_f(float v, float min_v, float max_v) {
  if (v < min_v) return min_v;
  if (v > max_v) return max_v;
  return v;
}

/* LVGL image angles use tenths of a degree. */
static void animate_img_angle_to(lv_obj_t* img, int16_t to_angle,
                                 uint32_t dur_ms) {
  if (!img) return;
  int16_t from_angle = 0;
#if LVGL_VERSION_MAJOR >= 8
  from_angle = lv_img_get_angle(img);
  /* LVGL normalizes the slightly negative speed zero angle to almost 360. */
  if (img == ui_KmPoint && from_angle > 3000) from_angle -= 3600;
#endif
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, img);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_img_set_angle);
  lv_anim_set_time(&a, dur_ms);
  lv_anim_set_values(&a, (int32_t)from_angle, (int32_t)to_angle);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static int map_value_to_angle(int value, int val_min, int val_max,
                              int angle_min, int angle_max) {
  if (val_max == val_min) return angle_min;
  float ratio = (float)(value - val_min) / (val_max - val_min);
  int angle = angle_min + (int)(ratio * (angle_max - angle_min));
  return clamp_i(angle, angle_min, angle_max);
}

static void update_all_pointers_from_state(uint32_t anim_ms) {
  animate_img_angle_to(
      ui_KmPoint, dashboard_speed_angle(g_vehicle_state.speed),
      anim_ms);
  animate_img_angle_to(
      ui_TMeterpoint, map_value_to_angle(g_vehicle_state.rpm, 0, 8000, 0, 2430),
      anim_ms);
  animate_img_angle_to(
      ui_TempPoint,
      dashboard_coolant_angle(g_vehicle_state.water_temp), anim_ms);
  animate_img_angle_to(
      ui_OilPoint,
      /* The fuel artwork has a 90-degree zero offset. */
      map_value_to_angle(g_vehicle_state.fuel_level, 0, 100, 900, 1800),
      anim_ms);
}

bool parse_and_set_state_value(const char* str_val, int min, int max,
                               int* target_state, const char* name) {
  char* endp = NULL;
  long v = strtol(str_val, &endp, 10);
  if (endp == str_val || *endp != '\0') {
    printf("Invalid value: %s\n", str_val);
    return false;
  }
  *target_state = clamp_i((int)v, min, max);
  printf("Set %s=%d\n", name, *target_state);
  return true;
}

static void update_mileage_labels(uint32_t period_ms) {
  float delta_km = (g_vehicle_state.speed / 3600000.0f) * period_ms;
  g_vehicle_state.total_mileage =
      clamp_f(g_vehicle_state.total_mileage + delta_km, 0.f, 1e9f);
  g_vehicle_state.trip_mileage =
      clamp_f(g_vehicle_state.trip_mileage + delta_km, 0.f, 1e9f);

  if (ui_ODO) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d",
             (int)(g_vehicle_state.total_mileage + 0.5f));
    lv_label_set_text(ui_ODO, buf);
  }
  if (ui_TRIP) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d",
             (int)(g_vehicle_state.trip_mileage + 0.5f));
    lv_label_set_text(ui_TRIP, buf);
  }
}
static void check_and_update_warnings(void) {
  if (ui_TempWarning) {
    if (g_vehicle_state.water_temp > WATER_TEMP_WARN)
      lv_obj_clear_flag(ui_TempWarning, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(ui_TempWarning, LV_OBJ_FLAG_HIDDEN);
  }
  if (ui_Low_engine_oil) {
    if (g_vehicle_state.fuel_level < FUEL_LOW)
      lv_obj_add_state(ui_Low_engine_oil, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(ui_Low_engine_oil, LV_STATE_CHECKED);
  }
}
void update_ui_from_state(lv_timer_t* timer) {
  uint32_t period = timer ? timer->period : 100;
  float dt_s = period / 1000.0f;
  static uint32_t self_test_end_ms = 0;
  /* Keep the two-second startup lamp test visible before applying warnings. */
  if (self_test_end_ms == 0) self_test_end_ms = lv_tick_get() + 2000;
  update_all_pointers_from_state(300);
  update_mileage_labels(period);
  if (lv_tick_get() >= self_test_end_ms) {
    check_and_update_warnings();
  }
}
