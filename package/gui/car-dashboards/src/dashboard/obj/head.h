#ifndef __HEAD_H__
#define __HEAD_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/select.h>
#include <unistd.h>

#include "../UI/ui.h"

// #include "lv_png.h"
#include "lvgl/lvgl.h"

#define WATER_TEMP_WARN 100
#define FUEL_LOW 10

typedef struct {
  int speed;       /* km/h, 0..220 */
  int rpm;         /* revolutions/minute, 0..8000 */
  int water_temp;  /* degrees Celsius, 0..150 */
  int fuel_level;  /* percent, 0..100 */

  float total_mileage; /* odometer, km */
  float trip_mileage;  /* trip distance, km */
} VehicleState;

extern VehicleState g_vehicle_state;

void update_ui_from_state(lv_timer_t *timer);

bool parse_and_set_state_value(const char *str_val, int min, int max,
                               int *target_state, const char *name);

void handle_console_input(void);
void init_all_lights_test(void);

#endif /* __HEAD_H__ */
