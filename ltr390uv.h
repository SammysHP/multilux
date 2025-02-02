
#define LTR390UV_ADDR 0x53

enum ltr390uv_registers {
    LTR_CONTROL = 0x00,
    LTR_RATE = 0x04, LTR_GAIN, LTR_ID, LTR_STATUS,
    LTR_ALS0 = 0x0D, LTR_ALS1, LTR_ALS2, LTR_UVS0, LTR_UVS1, LTR_UVS2,
    LTR_INT_CNF = 0x19, LTR_INT_PST = 0x1A,
    LTR_U0 = 0x21, LTR_U1, LTR_U2, LTR_L0, LTR_L1, LTR_L2};

enum ltr_integration {
    LTR_I_400MS = 0x0, LTR_I_200MS = 0x1 << 4, LTR_I_100MS = 0x2 << 4,
    LTR_I_50MS = 0x3 << 4, LTR_I_25MS = 0x4 << 4, LTR_I_13MS = 0x5 << 4};

enum ltr_rate {
    LTR_R_25MS = 0x0, LTR_R_50MS, LTR_R_100MS, LTR_R_200MS, LTR_R_500MS, LTR_R_1000MS,
    LTR_R_2000MS};

enum ltr_gain {
    LTR_1X = 0x0, LTR_3X, LTR_6X, LTR_9X, LTR_18X};

#define LTR_MAX_X LTR_18X
#define LTR_MIN_X LTR_1X

extern const int ltr390uv_addresses[];

extern char *ltr390uv_debug_als_header;
extern char *ltr390uv_debug_uvs_header;
extern char *ltr390uv_mode_help;

struct ltr390uv_state
{
    enum ltr_gain als_gain;
    enum ltr_integration als_integration;
    enum ltr_rate als_rate;
    enum ltr_gain uvs_gain;
    enum ltr_integration uvs_integration;
    enum ltr_rate uvs_rate;
    int uv_mode;
    struct running_stats als_stats;
    struct running_stats uvs_stats;
    int als_raw;
    int uvs_raw;
    double lux;
    double uv_uw;
    double uvi;
    char mode;
};


// use the ltr_integration enum to access this
extern int ltr_int_ms[128];
extern int ltr_more_int[128];
extern int ltr_less_int[128];

// use the ltr_rate enum to access this
extern const int ltr_faster_rate[];
extern const int ltr_slower_rate[];
extern const int ltr_rate_ms[];

// use the ltr_gain enum to access this
extern const int ltr_gain_scale[];
extern const int ltr_more_gain[];
extern const int ltr_less_gain[];

int ltr390uv_init(void);
int config_ltr390uv(hid_device *handle, int mode, int gain, int integration, int rate);
int setup_ltr390uv(hid_device *handle, struct ltr390uv_state *sensor);
int ltr390uv_clear_stats(struct ltr390uv_state *sensor);
int ltr390uv_check(hid_device *handle, int address, int force);
int ltr390uv_done(hid_device *handle);
double ltr_raw_to_uv(struct ltr390uv_state *sensor);
double ltr_raw_to_lux(struct ltr390uv_state *sensor);
int ltr390uv_read(hid_device *handle, struct ltr390uv_state *sensor);
double ltr_normalize(int raw, int integration);
int ltr_autoscale(struct ltr390uv_state *sensor);
int ltr390uv_process_mode(struct ltr390uv_state *sensor, char c);
int ltr390uv_tsv_header(struct ltr390uv_state *sensor, FILE *f);
int ltr390uv_tsv_row(struct ltr390uv_state *sensor, FILE *f);
