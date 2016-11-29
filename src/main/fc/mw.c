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

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#include "build/debug.h"

#include "blackbox/blackbox.h"

#include "common/maths.h"
#include "common/axis.h"
#include "common/utils.h"
#include "common/filter.h"

#include "drivers/light_led.h"
#include "drivers/system.h"
#include "drivers/gyro_sync.h"

#include "sensors/sensors.h"
#include "sensors/boardalignment.h"
#include "sensors/acceleration.h"
#include "sensors/gyro.h"
#include "sensors/battery.h"

#include "fc/config.h"
#include "fc/rc_controls.h"
#include "fc/rc_curves.h"
#include "fc/runtime_config.h"

#include "msp/msp_serial.h"

#include "io/beeper.h"
#include "io/motors.h"
#include "io/servos.h"
#include "io/serial.h"
#include "io/serial_cli.h"
#include "io/statusindicator.h"
#include "io/transponder_ir.h"
#include "io/asyncfatfs/asyncfatfs.h"

#include "rx/rx.h"

#include "scheduler/scheduler.h"

#include "flight/mixer.h"
#include "flight/servos.h"
#include "flight/pid.h"
#include "flight/failsafe.h"
#include "flight/gtune.h"
#include "flight/altitudehold.h"

#include "config/config_profile.h"
#include "config/config_master.h"
#include "config/feature.h"

// June 2013     V2.2-dev

enum {
    ALIGN_GYRO = 0,
    ALIGN_ACCEL = 1,
    ALIGN_MAG = 2
};


#define GYRO_WATCHDOG_DELAY 80 //  delay for gyro sync

#define AIRMODE_THOTTLE_THRESHOLD 1350 // Make configurable in the future. ~35% throttle should be fine

uint16_t cycleTime = 0;         // this is the number in micro second to achieve a full loop, it can differ a little and is taken into account in the PID loop

int16_t magHold;
int16_t headFreeModeHold;

uint8_t motorControlEnable = false;

int16_t telemTemperature1;      // gyro sensor temperature
static uint32_t disarmAt;     // Time of automatic disarm when "Don't spin the motors when armed" is enabled and auto_disarm_delay is nonzero

extern uint8_t PIDweight[3];

uint16_t filteredCycleTime;
bool isRXDataNew;
static bool armingCalibrationWasInitialised;
float setpointRate[3];
float rcInput[3];

void applyAndSaveAccelerometerTrimsDelta(rollAndPitchTrims_t *rollAndPitchTrimsDelta)
{
    masterConfig.accelerometerTrims.values.roll += rollAndPitchTrimsDelta->values.roll;
    masterConfig.accelerometerTrims.values.pitch += rollAndPitchTrimsDelta->values.pitch;

    saveConfigAndNotify();
}

#ifdef GTUNE

void updateGtuneState(void)
{
    static bool GTuneWasUsed = false;

    if (IS_RC_MODE_ACTIVE(BOXGTUNE)) {
        if (!FLIGHT_MODE(GTUNE_MODE) && ARMING_FLAG(ARMED)) {
            ENABLE_FLIGHT_MODE(GTUNE_MODE);
            init_Gtune(&currentProfile->pidProfile);
            GTuneWasUsed = true;
        }
        if (!FLIGHT_MODE(GTUNE_MODE) && !ARMING_FLAG(ARMED) && GTuneWasUsed) {
            saveConfigAndNotify();
            GTuneWasUsed = false;
        }
    } else {
        if (FLIGHT_MODE(GTUNE_MODE) && ARMING_FLAG(ARMED)) {
            DISABLE_FLIGHT_MODE(GTUNE_MODE);
        }
    }
}
#endif

bool isCalibrating()
{
#ifdef BARO
    if (sensors(SENSOR_BARO) && !isBaroCalibrationComplete()) {
        return true;
    }
#endif

    // Note: compass calibration is handled completely differently, outside of the main loop, see f.CALIBRATE_MAG

    return (!isAccelerationCalibrationComplete() && sensors(SENSOR_ACC)) || (!isGyroCalibrationComplete());
}

#define RC_RATE_INCREMENTAL 14.54f
#define RC_EXPO_POWER 3

