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
#include "rx/rx.h"

#pragma once

#define GYRO_I_MAX 256                      // Gyro I limiter
#define YAW_P_LIMIT_MIN 100                 // Maximum value for yaw P limiter
#define YAW_P_LIMIT_MAX 500                 // Maximum value for yaw P limiter
#define YAW_JUMP_PREVENTION_LIMIT_LOW 80
#define YAW_JUMP_PREVENTION_LIMIT_HIGH 400

#define DYNAMIC_PTERM_STICK_THRESHOLD 400

typedef enum {
    PIDROLL,
    PIDPITCH,
    PIDYAW,
    PIDALT,
    PIDPOS,
    PIDPOSR,
    PIDNAVR,
    PIDLEVEL,
    PIDMAG,
    PIDVEL,
    PID_ITEM_COUNT
} pidIndex_e;

typedef enum {
    PID_CONTROLLER_INTEGER = 1,           // Integer math to gain some more performance from F1 targets
    PID_CONTROLLER_FLOAT,
    PID_COUNT
} pidControllerType_e;

typedef enum {
    DELTA_FROM_ERROR = 0,
    DELTA_FROM_MEASUREMENT
} pidDeltaType_e;

typedef enum {
    SUPEREXPO_YAW_OFF = 0,
    SUPEREXPO_YAW_ON,
    SUPEREXPO_YAW_ALWAYS
} pidSuperExpoYaw_e;

#define IS_PID_CONTROLLER_FP_BASED(pidController) (pidController == 2)

typedef struct pidProfile_s {
    uint8_t pidController;                  // 1 = rewrite from http://www.multiwii.com/forum/viewtopic.php?f=8&t=3671, 2 = Luggi09s new baseflight pid

    uint8_t P8[PID_ITEM_COUNT];
    uint8_t I8[PID_ITEM_COUNT];
    uint8_t D8[PID_ITEM_COUNT];

    uint16_t dterm_lpf_hz;                  // Delta Filter in hz
    uint16_t yaw_lpf_hz;                    // Additional yaw filter when yaw axis too noisy
    uint8_t deltaMethod;                    // Alternative delta Calculation
    uint16_t rollPitchItermIgnoreRate;      // Experimental threshold for resetting iterm for pitch and roll on certain rates
    uint16_t yawItermIgnoreRate;            // Experimental threshold for resetting iterm for yaw on certain rates
    uint16_t yaw_p_limit;
    uint8_t dterm_average_count;            // Configurable delta count for dterm
    uint8_t dynamic_pid;                    // Dynamic PID implementation (currently only P and I)

#ifdef GTUNE
    uint8_t  gtune_lolimP[3];               // [0..200] Lower limit of P during G tune
    uint8_t  gtune_hilimP[3];               // [0..200] Higher limit of P during G tune. 0 Disables tuning for that axis.
    uint8_t  gtune_pwr;                     // [0..10] Strength of adjustment
    uint16_t gtune_settle_time;             // [200..1000] Settle time in ms
    uint8_t  gtune_average_cycles;          // [8..128] Number of looptime cycles used for gyro average calculation
#endif
} pidProfile_t;

struct controlRateConfig_s;
union rollAndPitchTrims_u;
struct rxConfig_s;
typedef void (*pidControllerFuncPtr)(const pidProfile_t *pidProfile, uint16_t max_angle_inclination,
        const union rollAndPitchTrims_u *angleTrim, const struct rxConfig_s *rxConfig);            // pid controller function prototype

extern int16_t axisPID[XYZ_AXIS_COUNT];
extern int32_t axisPID_P[3], axisPID_I[3], axisPID_D[3];
bool airmodeWasActivated;
extern uint32_t targetPidLooptime;

void pidSetController(pidControllerType_e type);
void pidResetErrorGyroState(void);
void setTargetPidLooptime(uint8_t pidProcessDenom);

