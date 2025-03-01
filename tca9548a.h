
extern const int tca9548a_addresses[];
extern char *tca9548a_mode_help;

struct tca9548a_state
{
    int address;
    int channel;
};

int tca9548a_select_channel(hid_device *handle, struct tca9548a_state *device, int channel, int force);
int tca9548a_check(hid_device *handle, int address, int force);

// What if another chip has an address collision with this board?
// "There is no limit to the number of bytes sent, but the last byte sent is what is in the register."
// So in theory the transaction will complete and then it will switch to a random register.
// And must be re-selected afterwards.

