/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_B500_HANDOFF_H
#define HZ_B500_HANDOFF_H
bool hz_b500_adopting_display(void);
bool hz_b500_prepare_handoff(struct platform_device *pdev);
void hz_b500_handoff_complete(void);
u32 hz_b500_handoff_pwm_duty(void);
#endif
