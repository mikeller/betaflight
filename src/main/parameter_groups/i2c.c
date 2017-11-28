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

#include "platform.h"

#if defined(USE_I2C) && !defined(SOFT_I2C)

#include "drivers/io.h"
#include "drivers/bus_i2c.h"
#include "drivers/bus_i2c_impl.h"

#include "config/parameter_group.h"
#include "config/parameter_group_ids.h"

static const i2cDefaultConfig_t i2cDefaultConfig[] = {
#ifdef USE_I2C_DEVICE_1
    { I2CDEV_1, IO_TAG(I2C1_SCL), IO_TAG(I2C1_SDA), I2C1_OVERCLOCK, I2C1_PULLUP },
#endif
#ifdef USE_I2C_DEVICE_2
    { I2CDEV_2, IO_TAG(I2C2_SCL), IO_TAG(I2C2_SDA), I2C2_OVERCLOCK, I2C2_PULLUP },
#endif
#ifdef USE_I2C_DEVICE_3
    { I2CDEV_3, IO_TAG(I2C3_SCL), IO_TAG(I2C3_SDA), I2C3_OVERCLOCK, I2C3_PULLUP },
#endif
#ifdef USE_I2C_DEVICE_4
    { I2CDEV_4, IO_TAG(I2C4_SCL), IO_TAG(I2C4_SDA), I2C4_OVERCLOCK, I2C4_PULLUP },
#endif
};

PG_REGISTER_WITH_RESET_FN(i2cConfig_t, i2cConfig, PG_I2C_CONFIG, 0);

void pgResetFn_i2cConfig(i2cConfig_t *i2cConfig)
{
    memset(i2cConfig, 0, sizeof(*i2cConfig));

    for (size_t index = 0 ; index < ARRAYLEN(i2cDefaultConfig) ; index++) {
        const i2cDefaultConfig_t *defconf = &i2cDefaultConfig[index];
        i2cConfig->ioTagScl[defconf->device] = defconf->ioTagScl;
        i2cConfig->ioTagSda[defconf->device] = defconf->ioTagSda;
        i2cConfig->overClock[defconf->device] = defconf->overClock;
        i2cConfig->pullUp[defconf->device] = defconf->pullUp;
    }
}
#endif // defined(USE_I2C) && !defined(USE_SOFT_I2C)
