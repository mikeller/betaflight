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

/*
 * Created by jflyper
 */

#include "config/parameter_group.h"

typedef struct i2cDefaultConfig_s {
    I2CDevice device;
    ioTag_t ioTagScl, ioTagSda;
    bool overClock;
    bool pullUp;
} i2cDefaultConfig_t;

PG_DECLARE(i2cConfig_t, i2cConfig);
