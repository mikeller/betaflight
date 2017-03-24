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

#include "inverter.h"

#ifdef USE_INVERTER
#include "io.h"
#include "io_impl.h"
#include "serial.h"

#include "fc/config.h"

static void inverterSet(IO_t pin, bool on)
{
    IOWrite(pin, on);
}

static void initInverter(ioTag_t ioTag)
{
    IO_t pin = IOGetByTag(ioTag);
    IOInit(pin, OWNER_INVERTER, 1);
    IOConfigGPIO(pin, IOCFG_OUT_PP);

    inverterSet(pin, false);
}


void initInverters(void)
{
    for (unsigned port = 0; port < SERIAL_PORT_MAX_INDEX; port++) {
        ioTag_t tag = serialPinConfig()->ioTagInv[port];
        if (tag) {
            initInverter(tag);
        }
    }
}

void enableInverter(serialPortIdentifier_e port, bool on)
{
    ioTag_t tag = serialPinConfig()->ioTagInv[SERIAL_PORT_IDENTIFIER_TO_RESOURCE_INDEX(port)];
    if (tag) {
        IO_t pin = IOGetByTag(tag);
        inverterSet(pin, on);
    }
}
#endif