void calculateSetpointRate(int axis, int16_t rc) {
    float angleRate, rcRate, rcSuperfactor, rcCommandf;
    uint8_t rcExpo;

    if (axis != YAW) {
        rcExpo = currentControlRateProfile->rcExpo8;
        rcRate = currentControlRateProfile->rcRate8 / 100.0f;
    } else {
        rcExpo = currentControlRateProfile->rcYawExpo8;
        rcRate = currentControlRateProfile->rcYawRate8 / 100.0f;
    }

    if (rcRate > 2.0f) rcRate = rcRate + (RC_RATE_INCREMENTAL * (rcRate - 2.0f));
    rcCommandf = rc / 500.0f;
    rcInput[axis] = ABS(rcCommandf);

    if (rcExpo) {
        float expof = rcExpo / 100.0f;
        rcCommandf = rcCommandf * powerf(rcInput[axis], RC_EXPO_POWER) * expof + rcCommandf * (1-expof);
    }

    angleRate = 200.0f * rcRate * rcCommandf;

    if (currentControlRateProfile->rates[axis]) {
        rcSuperfactor = 1.0f / (constrainf(1.0f - (ABS(rcCommandf) * (currentControlRateProfile->rates[axis] / 100.0f)), 0.01f, 1.00f));
        angleRate *= rcSuperfactor;
    }

    DEBUG_SET(DEBUG_ANGLERATE, axis, angleRate);

    setpointRate[axis] = constrainf(angleRate, -1998.0f, 1998.0f); // Rate limit protection (deg/sec)
}

void scaleRcCommandToFpvCamAngle(void) {
    //recalculate sin/cos only when masterConfig.rxConfig.fpvCamAngleDegrees changed
    static uint8_t lastFpvCamAngleDegrees = 0;
    static float cosFactor = 1.0;
    static float sinFactor = 0.0;

    if (lastFpvCamAngleDegrees != masterConfig.rxConfig.fpvCamAngleDegrees){
        lastFpvCamAngleDegrees = masterConfig.rxConfig.fpvCamAngleDegrees;
        cosFactor = cos_approx(masterConfig.rxConfig.fpvCamAngleDegrees * RAD);
        sinFactor = sin_approx(masterConfig.rxConfig.fpvCamAngleDegrees * RAD);
    }

    int16_t roll = rcCommand[ROLL];
    int16_t yaw = rcCommand[YAW];
    rcCommand[ROLL] = constrain(roll * cosFactor -  yaw * sinFactor, -500, 500);
    rcCommand[YAW]  = constrain(yaw  * cosFactor + roll * sinFactor, -500, 500);
}

void processRcCommand(void)
{
    static int16_t lastCommand[4] = { 0, 0, 0, 0 };
    static int16_t deltaRC[4] = { 0, 0, 0, 0 };
    static int16_t factor, rcInterpolationFactor;
    uint16_t rxRefreshRate;
    bool readyToCalculateRate = false;

    if (masterConfig.rxConfig.rcInterpolation || flightModeFlags) {
        if (isRXDataNew) {
            // Set RC refresh rate for sampling and channels to filter
            switch (masterConfig.rxConfig.rcInterpolation) {
                case(RC_SMOOTHING_AUTO):
                    rxRefreshRate = constrain(getTaskDeltaTime(TASK_RX), 1000, 20000) + 1000; // Add slight overhead to prevent ramps
                    break;
                case(RC_SMOOTHING_MANUAL):
                    rxRefreshRate = 1000 * masterConfig.rxConfig.rcInterpolationInterval;
                    break;
                case(RC_SMOOTHING_OFF):
                case(RC_SMOOTHING_DEFAULT):
                default:
                    rxRefreshRate = rxGetRefreshRate();
            }

            rcInterpolationFactor = rxRefreshRate / targetPidLooptime + 1;

            if (debugMode == DEBUG_RC_INTERPOLATION) {
                for (int axis = 0; axis < 2; axis++) debug[axis] = rcCommand[axis];
                debug[3] = rxRefreshRate;
            }

            for (int channel=0; channel < 4; channel++) {
                deltaRC[channel] = rcCommand[channel] -  (lastCommand[channel] - deltaRC[channel] * factor / rcInterpolationFactor);
                lastCommand[channel] = rcCommand[channel];
            }

            factor = rcInterpolationFactor - 1;
        } else {
            factor--;
        }

        // Interpolate steps of rcCommand
        if (factor > 0) {
            for (int channel=0; channel < 4; channel++) rcCommand[channel] = lastCommand[channel] - deltaRC[channel] * factor/rcInterpolationFactor;
        } else {
            factor = 0;
        }

        readyToCalculateRate = true;
    } else {
        factor = 0; // reset factor in case of level modes flip flopping
    }

    if (readyToCalculateRate || isRXDataNew) {
        // Scaling of AngleRate to camera angle (Mixing Roll and Yaw)
        if (masterConfig.rxConfig.fpvCamAngleDegrees && IS_RC_MODE_ACTIVE(BOXFPVANGLEMIX) && !FLIGHT_MODE(HEADFREE_MODE))
            scaleRcCommandToFpvCamAngle();

        for (int axis = 0; axis < 3; axis++) calculateSetpointRate(axis, rcCommand[axis]);

        isRXDataNew = false;
    }
}

