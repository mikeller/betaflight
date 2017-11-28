/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <platform.h>

#ifdef USE_PWM

#include "config/parameter_group_ids.h"

#include "drivers/io.h"

PG_REGISTER_WITH_RESET_FN(pwmConfig_t, pwmConfig, PG_PWM_CONFIG, 0);

void pgResetFn_pwmConfig(pwmConfig_t *pwmConfig)
{
    pwmConfig->inputFilteringMode = INPUT_FILTERING_DISABLED;
    int inputIndex = 0;
    for (int i = 0; i < USABLE_TIMER_CHANNEL_COUNT && inputIndex < PWM_INPUT_PORT_COUNT; i++) {
        if (timerHardware[i].usageFlags & TIM_USE_PWM) {
            pwmConfig->ioTags[inputIndex] = timerHardware[i].tag;
            inputIndex++;
        }
    }
}
#endif
