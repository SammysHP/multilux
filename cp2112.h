#define CP2112_VID 0x10C4
#define CP2112_PID 0xEA90

enum cp2112_reports {
    RESET_DEVICE = 0x01, GPIO_CONFIG, GET_GPIO, SET_GPIO, GET_VER, SMBUS_CONFIG,
    DATA_READ = 0x10, DATA_WRITE_READ, DATA_READ_FORCE, DATA_READ_RESPONSE, DATA_WRITE, XFER_STATUS_REQ, XFER_STATUS_RESPONSE, CANCEL_TRANSFER,
    USB_LOCK = 0x20, USB_USB_CONFIG, USB_MANU_STRING, USB_PRODUCT_STRING, USB_SERIAL_STRING};

enum cp2112_status {
    // adding explicit unknown states makes this much easier to deal with
    BUS_IDLE = 0x00, BUS_BUSY, BUS_GOOD, BUS_ERROR, BUS_UNKNOWN = 0xF,
    I2C_ADDR_ACK = 0x00, I2C_ADDR_NACK, I2C_RD_INPROGRESS, I2C_WR_INPROGRESS, I2C_UNKNOWN = 0xF,
    I2C_TIMEOUT_NACK = 0x00, I2C_TIMEOUT_NF, I2C_ARB_LOST, I2C_RD_INCOMPLETE, I2C_WR_INCOMPLETE, I2C_SUCCESS};

struct cp2112_status_reply {
    enum cp2112_status mode;
    enum cp2112_status message;
    int retries;
    int length;
};

#define I2C_FAST_SPEED 400000
#define I2C_NORMAL_SPEED 100000
#define I2C_SLOW_SPEED 20000
#define I2C_W_TIMEOUT 1000
#define I2C_R_TIMEOUT 1000
#define I2C_LOW_TIMOUT 0
#define I2C_ATTEMPTS 1

#define CRC_WIDTH 8
#define CRC_MSB (1 << (CRC_WIDTH - 1))
#define CRC_POLYNOMIAL 0x07
// the X^8 term is implied

int dump_buffer(unsigned char *buf, int len);
int cancel_transfer(hid_device *handle);
int setup_gpio(hid_device *handle, int use_leds);
int setup_i2c(hid_device *handle, int speed);
int get_gpio(hid_device *handle);
int set_gpio(hid_device *handle, int values, int bitmask);
char crc_naive(char *buf, int len);
int i2c_status(hid_device *handle, struct cp2112_status_reply *status);
int i2c_write(hid_device *handle, int address, unsigned char *data, int len);
int i2c_wait(hid_device *handle);
int read_word(hid_device *handle, int address, int reg, int reply_length);