void updateRcCommands(void)
{
    // PITCH & ROLL only dynamic PID adjustment,  depending on throttle value
    int32_t prop;
    if (rcData[THROTTLE] < currentControlRateProfile->tpa_breakpoint) {
        prop = 100;
    } else {
        if (rcData[THROTTLE] < 2000) {
            prop = 100 - (uint16_t)currentControlRateProfile->dynThrPID * (rcData[THROTTLE] - currentControlRateProfile->tpa_breakpoint) / (2000 - currentControlRateProfile->tpa_breakpoint);
        } else {
            prop = 100 - currentControlRateProfile->dynThrPID;
        }
    }

    for (int axis = 0; axis < 3; axis++) {
        // non coupled PID reduction scaler used in PID controller 1 and PID controller 2.
        PIDweight[axis] = prop;

        int32_t tmp = MIN(ABS(rcData[axis] - masterConfig.rxConfig.midrc), 500);
        if (axis == ROLL || axis == PITCH) {
            if (tmp > masterConfig.rcControlsConfig.deadband) {
                tmp -= masterConfig.rcControlsConfig.deadband;
            } else {
                tmp = 0;
            }
            rcCommand[axis] = tmp;
        } else if (axis == YAW) {
            if (tmp > masterConfig.rcControlsConfig.yaw_deadband) {
                tmp -= masterConfig.rcControlsConfig.yaw_deadband;
            } else {
                tmp = 0;
            }
            rcCommand[axis] = tmp * -masterConfig.rcControlsConfig.yaw_control_direction;
        }
        if (rcData[axis] < masterConfig.rxConfig.midrc) {
            rcCommand[axis] = -rcCommand[axis];
        }
    }

    int32_t tmp;
    if (feature(FEATURE_3D)) {
        tmp = constrain(rcData[THROTTLE], PWM_RANGE_MIN, PWM_RANGE_MAX);
        tmp = (uint32_t)(tmp - PWM_RANGE_MIN);
    } else {
        tmp = constrain(rcData[THROTTLE], masterConfig.rxConfig.mincheck, PWM_RANGE_MAX);
        tmp = (uint32_t)(tmp - masterConfig.rxConfig.mincheck) * PWM_RANGE_MIN / (PWM_RANGE_MAX - masterConfig.rxConfig.mincheck);
    }
    rcCommand[THROTTLE] = rcLookupThrottle(tmp);

    if (feature(FEATURE_3D) && IS_RC_MODE_ACTIVE(BOX3DDISABLESWITCH) && !failsafeIsActive()) {
        fix12_t throttleScaler = qConstruct(rcCommand[THROTTLE] - 1000, 1000);
        rcCommand[THROTTLE] = masterConfig.rxConfig.midrc + qMultiply(throttleScaler, PWM_RANGE_MAX - masterConfig.rxConfig.midrc);
    }

    if (FLIGHT_MODE(HEADFREE_MODE)) {
        const float radDiff = degreesToRadians(DECIDEGREES_TO_DEGREES(attitude.values.yaw) - headFreeModeHold);
        const float cosDiff = cos_approx(radDiff);
        const float sinDiff = sin_approx(radDiff);
        const int16_t rcCommand_PITCH = rcCommand[PITCH] * cosDiff + rcCommand[ROLL] * sinDiff;
        rcCommand[ROLL] = rcCommand[ROLL] * cosDiff - rcCommand[PITCH] * sinDiff;
        rcCommand[PITCH] = rcCommand_PITCH;
    }
}

