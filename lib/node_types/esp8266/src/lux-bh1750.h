// lux-bh1750.h
// light sensor based on bh1750 i2c chip

#ifndef _ULNOIOT_BH1750_H_
#define _ULNOIOT_BH1750_H_

#include <Arduino.h>
#include <Wire.h>
#include <i2c_device.h>

#include <BH1750.h>

class Lux_BH1750 : public I2C_Device {
    private:
        BH1750 *sensor = NULL;
    public:
        Lux_BH1750(const char* name);
        void start();
        bool measure();
};

#endif // _ULNOIOT_BH1750_H_