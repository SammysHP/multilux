
#define MLX90614_ADDRESS  (0x5A<<1)
// though it can be set to anything

#define MLX_DEG_PER_COUNT 0.02
#define MLX_VDD_OFFSET_DEGREES -0.15
#define NO_TEMPERATURE -300

enum mlx_registers {RAW_AMB = 0x03, RAW_OBJ1, RAW_OBJ2, T_AMB, T_OBJ1, T_OBJ2,   // read-only ram registers
    TO_MAX = 0x20, TO_MIN, PWMCTRL, TA_RANGE, EMISSIVITY, CONFIG1, MLX_ADDRESS = 0x2E,  // eeprom registers
    READ_FLAGS = 0xF0, MLX_SLEEP = 0xFF};  // other commands
// "ID number" in registers 0x3C-0x3F

extern const int mlx90614_addresses[];

extern char *mlx90614_mode_help;
extern char *mlx90614_debug_header;

struct mlx90614_state
{
    int address;
    double t_amb;
    double t_obj;
    struct running_stats t_amb_stats;
    struct running_stats t_obj_stats;
    char mode;
};

double compute_celsius(int n);
int mlx90614_read(hid_device *handle, struct mlx90614_state *sensor);
int mlx90614_clear_stats(struct mlx90614_state *sensor);
int mlx90614_check(hid_device *handle, int address, int force);
int mlx90614_process_mode(struct mlx90614_state *sensor, char c);
int mlx90614_tsv_header(struct mlx90614_state *sensor, FILE *f);
int mlx90614_tsv_row(struct mlx90614_state *sensor, FILE *f);