void updateLEDs(void)
{
    if (ARMING_FLAG(ARMED)) {
        LED0_ON;
    } else {
        if (IS_RC_MODE_ACTIVE(BOXARM) == 0 || armingCalibrationWasInitialised) {
            ENABLE_ARMING_FLAG(OK_TO_ARM);
        }

        if (!STATE(SMALL_ANGLE)) {
            DISABLE_ARMING_FLAG(OK_TO_ARM);
        }

        if (isCalibrating() || (averageSystemLoadPercent > 100)) {
            warningLedFlash();
            DISABLE_ARMING_FLAG(OK_TO_ARM);
        } else {
            if (ARMING_FLAG(OK_TO_ARM)) {
                warningLedDisable();
            } else {
                warningLedFlash();
            }
        }

        warningLedUpdate();
    }
}

void mwDisarm(void)
{
    armingCalibrationWasInitialised = false;

    if (ARMING_FLAG(ARMED)) {
        DISABLE_ARMING_FLAG(ARMED);

#ifdef BLACKBOX
        if (feature(FEATURE_BLACKBOX)) {
            finishBlackbox();
        }
#endif

        beeper(BEEPER_DISARMING);      // emit disarm tone
    }
}

#define TELEMETRY_FUNCTION_MASK (FUNCTION_TELEMETRY_FRSKY | FUNCTION_TELEMETRY_HOTT | FUNCTION_TELEMETRY_LTM | FUNCTION_TELEMETRY_SMARTPORT)

static void releaseSharedTelemetryPorts(void) {
    serialPort_t *sharedPort = findSharedSerialPort(TELEMETRY_FUNCTION_MASK, FUNCTION_MSP);
    while (sharedPort) {
        mspSerialReleasePortIfAllocated(sharedPort);
        sharedPort = findNextSharedSerialPort(TELEMETRY_FUNCTION_MASK, FUNCTION_MSP);
    }
}

void mwArm(void)
{
    static bool firstArmingCalibrationWasCompleted;

    if (masterConfig.armingConfig.gyro_cal_on_first_arm && !firstArmingCalibrationWasCompleted) {
        gyroSetCalibrationCycles();
        armingCalibrationWasInitialised = true;
        firstArmingCalibrationWasCompleted = true;
    }

    if (!isGyroCalibrationComplete()) return;  // prevent arming before gyro is calibrated

    if (ARMING_FLAG(OK_TO_ARM)) {
        if (ARMING_FLAG(ARMED)) {
            return;
        }
        if (IS_RC_MODE_ACTIVE(BOXFAILSAFE)) {
            return;
        }
        if (!ARMING_FLAG(PREVENT_ARMING)) {
            ENABLE_ARMING_FLAG(ARMED);
            ENABLE_ARMING_FLAG(WAS_EVER_ARMED);
            headFreeModeHold = DECIDEGREES_TO_DEGREES(attitude.values.yaw);

#ifdef BLACKBOX
            if (feature(FEATURE_BLACKBOX)) {
                serialPort_t *sharedBlackboxAndMspPort = findSharedSerialPort(FUNCTION_BLACKBOX, FUNCTION_MSP);
                if (sharedBlackboxAndMspPort) {
                    mspSerialReleasePortIfAllocated(sharedBlackboxAndMspPort);
                }
                startBlackbox();
            }
#endif
            disarmAt = millis() + masterConfig.armingConfig.auto_disarm_delay * 1000;   // start disarm timeout, will be extended when throttle is nonzero

            //beep to indicate arming
#ifdef GPS
            if (feature(FEATURE_GPS) && STATE(GPS_FIX) && GPS_numSat >= 5)
                beeper(BEEPER_ARMING_GPS_FIX);
            else
                beeper(BEEPER_ARMING);
#else
            beeper(BEEPER_ARMING);
#endif

            return;
        }
    }

    if (!ARMING_FLAG(ARMED)) {
        beeperConfirmationBeeps(1);
    }
}

