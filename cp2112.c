#include <stdio.h>
#include <string.h>
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

#include <hidapi.h>
#include "cp2112.h"

int dump_buffer(unsigned char *buf, int len)
{
    int i;
    for (i=0; i<len; i++) {
        printf("0x%X ", buf[i]);
    }
    printf("\n");
    return 0;
}

int cancel_transfer(hid_device *handle)
{
    unsigned char buf[5];
    buf[0] = CANCEL_TRANSFER;
    buf[1] = 0x01;
    return hid_send_feature_report(handle, buf, 2);
}

int setup_gpio(hid_device *handle, int use_leds)
{
    unsigned char buf[10];
    // all gpio is push-pull outputs
    buf[0] = GPIO_CONFIG;
    buf[1] = 0xFF;
    buf[2] = 0xFC;
    if (use_leds) {
        buf[3] = 0x06;
    }
    else {
        buf[3] = 0x00;
    }
    buf[4] = 0x00;
    return hid_send_feature_report(handle, buf, 5);
}

int setup_i2c(hid_device *handle, int speed)
{
    unsigned char buf[20];
    buf[0] = SMBUS_CONFIG;
    buf[1] = (speed >> 24) & 0xFF;
    buf[2] = (speed >> 16) & 0xFF;
    buf[3] = (speed >>  8) & 0xFF;
    buf[4] = (speed)       & 0xFF;
    buf[5] = 0x02;  // self address
    buf[6] = 0x00;  // autosend
    buf[7]  = (I2C_W_TIMEOUT >> 8) & 0xFF;
    buf[8]  = (I2C_W_TIMEOUT)      & 0xFF;
    buf[9]  = (I2C_R_TIMEOUT >> 8) & 0xFF;
    buf[10] = (I2C_R_TIMEOUT)      & 0xFF;
    buf[11] = (I2C_LOW_TIMOUT);
    buf[12] = (I2C_ATTEMPTS  >> 8) & 0xFF;
    buf[13] = (I2C_ATTEMPTS)       & 0xFF;
    return hid_send_feature_report(handle, buf, 14);
}

int get_gpio(hid_device *handle)
{
    unsigned char buf[5];
    int res;
    buf[0] = GET_GPIO;
    buf[1] = 2;
    res = hid_get_feature_report(handle, buf, 2);
    if (res < 0) {
        return res;
    }
    if (buf[0] != GET_GPIO) {
        return -1;
    }
    return (int)buf[1];
}

int set_gpio(hid_device *handle, int values, int bitmask)
{
    unsigned char buf[5];
    buf[0] = SET_GPIO;
    buf[1] = values;
    buf[2] = bitmask;
    return hid_send_feature_report(handle, buf, 3);
    // probably should be smarter about types
    // see if this works with hid_write() for stability
}

char crc_naive(char *buf, int len)
{
    int i, bit;
    char res = 0;
    for (i=0; i<len; i++) {
        res ^= (buf[i] << (CRC_WIDTH - 8));
        for (bit=8; bit>0; bit--) {
            if (res & CRC_MSB) { 
                res = (res << 1) ^ CRC_POLYNOMIAL;
            } else {
                res = (res << 1);
            }
        }
    }
    return res;
}

int i2c_status(hid_device *handle, struct cp2112_status_reply *status)
{
    unsigned char buf[30];
    int res;
    status->mode = BUS_UNKNOWN;
    status->message = I2C_UNKNOWN;
    status->retries = 0;
    status->length = 0;
    buf[0] = XFER_STATUS_REQ;
    buf[1] = 0x01;
    res = hid_write(handle, buf, 2);
    //res = hid_send_feature_report(handle, buf, 2);
    if (res < 0) {
        return res;
    }
    memset(buf, 0, sizeof(buf));
    res = hid_read(handle, buf, 7);
    // not sure why a READ_RESPONSE comes back sometimes
    if (buf[0] != XFER_STATUS_RESPONSE) {
        return -1;
    }
    status->mode = buf[1];
    switch (status->mode) {
    case BUS_GOOD:
        status->length = (buf[5] << 8) | buf[6];
    case BUS_ERROR:
        status->retries = (buf[3] << 8) | buf[4];
    case BUS_BUSY:
        status->message = buf[2];
    default:
        break;
    }
    return 0;
}

int i2c_write(hid_device *handle, int address, unsigned char *data, int len)
{
    unsigned char buf[100];
    int res;
    struct cp2112_status_reply status;
    if (len > 95) {
        return -1;
    }
    buf[0] = DATA_WRITE;
    buf[1] = address << 1;
    buf[2] = len;
    memcpy(buf+3, data, len);
    res = hid_write(handle, buf, len+3);
    //return res;
    do {
        res = i2c_status(handle, &status);
        if (res < 0) {
            return res;
        }
    } while(status.message == I2C_WR_INPROGRESS);
    return status.message != I2C_SUCCESS;
}

int i2c_wait(hid_device *handle)
{
    struct cp2112_status_reply status;
    int res;
    do {
        res = i2c_status(handle, &status);
        if (res < 0) {
            return res;
        }
    } while (status.mode == BUS_BUSY);
    if (status.mode == BUS_IDLE) {
        return 0;
    }
    return (status.message != I2C_SUCCESS);
}

int read_word(hid_device *handle, int address, int reg, int reply_length)
{
    // performs a write-read that has been stripped down to the bare minimum for these sensors
    // setting reg to -1 will disable the register request
    unsigned char buf[10];
    struct cp2112_status_reply status;
    int res;
    buf[0] = DATA_WRITE_READ;
    buf[1] = address << 1;
    buf[2] = 0;  // reply length high byte
    buf[3] = reply_length & 0xFF;
    if (reg >= 0) {
        buf[4] = 1;  // send length
        buf[5] = reg & 0xFF;  // payload
        res = hid_write(handle, buf, 6);
    } else {
        buf[0] = DATA_READ;
        buf[4] = 0;
        buf[5] = 0;
        res = hid_write(handle, buf, 5);
    }
    if (res < 0) {
        return res;
    }

    while (1) {
        res = i2c_status(handle, &status);
        if (res < 0) {
            return res;
        }
        if (status.mode == BUS_BUSY) {
            usleep(1000);
            continue;
        }
        if (status.mode == BUS_GOOD) {
            break;
        }
        return -1;
    }
    if (status.length < reply_length) {
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    buf[0] = DATA_READ_FORCE;
    buf[1] = 0x00;
    buf[2] = reply_length & 0xFF;
    res = hid_write(handle, buf, 3);
    if (res < 0) {
        return res;
    }

    res = hid_read(handle, buf, reply_length+3);
    if (res < 0) {
        return res;
    }
    if (buf[0] == DATA_READ_RESPONSE && buf[2] != reply_length) {
        return -1;
    }
    if (buf[2] == 1) {
        return buf[3];
    }
    if (buf[2] == 2) {
        return (buf[4]<<8) + buf[3];
    }
    if (buf[2] == 3) {
        return (buf[5]<<16) + (buf[4]<<8) + buf[3];
    }
    return -1;
}

int cleanup(hid_device *handle)
{
    hid_close(handle);
    hid_exit();
#ifdef _WIN32
    system("pause");
#endif
    return 0;
}

int has_address(int address, const int address_list[])
{
    int i;
    for (i=0; address_list[i]!=END_LIST; i++) {
        if (address_list[i] == ANY_ADDRESS) {
            return true;
        }
        if (address_list[i] == address) {
            return true;
        }
    }
    return false;
}

