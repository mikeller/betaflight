/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

#include <platform.h>
#include "drivers/io.h"

#include "drivers/dma.h"
#include "drivers/timer.h"
#include "drivers/timer_def.h"

const timerHardware_t timerHardware[USABLE_TIMER_CHANNEL_COUNT] = {
    DEF_TIM(TIM1,  CH3, PA10, TIM_USE_PPM,   0, 0), // PPM

    DEF_TIM(TIM3,  CH1, PC6,  TIM_USE_MOTOR, 0, 0), // S1 (UP: 1, 2) (1, 4)
    DEF_TIM(TIM3,  CH2, PC7,  TIM_USE_MOTOR, 0, 0), // S2 (UP: 1, 2) (1, 5)
    DEF_TIM(TIM3,  CH3, PC8,  TIM_USE_MOTOR, 0, 0), // S3 (UP: 1, 2) (1, 7)
    DEF_TIM(TIM3,  CH4, PC9,  TIM_USE_MOTOR, 0, 0), // S4 (UP: 1, 2) (1, 2)
    DEF_TIM(TIM2,  CH3, PB2,  TIM_USE_MOTOR, 0, 0), // S5 (UP: 1, 1) (1, 1)
    DEF_TIM(TIM2,  CH1, PB10, TIM_USE_MOTOR, 0, 0), // S6 (UP: 1, 1) (1, 3)

    DEF_TIM(TIM4,  CH4, PB9,  TIM_USE_LED,   0, 0), // LED STRIP (no DMA, won't work)
//    DEF_TIM(TIM4,  CH4, PB9,  TIM_USE_PWM,   0, 0), // CAM (no DMA)
    DEF_TIM(TIM1,  CH1, PA8,  TIM_USE_ANY,   0, 0), // LED STRIP (2, 1)
};
