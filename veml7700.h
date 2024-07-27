
#define VEML7700_ADDR 0x20

enum veml7700_registers {
    ALS_CONF = 0x00, ALS_WH, ALS_WL, VEML_POWER, ALS_DATA, UNFILTERED_DATA, ALS_INT, VEML_ID};

enum veml7700_gain {
    ALS_GAIN_1X = 0x00, ALS_GAIN_2X, ALS_GAIN_8DIV, ALS_GAIN_4DIV};

enum veml7700_integration {
    ALS_IT_100ms = 0x00, ALS_IT_200ms, ALS_IT_400ms, ALS_IT_800ms, ALS_IT_50ms = 0x8, ALS_IT_25ms = 0xC};

// use the ALS_IT enum to access this
const double veml7700_i_scale[] = {0.0672,  0.0336,   0.0168, 0.0084,  [0x8]=0.1344, [0xC]=0.2688};
const int     veml7700_int_ms[] = {100,     200,      400,    800,     [0x8]=50,     [0xC]=25};
const int    veml7700_refresh[] = {600,     700,      900,    1300,    [0x8]=550,    [0xC]=525};
// "All refresh times .... are shown in the table on the next page."
// all except for 50ms and 25ms ;_;
// it appears to be a constant 500ms?

// use the ALS_GAIN enum to access this
const double veml7700_g_scale[] = {1.0, 0.5, 8.0, 4.0};
const char *veml7700_g_str[]    = {"1", "2", "1/8", "1/4"};

// to make auto-scaling logic easier
// use the enums to access these
const int more_gain[] = {ALS_GAIN_2X, -1, ALS_GAIN_4DIV, ALS_GAIN_1X};
const int less_gain[] = {ALS_GAIN_4DIV, ALS_GAIN_1X, -1, ALS_GAIN_8DIV};
const int more_int[] = {ALS_IT_200ms, ALS_IT_400ms, ALS_IT_800ms, -1, -1, -1, -1, -1, ALS_IT_100ms, -1, -1, -1, ALS_IT_50ms};
const int less_int[] = {ALS_IT_50ms, ALS_IT_100ms, ALS_IT_200ms, ALS_IT_400ms, -1, -1, -1, -1, ALS_IT_25ms, -1, -1, -1, -1};

