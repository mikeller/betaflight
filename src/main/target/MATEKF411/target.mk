F411_TARGETS    += $(TARGET)
FEATURES        += SDCARD VCP 

TARGET_SRC = \
            drivers/accgyro/accgyro_spi_mpu6500.c \
            drivers/accgyro/accgyro_mpu6500.c \
            drivers/max7456.c