// Automatic ACC Offset Calibration
bool AccInflightCalibrationArmed = false;
bool AccInflightCalibrationMeasurementDone = false;
bool AccInflightCalibrationSavetoEEProm = false;
bool AccInflightCalibrationActive = false;
uint16_t InflightcalibratingA = 0;

void handleInflightCalibrationStickPosition(void)
{
    if (AccInflightCalibrationMeasurementDone) {
        // trigger saving into eeprom after landing
        AccInflightCalibrationMeasurementDone = false;
        AccInflightCalibrationSavetoEEProm = true;
    } else {
        AccInflightCalibrationArmed = !AccInflightCalibrationArmed;
        if (AccInflightCalibrationArmed) {
            beeper(BEEPER_ACC_CALIBRATION);
        } else {
            beeper(BEEPER_ACC_CALIBRATION_FAIL);
        }
    }
}

static void updateInflightCalibrationState(void)
{
    if (AccInflightCalibrationArmed && ARMING_FLAG(ARMED) && rcData[THROTTLE] > masterConfig.rxConfig.mincheck && !IS_RC_MODE_ACTIVE(BOXARM)) {   // Copter is airborne and you are turning it off via boxarm : start measurement
        InflightcalibratingA = 50;
        AccInflightCalibrationArmed = false;
    }
    if (IS_RC_MODE_ACTIVE(BOXCALIB)) {      // Use the Calib Option to activate : Calib = TRUE measurement started, Land and Calib = 0 measurement stored
        if (!AccInflightCalibrationActive && !AccInflightCalibrationMeasurementDone)
            InflightcalibratingA = 50;
        AccInflightCalibrationActive = true;
    } else if (AccInflightCalibrationMeasurementDone && !ARMING_FLAG(ARMED)) {
        AccInflightCalibrationMeasurementDone = false;
        AccInflightCalibrationSavetoEEProm = true;
    }
}

void updateMagHold(void)
{
    if (ABS(rcCommand[YAW]) < 15 && FLIGHT_MODE(MAG_MODE)) {
        int16_t dif = DECIDEGREES_TO_DEGREES(attitude.values.yaw) - magHold;
        if (dif <= -180)
            dif += 360;
        if (dif >= +180)
            dif -= 360;
        dif *= -masterConfig.rcControlsConfig.yaw_control_direction;
        if (STATE(SMALL_ANGLE))
            rcCommand[YAW] -= dif * currentProfile->pidProfile.P8[PIDMAG] / 30;    // 18 deg
    } else
        magHold = DECIDEGREES_TO_DEGREES(attitude.values.yaw);
}

