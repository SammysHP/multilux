#define VEML7700_ADDR 0x10

enum veml7700_registers {
    ALS_CONF = 0x00, ALS_WH, ALS_WL, VEML_POWER, ALS_DATA, UNFILTERED_DATA, ALS_INT, VEML_ID};

enum veml7700_gain {
    ALS_GAIN_1X = 0x00, ALS_GAIN_2X, ALS_GAIN_8DIV, ALS_GAIN_4DIV};

enum veml7700_integration {
    ALS_IT_100ms = 0x00, ALS_IT_200ms, ALS_IT_400ms, ALS_IT_800ms, ALS_IT_50ms = 0x8, ALS_IT_25ms = 0xC};

struct veml7700_state
{
    enum veml7700_gain gain;
    enum veml7700_integration integration;
    enum veml7700_gain prev_gain;
    enum veml7700_integration prev_integration;
    struct running_stats als_stats;
    struct running_stats unf_stats;
    int raw;
    double lux;
    double unf;
    char mode;
    struct timespec wait_until;
};

// use the ALS_IT enum to access this
extern const double veml7700_i_scale[];
extern const int     veml7700_int_ms[];
extern const int    veml7700_refresh[];

// use the ALS_GAIN enum to access this
extern const double veml7700_g_scale[];
extern const char *veml7700_g_str[];

// to make auto-scaling logic easier
// use the enums to access these
extern const int v7700_more_gain[];
extern const int v7700_less_gain[];
extern const int v7700_more_int[];
extern const int v7700_less_int[];

extern const int veml7700_addresses[];

extern char *veml7700_mode_help;
extern char *veml7700_debug_header;

int veml7700_sleep(hid_device *handle);
int veml7700_setup(hid_device *handle, struct veml7700_state *sensor, int force);
int veml7700_tick(struct veml7700_state *sensor);
double lame_lux_correction(double n);
double smooth_lux_correction(double n);
int veml7700_autoscale(struct veml7700_state *sensor, int raw);
int compute_lux(struct veml7700_state *sensor, int raw, int raw_unf);
int veml7700_check(hid_device *handle, int address, int force);
int veml7700_clear_stats(struct veml7700_state *sensor);
int veml7700_read(hid_device *handle, struct veml7700_state *sensor);
int veml7700_process_mode(struct veml7700_state *sensor, char c);
int veml7700_tsv_header(struct veml7700_state *sensor, FILE *f);
int veml7700_tsv_row(struct veml7700_state *sensor, FILE *f);
