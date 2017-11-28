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

#ifdef USE_SPI

#include "drivers/bus_spi.h"
#include "drivers/dma.h"
#include "drivers/io.h"

#include "config/parameter_group.h"
#include "config/parameter_group_ids.h"

PG_REGISTER_WITH_RESET_FN(spiPinConfig_t, spiPinConfig, PG_SPI_PIN_CONFIG, 0);

typedef struct spiDefaultConfig_s {
    SPIDevice device;
    ioTag_t sck;
    ioTag_t miso;
    ioTag_t mosi;
} spiDefaultConfig_t;
#endif