void processRx(uint32_t currentTime)
{
    static bool armedBeeperOn = false;

    calculateRxChannelsAndUpdateFailsafe(currentTime);

    // in 3D mode, we need to be able to disarm by switch at any time
    if (feature(FEATURE_3D)) {
        if (!IS_RC_MODE_ACTIVE(BOXARM))
            mwDisarm();
    }

    updateRSSI(currentTime);

    if (feature(FEATURE_FAILSAFE)) {

        if (currentTime > FAILSAFE_POWER_ON_DELAY_US && !failsafeIsMonitoring()) {
            failsafeStartMonitoring();
        }

        failsafeUpdateState();
    }

    const throttleStatus_e throttleStatus = calculateThrottleStatus(&masterConfig.rxConfig, masterConfig.flight3DConfig.deadband3d_throttle);

    // When armed and motors aren't spinning, do beeps and then disarm
    // board after delay so users without buzzer won't lose fingers.
    // mixTable constrains motor commands, so checking  throttleStatus is enough
    if (ARMING_FLAG(ARMED)
        && feature(FEATURE_MOTOR_STOP)
        && !STATE(FIXED_WING)
        && !feature(FEATURE_3D)
        && !isAirmodeActive()
    ) {
        if (isUsingSticksForArming()) {
            if (throttleStatus == THROTTLE_LOW) {
                if (masterConfig.armingConfig.auto_disarm_delay != 0
                    && (int32_t)(disarmAt - millis()) < 0
                ) {
                    // auto-disarm configured and delay is over
                    mwDisarm();
                    armedBeeperOn = false;
                } else {
                    // still armed; do warning beeps while armed
                    beeper(BEEPER_ARMED);
                    armedBeeperOn = true;
                }
            } else {
                // throttle is not low
                if (masterConfig.armingConfig.auto_disarm_delay != 0) {
                    // extend disarm time
                    disarmAt = millis() + masterConfig.armingConfig.auto_disarm_delay * 1000;
                }

                if (armedBeeperOn) {
                    beeperSilence();
                    armedBeeperOn = false;
                }
            }
        } else {
            // arming is via AUX switch; beep while throttle low
            if (throttleStatus == THROTTLE_LOW) {
                beeper(BEEPER_ARMED);
                armedBeeperOn = true;
            } else if (armedBeeperOn) {
                beeperSilence();
                armedBeeperOn = false;
            }
        }
    }

    processRcStickPositions(&masterConfig.rxConfig, throttleStatus, masterConfig.armingConfig.disarm_kill_switch);

    pidSetStabilisationState(PID_STABILISATION_ON);

    // Prevent motor windup on when on ground
    // rcCommand in the range -500 to 500 at this point
    // create a center band slightly wider than the deadband
    const int centerBand = masterConfig.rcControlsConfig.deadband + 4;
    const int yawCenterBand = masterConfig.rcControlsConfig.yaw_deadband + 4;
    if (!isAirmodeActive() || rcCommand[THROTTLE] > masterConfig.rxConfig.airModeActivateThreshold
            || rcCommand[ROLL]  < -centerBand || rcCommand[ROLL]  > centerBand
            || rcCommand[PITCH] < -centerBand || rcCommand[PITCH] > centerBand
            || rcCommand[YAW]   < -yawCenterBand || rcCommand[YAW]   > yawCenterBand) {
        // do nothing
    } else {
        // all sticks centered and throttle low
#define COS_5_DEGREES 0.996194698091746f
        if (getCosTiltAngle() > COS_5_DEGREES) {
            // we are nearly level
            const int32_t accZ = accSmooth[Z] * 100 / acc.acc_1G;
            if ((accZ > 80) && (accZ < 110)) {
                // accelerometer  Z readings is in the range 0.80g - 1.10g, so we are not in free fall
                // so don't allow ITerm to wind-up
    /* In airmode Iterm should be prevented to grow when Low thottle and Roll + Pitch Centered.
     This is needed to prevent Iterm winding on the ground, but keep full stabilisation on 0 throttle while in air */

                pidResetErrorGyroState();
                if (currentProfile->pidProfile.pidAtMinThrottle)
                    pidSetStabilisationState(PID_STABILISATION_ZERO_ITERM);
                else
                    pidSetStabilisationState(PID_STABILISATION_OFF);
            }
        }
    }

    if (feature(FEATURE_INFLIGHT_ACC_CAL)) {
        updateInflightCalibrationState();
    }

    updateActivatedModes(masterConfig.modeActivationConditions);

    if (!cliMode) {
        updateAdjustmentStates(masterConfig.adjustmentRanges);
        processRcAdjustments(currentControlRateProfile, &masterConfig.rxConfig);
    }

    bool canUseHorizonMode = true;

    if ((IS_RC_MODE_ACTIVE(BOXANGLE) || (feature(FEATURE_FAILSAFE) && failsafeIsActive())) && (sensors(SENSOR_ACC))) {
        // bumpless transfer to Level mode
        canUseHorizonMode = false;

        if (!FLIGHT_MODE(ANGLE_MODE)) {
            ENABLE_FLIGHT_MODE(ANGLE_MODE);
        }
    } else {
        DISABLE_FLIGHT_MODE(ANGLE_MODE); // failsafe support
    }

    if (IS_RC_MODE_ACTIVE(BOXHORIZON) && canUseHorizonMode) {

        DISABLE_FLIGHT_MODE(ANGLE_MODE);

        if (!FLIGHT_MODE(HORIZON_MODE)) {
            ENABLE_FLIGHT_MODE(HORIZON_MODE);
        }
    } else {
        DISABLE_FLIGHT_MODE(HORIZON_MODE);
    }

    if (FLIGHT_MODE(ANGLE_MODE) || FLIGHT_MODE(HORIZON_MODE)) {
        LED1_ON;
    } else {
        LED1_OFF;
    }

#ifdef  MAG
    if (sensors(SENSOR_ACC) || sensors(SENSOR_MAG)) {
        if (IS_RC_MODE_ACTIVE(BOXMAG)) {
            if (!FLIGHT_MODE(MAG_MODE)) {
                ENABLE_FLIGHT_MODE(MAG_MODE);
                magHold = DECIDEGREES_TO_DEGREES(attitude.values.yaw);
            }
        } else {
            DISABLE_FLIGHT_MODE(MAG_MODE);
        }
        if (IS_RC_MODE_ACTIVE(BOXHEADFREE)) {
            if (!FLIGHT_MODE(HEADFREE_MODE)) {
                ENABLE_FLIGHT_MODE(HEADFREE_MODE);
            }
        } else {
            DISABLE_FLIGHT_MODE(HEADFREE_MODE);
        }
        if (IS_RC_MODE_ACTIVE(BOXHEADADJ)) {
            headFreeModeHold = DECIDEGREES_TO_DEGREES(attitude.values.yaw); // acquire new heading
        }
    }
#endif

#ifdef GPS
    if (sensors(SENSOR_GPS)) {
        updateGpsWaypointsAndMode();
    }
#endif

    if (IS_RC_MODE_ACTIVE(BOXPASSTHRU)) {
        ENABLE_FLIGHT_MODE(PASSTHRU_MODE);
    } else {
        DISABLE_FLIGHT_MODE(PASSTHRU_MODE);
    }

    if (masterConfig.mixerConfig.mixerMode == MIXER_FLYING_WING || masterConfig.mixerConfig.mixerMode == MIXER_AIRPLANE) {
        DISABLE_FLIGHT_MODE(HEADFREE_MODE);
    }

#ifdef TELEMETRY
    if (feature(FEATURE_TELEMETRY)) {
        if ((!masterConfig.telemetryConfig.telemetry_switch && ARMING_FLAG(ARMED)) ||
                (masterConfig.telemetryConfig.telemetry_switch && IS_RC_MODE_ACTIVE(BOXTELEMETRY))) {

            releaseSharedTelemetryPorts();
        } else {
            // the telemetry state must be checked immediately so that shared serial ports are released.
            telemetryCheckState();
            mspSerialAllocatePorts();
        }
    }
#endif

#ifdef VTX
    vtxUpdateActivatedChannel();
#endif
}

