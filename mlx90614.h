
#define MLX90614_ADDRESS  (0x5A<<1)
// though it can be set to anything

#define MLX_DEG_PER_COUNT 0.02
#define MLX_VDD_OFFSET_DEGREES -0.15
#define NO_TEMPERATURE -300

enum mlx_registers {RAW_AMB = 0x03, RAW_OBJ1, RAW_OBJ2, T_AMB, T_OBJ1, T_OBJ2,   // read-only ram registers
    TO_MAX = 0x20, TO_MIN, PWMCTRL, TA_RANGE, EMISSIVITY, CONFIG1, MLX_ADDRESS = 0x2E,  // eeprom registers
    READ_FLAGS = 0xF0, MLX_SLEEP = 0xFF};  // other commands
// "ID number" in registers 0x3C-0x3F

