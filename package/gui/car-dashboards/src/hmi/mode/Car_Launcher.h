#ifndef __CAR_LAUNCHER_H
#define __CAR_LAUNCHER_H

#include "lvgl/lvgl.h"

/**
 * @brief Initialize the vehicle launcher.
 * @note Call from main.c instead of Car_Dashboard_Gui().
 */
void Car_Launcher_Gui(void);

/**
 * @brief Get the launcher screen for navigation.
 * @return The launcher screen object.
 */
lv_obj_t * Car_Launcher_Get_Screen(void);

#endif /* __CAR_LAUNCHER_H */