void subTaskPidController(void)
{
    uint32_t startTime;
    if (debugMode == DEBUG_PIDLOOP || debugMode == DEBUG_SCHEDULER) {startTime = micros();}
    // PID - note this is function pointer set by setPIDController()
    pidController(
        &currentProfile->pidProfile,
        masterConfig.max_angle_inclination,
        &masterConfig.accelerometerTrims,
        masterConfig.rxConfig.midrc
    );
    if (debugMode == DEBUG_PIDLOOP || debugMode == DEBUG_SCHEDULER) {debug[1] = micros() - startTime;}
}

void subTaskMainSubprocesses(void)
{

    const uint32_t startTime = micros();

    // Read out gyro temperature. can use it for something somewhere. maybe get MCU temperature instead? lots of fun possibilities.
    if (gyro.temperature) {
        gyro.temperature(&telemTemperature1);
    }

    #ifdef MAG
            if (sensors(SENSOR_MAG)) {
                updateMagHold();
            }
    #endif

    #ifdef GTUNE
            updateGtuneState();
    #endif

    #if defined(BARO) || defined(SONAR)
            // updateRcCommands sets rcCommand, which is needed by updateAltHoldState and updateSonarAltHoldState
            updateRcCommands();
            if (sensors(SENSOR_BARO) || sensors(SENSOR_SONAR)) {
                if (FLIGHT_MODE(BARO_MODE) || FLIGHT_MODE(SONAR_MODE)) {
                    applyAltHold(&masterConfig.airplaneConfig);
                }
            }
    #endif

        // If we're armed, at minimum throttle, and we do arming via the
        // sticks, do not process yaw input from the rx.  We do this so the
        // motors do not spin up while we are trying to arm or disarm.
        // Allow yaw control for tricopters if the user wants the servo to move even when unarmed.
        if (isUsingSticksForArming() && rcData[THROTTLE] <= masterConfig.rxConfig.mincheck
    #ifndef USE_QUAD_MIXER_ONLY
    #ifdef USE_SERVOS
                    && !((masterConfig.mixerConfig.mixerMode == MIXER_TRI || masterConfig.mixerConfig.mixerMode == MIXER_CUSTOM_TRI) && masterConfig.servoMixerConfig.tri_unarmed_servo)
    #endif
                    && masterConfig.mixerConfig.mixerMode != MIXER_AIRPLANE
                    && masterConfig.mixerConfig.mixerMode != MIXER_FLYING_WING
    #endif
        ) {
            rcCommand[YAW] = 0;
            setpointRate[YAW] = 0;
        }

        if (masterConfig.throttleCorrectionConfig.throttle_correction_value && (FLIGHT_MODE(ANGLE_MODE) || FLIGHT_MODE(HORIZON_MODE))) {
            rcCommand[THROTTLE] += calculateThrottleAngleCorrection(masterConfig.throttleCorrectionConfig.throttle_correction_value);
        }

        processRcCommand();

    #ifdef GPS
        if (sensors(SENSOR_GPS)) {
            if ((FLIGHT_MODE(GPS_HOME_MODE) || FLIGHT_MODE(GPS_HOLD_MODE)) && STATE(GPS_FIX_HOME)) {
                updateGpsStateForHomeAndHoldMode();
            }
        }
    #endif

    #ifdef USE_SDCARD
        afatfs_poll();
    #endif

    #ifdef BLACKBOX
        if (!cliMode && feature(FEATURE_BLACKBOX)) {
            handleBlackbox(startTime);
        }
    #endif

    #ifdef TRANSPONDER
        transponderUpdate(startTime);
    #endif
        DEBUG_SET(DEBUG_PIDLOOP, 2, micros() - startTime);
}

