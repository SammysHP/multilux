#include <stdio.h>
#include <hidapi.h>
#include "cp2112.h"
#include "tca9548a.h"

const int tca9548a_addresses[] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, END_LIST};

int tca9548a_select_channel(hid_device *handle, struct tca9548a_state *device, int channel, int force)
{
    unsigned char buf[5];
    int res;
    if (!force && device->channel == channel) {
        return 0;
    }
    buf[0] = 0;
    if (channel >= 0) {
        buf[0] = 1 << channel;
    }
    res = i2c_write(handle, device->address, buf, 1);
    if (res < 0) {
        return res;
    }
    device->channel = channel;
    return 0;
}

int tca9548a_check(hid_device *handle, int address, int force)
{
    int i;
    if (!force && !has_address(address, tca9548a_addresses)) {
        return false;
    }
    i = read_word(handle, address, NO_REGISTER, 1);
    return i >= 0;
}

char *tca9548a_mode_help = "The TCA9548A multiplexer has no modes.";