void subTaskMotorUpdate(void)
{
    const uint32_t startTime = micros();
    if (debugMode == DEBUG_CYCLETIME) {
        static uint32_t previousMotorUpdateTime;
        const uint32_t currentDeltaTime = startTime - previousMotorUpdateTime;
        debug[2] = currentDeltaTime;
        debug[3] = currentDeltaTime - targetPidLooptime;
        previousMotorUpdateTime = startTime;
    }

    mixTable(&currentProfile->pidProfile);

#ifdef USE_SERVOS
    // motor outputs are used as sources for servo mixing, so motors must be calculated using mixTable() before servos.
    servoTable();
    filterServos();
    writeServos();
#endif

    if (motorControlEnable) {
        writeMotors();
    }
    DEBUG_SET(DEBUG_PIDLOOP, 3, micros() - startTime);
}

uint8_t setPidUpdateCountDown(void)
{
    if (masterConfig.gyroConfig.gyro_soft_lpf_hz) {
        return masterConfig.pid_process_denom - 1;
    } else {
        return 1;
    }
}

// Function for loop trigger
void taskMainPidLoop(uint32_t currentTime)
{
    UNUSED(currentTime);

    static bool runTaskMainSubprocesses;
    static uint8_t pidUpdateCountdown;

    cycleTime = getTaskDeltaTime(TASK_SELF);

    if (debugMode == DEBUG_CYCLETIME) {
        debug[0] = cycleTime;
        debug[1] = averageSystemLoadPercent;
    }

    if (runTaskMainSubprocesses) {
        subTaskMainSubprocesses();
        runTaskMainSubprocesses = false;
    }

    // DEBUG_PIDLOOP, timings for:
    // 0 - gyroUpdate()
    // 1 - pidController()
    // 2 - subTaskMainSubprocesses()
    // 3 - subTaskMotorUpdate()
    uint32_t startTime;
    if (debugMode == DEBUG_PIDLOOP || debugMode == DEBUG_SCHEDULER) {startTime = micros();}
    gyroUpdate();
    if (debugMode == DEBUG_PIDLOOP || debugMode == DEBUG_SCHEDULER) {debug[0] = micros() - startTime;}

    if (pidUpdateCountdown) {
        pidUpdateCountdown--;
    } else {
        pidUpdateCountdown = setPidUpdateCountDown();
        subTaskPidController();
        subTaskMotorUpdate();
        runTaskMainSubprocesses = true;
    }
}
