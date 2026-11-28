#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "driver/usb_serial_jtag.h"
#include "driver/ledc.h"
#include "freertos/queue.h"
#include "codec_board.h"
#include "codec_init.h"
#include "esp_codec_dev.h"
#include "codec_port.h"

static I2cMasterBus g_i2c_static(14, 13, 0);
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "netinet/in.h"
#include "arpa/inet.h"
#include "sys/time.h"
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"

#include "u8g2_st7305.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "dr_flac.h"
#include "freertos/idf_additions.h"
#include "user_config.h"
#include "fw_version.h"
#include "ui_theme.h"
#include "ui_audio.h"
#include "win_glyphs.h"
#include "menu_glyphs.h"
#include "word_cards.h"

#include "ink_effect_frames.h"

static const char *TAG = "stickman";

// X1K4 joystick, direct wiring documented in joystick_test/README.md
static constexpr gpio_num_t JOY_SW = GPIO_NUM_3;
static constexpr gpio_num_t K1 = GPIO_NUM_43; // blue / C, TXD header pin
static constexpr gpio_num_t K2 = GPIO_NUM_44; // green / D, RXD header pin
static constexpr gpio_num_t K3 = GPIO_NUM_17;
static constexpr gpio_num_t K4 = GPIO_NUM_18;

static constexpr int SCREEN_W = 400;
static constexpr int SCREEN_H = 300;
static constexpr int GROUND = 244;

static constexpr float PI_F = 3.14159265f;
static constexpr float DEG = PI_F / 180.0f;
static constexpr int MAX_HP = 1000;
static constexpr int MAX_GAS = 3;
static constexpr float GAS_GAIN_SCALE = 0.25f; // 4x harder than v0.7
static constexpr float GAS_PIP_REQUIREMENT = 60.0f; // 60% of the old 100-point pip

struct Pose {
    float torso = -82, head = -88;
    float uaF = 50, faF = -92;
    float uaB = 118, faB = -30;
    float thF = 52, shF = 88;
    float thB = 104, shB = 126;
    float dx = 0, dy = 0;
    float rot = 0;
};

struct Fighter {
    float x = 160;
    int facing = 1;
    float airY = 0;
    float vy = 0;
    int state = 0; // 0 idle,1 walk,2 jump,3 jab,4 kick,5 guard,6 ink,7 hook,8 high kick,9 hurt,10 sweep,11 knee,12 dash,13 air kick,14 roll,15 counter,16 grab,17 throw,18 ink burst,19 shadow rush,20 ink dragon,21 thrown,22 run,23 cross
    float t = 0;
    float walkPhase = 0;
    float flash = 0;
    int hp = MAX_HP;
    int gas = 1;
    float gasCharge = 0.0f;
    bool hitDone = false;
    bool guardHold = false;
    int dashDir = 0;
    float guardT = 0.0f;
    bool prevJump = false;
    bool prevK1 = false;
    bool prevK2 = false;
    bool prevK3 = false;
    bool prevK4 = false;
    int hitCount = 0;
    float nextHit = 0.0f;
    float moveHold = 0.0f;
};

struct Input {
    int x = 0;
    bool up = false;
    bool down = false;
    bool sw = false;
    bool k1 = false;
    bool k2 = false;
    bool k3 = false;
    bool k4 = false;
    int rawVrx = 2048;
    int rawVry = 2048;
};

static u8g2_st7305_t g_lcd;
static u8g2_t *g = nullptr;
static adc_oneshot_unit_handle_t g_adc;
static Fighter p1, dummy;
static Input in, prevIn;
static bool ko = false;
static bool g_debug_hold = false;
static bool g_ui_frame = true;
static char g_impact_text[16] = "";
static float g_impact_t = 0.0f;
static int winner = 0;
static float aiTimer = 0.8f;
static float aiInkCooldown = 2.0f;
static float g_time = 0.0f;
static char g_ota_info[48] = "192.168.4.1";
static int g_batt_mv = 0, g_batt_pct = 0;
static bool g_charging = false;
static bool g_wifi_sta = false;
static int g_rssi = -127;
static char g_clock[8] = "--:--";
static i2c_master_dev_handle_t g_rtc_dev = nullptr;
static bool g_rtc_ok = false;
static time_t g_last_time_save = 0;
static char g_ssid[33] = "";
static int64_t g_beep_end = 0;
static QueueHandle_t g_sfx_q = nullptr;
static volatile bool g_audio_ready = false;
static I2cMasterBus *g_i2c_bus = nullptr;
static CodecPort *g_codec = nullptr;
static uint8_t *g_audio_buf = nullptr;
static int g_voice_state = 0;
static volatile bool g_screen_pause = false;
static bool g_ota_started = false;

#define CAPTURE_SERVER_IP "192.168.3.39"
#define CAPTURE_SERVER_PORT 8766
#define CAP_START 1
#define CAP_DATA 2
#define CAP_END 3
#define CAP_PATH_LEN 64
static QueueHandle_t g_cap_q = nullptr; // char[CAP_PATH_LEN] temp WAV paths
static volatile bool g_capture_pause = false;
static volatile bool g_cap_connected = false;
static volatile uint32_t g_cap_sent = 0;
static volatile uint32_t g_cap_drop = 0;
static volatile bool g_vad_active = false;
static volatile uint32_t g_vad_segments = 0;
struct CapturePauseGuard {
    CapturePauseGuard() { g_capture_pause = true; vTaskDelay(pdMS_TO_TICKS(40)); }
    ~CapturePauseGuard() { g_capture_pause = false; }
};
static char g_voice_msg[64] = "C+D HOLD: TALK 60s";
static volatile bool g_cmd_recording = false;
static volatile bool g_cmd_busy = false;
static volatile bool g_cmd_stop = false;
static volatile uint32_t g_cmd_elapsed_ms = 0;
static char g_cmd_msg[64] = "C+D HOLD: TALK 60s";
static char g_fb_stage[40] = "等待语音";
static char g_fb_detail[256] = "按住 C+D 说命令；松开后这里显示执行反馈。";
static char g_fb_text[512] = "";
static char g_fb_ds[256] = "";
static char g_fb_sv[256] = "";
static char g_fb_wh[256] = "";
static char g_fb_time[24] = "";
static char g_v_text[512] = "";
static char g_v_reply[512] = "";
static char g_v_speaker[32] = "";
static int g_voice_page = 0;
static volatile int g_mic_level = 0;
static char *g_notes_buf = nullptr;
static int g_notes_scroll = 0;
static void draw_wrapped_utf8(const char *s, int x, int y, int maxw, int lineh, int maxlines);
static void notes_enter();
static void drawNotes();
static void handleNotesInput();

static bool g_dark_ui = false;
static bool g_no_invert = false;
static int g_wuli_page = 0;
static int g_wuli_sel = 0;
static float ph_v0 = 2.5f;
static float ph_h = 2.5f;
static float ph_t = 0.0f;
static float ph_x = 0.0f;
static float ph_y = 10.0f;
static bool ph_flying = false;
static bool ph_landed = false;
static float ph_tx[256];
static float ph_ty[256];
static int ph_tn = 0;
static float ph_g = 9.8f;
static int ph_speed = 2; // 0=0.1x 1=0.25x 2=1x
static uint8_t ph_overlay = 0x0F; // strobe, x-proj, compare, vectors
static bool ph_paused = false;
static int g_phys_view = 0; // 0 list, 1 projectile, 2 spring, 3 freefall
static int g_phys_cat = 0;  // 0 root, 1 middle, 2 high
static int g_phys_sel = 0;
static float sp_A = 8.0f, sp_k = 20.0f, sp_t = 0.0f;
static float sp_m = 1.0f, sp_phi = PI_F;
static bool sp_paused = false;
static int sp_speed = 1;
static uint8_t sp_overlay = 0x07;
static int sp_graph = 0;
static int sp_sel = 0;
static float ff_h = 45.0f, ff_t = 0.0f, ff_y = 45.0f;
static bool ff_run = false, ff_landed = false;
static int g_phys_off = 0;
static float um_v = 10.0f, um_t = 0.0f, um_x = 0.0f;
static bool um_run = false;
static float am_a = 2.0f, am_v0 = 0.0f, am_t = 0.0f;
static bool am_run = false;
static float pd_A = 35.0f, pd_t = 0.0f;
static bool pd_run = true;
static float fc_f1 = 30.0f, fc_f2 = 20.0f;
static int fc_ang = 60;
static float mc_vx = 4.0f, mc_vy = 3.0f, mc_t = 0.0f;
static bool mc_run = false;
static float n2_F = 10.0f, n2_m = 2.0f, n2_t = 0.0f;
static bool n2_run = false;
static float fr_F = 4.0f, fr_t = 0.0f, fr_v = 0.0f, fr_x = 0.0f;
static bool fr_run = false;
static float hk_m = 1.0f, hk_k = 20.0f;
static float pv_m1 = 1.0f, pv_m2 = 2.0f, pv_t = 0.0f;
static bool pv_run = false;
static float cm_v = 4.0f, cm_r = 1.5f, cm_t = 0.0f;
static bool cm_run = false;
static float cp_m = 1.0f, cp_v = 4.0f, cp_r = 2.0f;
static float wk_F = 10.0f, wk_s = 5.0f;
static float ke_m = 2.0f, ke_v = 5.0f;
static float pe_m = 2.0f, pe_h = 5.0f;
static float ec_m = 1.0f, ec_h = 10.0f, ec_t = 0.0f, ec_y = 10.0f, ec_v = 0.0f;
static bool ec_run = false;
static float pw_F = 10.0f, pw_v = 5.0f;
static float im_F = 10.0f, im_t = 2.0f;
static float mo_m = 2.0f, mo_v = 5.0f;
static float el_m1 = 2.0f, el_v1 = 3.0f, el_m2 = 1.0f, el_v2 = -2.0f;
static float in_m1 = 2.0f, in_v1 = 3.0f, in_m2 = 1.0f, in_v2 = 0.0f;
static float rc_m = 10.0f, rc_fuel = 1.0f, rc_ve = 50.0f;
static float oh_V = 6.0f, oh_R = 3.0f;
static float sr_R1 = 2.0f, sr_R2 = 3.0f, sr_V = 6.0f;
static float pr_R1 = 2.0f, pr_R2 = 3.0f, pr_V = 6.0f;
static float ce_E = 6.0f, ce_r = 0.5f, ce_R = 3.0f;
static float rh_V = 6.0f, rh_R0 = 2.0f, rh_Rx = 3.0f;
static float am_B = 0.5f, am_I = 2.0f, am_L = 1.0f;
static bool am_rev = false;
static float fd_speed = 1.0f, fd_t = 0.0f;
static int fd_turns = 5;
static bool fd_run = true;
static bool lz_approach = true;
static float mw_A = 22.0f, mw_f = 1.0f, mw_t = 0.0f;
static bool mw_run = true;
static float iw_sep = 50.0f, iw_lam = 24.0f;
static float rf_ang = 40.0f, rf_n1 = 1.0f, rf_n2 = 1.5f;
static float tr_ang = 45.0f, tr_n1 = 1.5f, tr_n2 = 1.0f;
static float ln_u = 30.0f, ln_f = 10.0f;
static float ds_lam = 30.0f, ds_d = 3.0f;
static float pd_ang = 40.0f;
static float ig_T = 300.0f, ig_V = 1.0f;
static float it_T = 300.0f, it_V = 1.0f;
static float th_T1 = 80.0f, th_T2 = 20.0f; static bool th_run = false;
static float ph_f = 5.0f, ph_W = 3.0f;
static float bb_T = 3000.0f;
static float rd_half = 5.0f, rd_t = 0.0f; static bool rd_run = false;
static float jb_f1 = 10.0f, jb_f2 = 10.0f;
static float jw_m = 1.0f, jw_k = 20.0f;
static float jc_f1 = 10.0f, jc_f2 = 6.0f; static bool jc_same = true;
static float js_f = 440.0f, js_t = 0.0f; static bool js_run = true;
static float jm_ang = 35.0f;
static float by_V = 1.0f, by_m = 2.0f;
static float lv_L1 = 1.0f, lv_F1 = 10.0f, lv_L2 = 2.0f;
static float av_s = 120.0f, av_t = 4.0f;
static float de_m = 54.0f, de_V = 20.0f;
static float lp_h = 0.5f;
static float ep_U = 6.0f, ep_I = 2.0f;
static float qc_q1 = 2.0f, qc_q2 = -2.0f, qc_r = 3.0f;
static float ja_R = 2.0f, ja_V = 3.0f;
static float jl_I = 2.0f, jl_R = 5.0f, jl_t = 10.0f;
static void chargeReset(); static void chargeInput(float dt); static void drawCharge();
static void ammeterReset(); static void ammeterInput(float dt); static void drawAmmeter();
static void jouleReset(); static void jouleInput(float dt); static void drawJoule();
static void avgSpeedReset(); static void avgSpeedInput(float dt); static void drawAvgSpeed();
static void densityReset(); static void densityInput(float dt); static void drawDensity();
static void liquidReset(); static void liquidInput(float dt); static void drawLiquid();
static void ePowerReset(); static void ePowerInput(float dt); static void drawEPower();
static void soundReset(); static void soundInput(float dt); static void soundUpdate(float dt); static void drawSound();
static void mirrorReset(); static void mirrorInput(float dt); static void drawMirror();
static void buoyancyReset(); static void buoyancyInput(float dt); static void drawBuoyancy();
static void leverReset(); static void leverInput(float dt); static void drawLever();
static void balanceReset(); static void balanceInput(float dt); static void drawBalance();
static void juniorWeightReset(); static void juniorWeightInput(float dt); static void drawJuniorWeight();
static void collinearReset(); static void collinearInput(float dt); static void drawCollinear();
static void idealGasReset(); static void idealGasInput(float dt); static void drawIdealGas();
static void isothermalReset(); static void isothermalInput(float dt); static void drawIsothermal();
static void thermoReset(); static void thermoInput(float dt); static void thermoUpdate(float dt); static void drawThermo();
static void photoelectricReset(); static void photoelectricInput(float dt); static void drawPhotoelectric();
static void blackbodyReset(); static void blackbodyInput(float dt); static void drawBlackbody();
static void decayReset(); static void decayInput(float dt); static void decayUpdate(float dt); static void drawDecay();
static void interfereReset(); static void interfereInput(float dt); static void drawInterfere();
static void refractReset(); static void refractInput(float dt); static void drawRefract();
static void tirReset(); static void tirInput(float dt); static void drawTir();
static void lensReset(); static void lensInput(float dt); static void drawLens();
static void doubleSlitReset(); static void doubleSlitInput(float dt); static void drawDoubleSlit();
static void prismReset(); static void prismInput(float dt); static void drawPrism();
static void ampereReset(); static void ampereInput(float dt); static void drawAmpere();
static void faradayReset(); static void faradayInput(float dt); static void faradayUpdate(float dt); static void drawFaraday();
static void lenzReset(); static void lenzInput(float dt); static void drawLenz();
static void waveReset(); static void waveInput(float dt); static void waveUpdate(float dt); static void drawWave();
static void ohmReset(); static void ohmInput(float dt); static void drawOhm();
static void seriesReset(); static void seriesInput(float dt); static void drawSeries();
static void parallelReset(); static void parallelInput(float dt); static void drawParallel();
static void closedReset(); static void closedInput(float dt); static void drawClosed();
static void rheostatReset(); static void rheostatInput(float dt); static void drawRheostat();
static void impulseReset(); static void impulseInput(float dt); static void drawImpulse();
static void momentumReset(); static void momentumInput(float dt); static void drawMomentum();
static void elasticReset(); static void elasticInput(float dt); static void drawElastic();
static void inelasticReset(); static void inelasticInput(float dt); static void drawInelastic();
static void recoilReset(); static void recoilInput(float dt); static void drawRecoil();
static void kineticReset(); static void kineticInput(float dt); static void drawKinetic();
static void potentialReset(); static void potentialInput(float dt); static void drawPotential();
static void energyReset(); static void energyInput(float dt); static void energyUpdate(float dt); static void drawEnergy();
static void powerReset(); static void powerInput(float dt); static void drawPower();
static void pulleyReset(); static void pulleyInput(float dt); static void pulleyUpdate(float dt); static void drawPulley();
static void circularReset(); static void circularInput(float dt); static void circularUpdate(float dt); static void drawCircular();
static void centripetalReset(); static void centripetalInput(float dt); static void drawCentripetal();
static void workReset(); static void workInput(float dt); static void drawWork();
static void motionCompReset(); static void motionCompInput(float dt); static void motionCompUpdate(float dt); static void drawMotionComp();
static void newtonReset(); static void newtonInput(float dt); static void newtonUpdate(float dt); static void drawNewton();
static void frictionReset(); static void frictionInput(float dt); static void frictionUpdate(float dt); static void drawFriction();
static void hookeReset(); static void hookeInput(float dt); static void drawHooke();
static void uniformReset();
static void uniformInput(float dt);
static void uniformUpdate(float dt);
static void drawUniform();
static void accelReset();
static void accelInput(float dt);
static void accelUpdate(float dt);
static void drawAccel();
static void pendulumReset();
static void pendulumInput(float dt);
static void pendulumUpdate(float dt);
static void drawPendulum();
static void forceReset();
static void forceInput(float dt);
static void drawForce();
static void physicsReset();
static void projectileInput(float dt);
static void projectileUpdate(float dt);
static void drawProjectile();
static void drawProjectileFormula();
static void drawSpringFormula();
static void drawPhysicsHint(int view) {
    (void)view;
}

static void springReset();
static void springInput(float dt);
static void springUpdate(float dt);
static void drawSpring();
static void freefallReset();
static void freefallInput(float dt);
static void freefallUpdate(float dt);
static void drawFreefall();
static void drawPhysicsMenu();
static void handlePhysicsMenuInput();
static bool g_sd_ok = false;
static bool g_sd_tried = false;
static sdmmc_card_t *g_sd_card = nullptr;

static bool sd_ensure() {
    if (g_sd_ok) return true;
    if (g_sd_tried) return false;
    g_sd_tried = true;
    esp_vfs_fat_sdmmc_mount_config_t m = {};
    m.format_if_mount_failed = false;
    m.max_files = 5;
    m.allocation_unit_size = 16 * 1024;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 20000;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_38;
    slot.cmd = GPIO_NUM_21;
    slot.d0 = GPIO_NUM_39;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &m, &g_sd_card);
    g_sd_ok = (ret == ESP_OK);
    ESP_LOGW(TAG, "SD mount: %s", esp_err_to_name(ret));
    return g_sd_ok;
}

static void save_voice_note_sd() {
    if (!sd_ensure()) return;
    time_t now = time(nullptr);
    struct tm tmv = {};
    localtime_r(&now, &tmv);
    char stamp[32];
    if (tmv.tm_year >= 120) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    else snprintf(stamp, sizeof(stamp), "%s", g_clock);
    FILE *f = fopen("/sdcard/voice_notes.txt", "a");
    if (!f) return;
    fprintf(f, "[%s] %s\n转写：%s\n建议：%s\n\n",
            stamp,
            g_v_speaker[0] ? g_v_speaker : "未知",
            g_v_text[0] ? g_v_text : "(未听清)",
            g_v_reply[0] ? g_v_reply : "(无回复)");
    fclose(f);
    ESP_LOGI(TAG, "voice note saved to SD");
}
static int g_codec_ret = -99;
static bool g_spk_ok = false;
static bool g_rec_ok = false;
static int g_rec_open_ret = -99;
static size_t g_voice_bytes = 0;
static int g_read_ret = 0;
static int g_read_calls = 0;
static int g_i2c_ret = -99;
extern int codec_debug_stage;
extern int codec_debug_err;
static bool g_es8311 = false;
static bool g_es7210 = false;
static float g_status_t = 0.0f;
static float g_hitStop = 0.0f, g_shake = 0.0f;
static int g_shakeX = 0, g_shakeY = 0;

struct HitFx {
    float x = 0, y = 0, t = 0;
    int kind = 0;
};
static HitFx g_fx[8];
static int qcfStage = 0;
static float qcfDownAt = -10.0f;
static int lastTapDir = 0;
static float lastTapAt = -10.0f;

enum { MODE_MENU = 0, MODE_GAME = 1, MODE_WORD = 2, MODE_SETTINGS = 3, MODE_VOICE = 4, MODE_NOTES = 5, MODE_PHYSICS = 6, MODE_INFO = 7, MODE_HELP = 8, MODE_FEEDBACK = 9, MODE_WIFI = 10, MODE_FILES = 11, MODE_IMAGE = 12, MODE_MUSIC = 13, MODE_SETTINGS_CTRL = 14, MODE_WORD_MENU = 15, MODE_SURVEY_MENU = 16, MODE_SURVEY = 17, MODE_SURVEY_REPORT = 18, MODE_VIDEO = 19 };
static int g_mode = MODE_MENU;
static int g_menu_sel = 0;
static int g_secret_step = 0;
static int64_t g_secret_t = 0;
static uint8_t g_volume = 100;
static uint8_t g_contrast = 128;
static int g_set_sel = 0;
static int g_wifi_view = 0;
static wifi_ap_record_t g_wifi_aps[8];
static int g_wifi_count = 0, g_wifi_sel = 0;
static char g_wifi_ssid[33] = "";
static char g_wifi_pass[65] = "";
static int g_key_page = 0, g_key_sel = 0;
static int g_file_count = 0, g_file_sel = 0, g_file_view = 0, g_file_off = 0;
static int g_info_page = 0;
static int g_settings_sel = 0;
static char g_file_names[24][40];
static uint32_t g_file_sizes[24];
static char g_file_title[40];
static char g_file_preview[1600];
static bool g_file_is_txt = false, g_file_is_wav = false;
static int g_file_scroll = 0;
static volatile bool g_wav_playing = false, g_wav_stop = false;
static volatile uint32_t g_wav_pos = 0, g_wav_total = 0;
static bool g_file_is_mp3 = false, g_file_is_gif = false;
static volatile bool g_mp3_playing = false, g_mp3_stop = false;
static volatile uint32_t g_mp3_pos = 0, g_mp3_total = 0;
static char g_audio_path[300] = "";
static uint8_t *g_img_gray = nullptr;
static int g_music_count = 0, g_music_sel = 0, g_music_off = 0;
static int g_music_repeat = 0; // 0 off, 1 all, 2 one
static bool g_music_dirty = true;
static uint32_t g_music_next_draw = 0;
static FILE *g_video_f = nullptr;
static uint16_t g_video_w = 0, g_video_h = 0, g_video_frames = 0, g_video_idx = 0;
static uint8_t g_video_fps = 0;
static bool g_video_playing = false;
static int64_t g_video_last_us = 0;
static uint8_t g_video_frame[15000];
static int g_music_files[24];
static bool g_player_running = false;
static bool g_player_paused = false;
static volatile bool g_player_stop = false;
static volatile bool g_player_finished = false;
static volatile uint32_t g_player_pos_ms = 0;
static volatile uint32_t g_player_dur_ms = 0;
static volatile int g_player_level = 0;
static volatile uint32_t g_player_gen = 0;
static volatile int g_player_stage = 0;
static char g_player_err[48] = "";
static int g_player_track = -1;
static char g_player_path[300] = "";
static volatile bool g_track_finished = false;
static int g_word_idx = 0;
static int g_word_level = 0; // 0 KET, 1 PET
static int g_word_cat_sel = 0;
static int g_survey_q = 0;
static int g_survey_ans[5] = {3,3,3,3,3};
static bool g_survey_done = false;
static char g_survey_report[1400] = "";
static bool g_word_reveal = false;
static float g_back_hold = 0.0f;

static float clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float ease(float t) {
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}

static void polar(float deg, float len, float &ox, float &oy) {
    ox = cosf(deg * DEG) * len;
    oy = sinf(deg * DEG) * len;
}

struct Point { float x, y; };

static Point add(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
static Point pol(float deg, float len) { Point p; polar(deg, len, p.x, p.y); return p; }

struct Skeleton {
    Point hip, neck, head;
    Point shF, shB, elF, elB, haF, haB;
    Point hiF, hiB, knF, knB, ftF, ftB;
};

// Proportions tuned against the 10 ink-cartwheel frames: small head,
// long legs, narrow waist, slightly wider shoulders.
static constexpr float HEAD_R = 9.0f;
static constexpr float TORSO = 36.0f;
static constexpr float UPPER_ARM = 21.0f;
static constexpr float FOREARM = 21.0f;
static constexpr float THIGH = 32.0f;
static constexpr float SHIN = 33.0f;
static constexpr float FOOT = 7.0f;
static constexpr float HIP_H = 64.0f;
static constexpr float SHOULDER_W = 20.0f;
static constexpr float HIP_W = 12.0f;

static Skeleton makeSkeleton(const Pose &p) {
    Skeleton s;
    s.hip = {0, 0};
    Point td = pol(p.torso, 1.0f);
    Point axis = {-sinf(p.torso * DEG), cosf(p.torso * DEG)};
    s.neck = {td.x * TORSO, td.y * TORSO};
    s.head = add(s.neck, pol(p.head, HEAD_R - 1.0f));
    Point shoulder = {s.neck.x - td.x * 3.0f, s.neck.y - td.y * 3.0f};
    s.shF = {shoulder.x + axis.x * SHOULDER_W * 0.5f, shoulder.y + axis.y * SHOULDER_W * 0.5f};
    s.shB = {shoulder.x - axis.x * SHOULDER_W * 0.5f, shoulder.y - axis.y * SHOULDER_W * 0.5f};
    s.hiF = {axis.x * HIP_W * 0.5f, axis.y * HIP_W * 0.5f};
    s.hiB = {-axis.x * HIP_W * 0.5f, -axis.y * HIP_W * 0.5f};
    s.elF = add(s.shF, pol(p.uaF, UPPER_ARM));
    s.haF = add(s.elF, pol(p.faF, FOREARM));
    s.elB = add(s.shB, pol(p.uaB, UPPER_ARM));
    s.haB = add(s.elB, pol(p.faB, FOREARM));
    s.knF = add(s.hiF, pol(p.thF, THIGH));
    s.ftF = add(s.knF, pol(p.shF, SHIN));
    s.knB = add(s.hiB, pol(p.thB, THIGH));
    s.ftB = add(s.knB, pol(p.shB, SHIN));

    // Whole-body rotation for jumps/cartwheels. Hip is the pivot.
    if (p.rot != 0.0f) {
        float c = cosf(p.rot * DEG), sn = sinf(p.rot * DEG);
        auto R = [&](Point q) {
            return Point{q.x * c - q.y * sn, q.x * sn + q.y * c};
        };
        s.neck = R(s.neck); s.head = R(s.head);
        s.shF = R(s.shF); s.shB = R(s.shB);
        s.elF = R(s.elF); s.elB = R(s.elB);
        s.haF = R(s.haF); s.haB = R(s.haB);
        s.hiF = R(s.hiF); s.hiB = R(s.hiB);
        s.knF = R(s.knF); s.knB = R(s.knB);
        s.ftF = R(s.ftF); s.ftB = R(s.ftB);
    }
    return s;
}

// U8G2 has 1-pixel lines. Draw 2-pixel lines for the front limbs/body.
static void brushDot(int x, int y, int r) {
    for (int oy = -r; oy <= r; ++oy)
        for (int ox = -r; ox <= r; ++ox)
            if (ox * ox + oy * oy <= r * r)
                u8g2_DrawPixel(g, x + ox, y + oy);
}

static void __attribute__((unused)) brushLine(Point a, Point b, int w) {
    int r = w / 2;
    int x0 = (int)roundf(a.x), y0 = (int)roundf(a.y);
    int x1 = (int)roundf(b.x), y1 = (int)roundf(b.y);
    for (int oy = -r; oy <= r; ++oy)
        for (int ox = -r; ox <= r; ++ox)
            if (ox * ox + oy * oy <= r * r)
                u8g2_DrawLine(g, x0 + ox, y0 + oy, x1 + ox, y1 + oy);
}

static void fillPoly(const Point *pts, int n) {
    if (n < 3) return;
    float ymin = pts[0].y, ymax = pts[0].y;
    for (int i = 1; i < n; ++i) {
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }
    int y0 = (int)floorf(ymin), y1 = (int)ceilf(ymax);
    for (int y = y0; y <= y1; ++y) {
        float xs[16]; int m = 0;
        for (int i = 0; i < n; ++i) {
            Point a = pts[i], b = pts[(i + 1) % n];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                float t = (y - a.y) / (b.y - a.y);
                if (m < 16) xs[m++] = a.x + t * (b.x - a.x);
            }
        }
        for (int i = 0; i < m; ++i)
            for (int j = i + 1; j < m; ++j)
                if (xs[j] < xs[i]) { float t = xs[i]; xs[i] = xs[j]; xs[j] = t; }
        for (int i = 0; i + 1 < m; i += 2)
            u8g2_DrawLine(g, (int)roundf(xs[i]), y, (int)roundf(xs[i+1]), y);
    }
}

static void taperedLimb(Point a, Point b, int wa, int wb, bool hollow) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) return;
    float nx = -dy / len, ny = dx / len;
    Point q[4] = {
        {a.x + nx * wa * 0.5f, a.y + ny * wa * 0.5f},
        {b.x + nx * wb * 0.5f, b.y + ny * wb * 0.5f},
        {b.x - nx * wb * 0.5f, b.y - ny * wb * 0.5f},
        {a.x - nx * wa * 0.5f, a.y - ny * wa * 0.5f}
    };
    if (hollow) {
        for (int i = 0; i < 4; ++i) {
            Point p0 = q[i], p1 = q[(i + 1) % 4];
            u8g2_DrawLine(g, (int)roundf(p0.x), (int)roundf(p0.y),
                          (int)roundf(p1.x), (int)roundf(p1.y));
        }
    } else {
        fillPoly(q, 4);
    }
}

// A white halo around a black joint makes the two tapered segments read as
// separate pieces instead of one continuous tube.
static void jointNode(Point p, int r) {
    int x = (int)roundf(p.x), y = (int)roundf(p.y);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawDisc(g, x, y, r + 2, U8G2_DRAW_ALL);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawDisc(g, x, y, r, U8G2_DRAW_ALL);
}

static void pixelFist(Point p, int facing) {
    int x = (int)roundf(p.x), y = (int)roundf(p.y);
    u8g2_DrawBox(g, x - 3, y - 3, 7, 7);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawLine(g, x - 2, y - 1, x + 2, y - 1);
    u8g2_SetDrawColor(g, 1);
    if (facing > 0) u8g2_DrawPixel(g, x + 4, y);
}

static void pixelBoot(Point ankle, int facing) {
    int x = (int)roundf(ankle.x), y = (int)roundf(ankle.y);
    u8g2_DrawBox(g, x - 4, y - 3, 8, 6);
    if (facing > 0) u8g2_DrawBox(g, x + 4, y - 1, 5, 4);
    else u8g2_DrawBox(g, x - 9, y - 1, 5, 4);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawLine(g, x - 3, y + 3, x + facing * 7, y + 3);
    u8g2_SetDrawColor(g, 1);
}

static void __attribute__((unused)) dashedLine(Point a, Point b, int w = 1) {
    int x0 = (int)roundf(a.x), y0 = (int)roundf(a.y);
    int x1 = (int)roundf(b.x), y1 = (int)roundf(b.y);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, n = 0;
    while (true) {
        if ((n++ & 3) < 2) brushDot(x0, y0, w / 2);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static Point worldPoint(const Fighter &f, Point local, float pelvisY) {
    Point p;
    p.x = f.x + local.x * f.facing + g_shakeX;
    p.y = pelvisY + local.y + g_shakeY;
    return p;
}

// Build a pose from the state machine. The skeleton math is deliberately the
// same angle convention as the JS demo: absolute angles, 0=right, 90=down.
static Pose inkPose(float t) {
    static const Pose K[10] = {
        {-82,-90, 50,-92, 118,-30, 52, 88, 104,126,  0,  0,   0},
        {-70,-70,120, 20, 150,-10, 65,120,  95,130, -4,  4,  25},
        {-48,-38,150, 70, 160, 30, 30, 90,  70,120,  8,-10, 105},
        {-32,-25,100,130, 125,100, 10, 40,  55,100, 18,-18, 200},
        {-58,-60, 55,100,  85, 80, 50, 25,  90, 55, 28, -4, 295},
        {-78,-85, 45,-70, 110,-20, 58,105, 100,128, 32,  5, 350},
        {-82,-90, 50,-92, 118,-30, 52, 88, 104,126, 34,  0, 360},
        {-82,-90, 50,-92, 118,-30, 52, 88, 104,126, 34,  0, 360},
        {-82,-90, 50,-92, 118,-30, 52, 88, 104,126, 34,  0, 360},
        {-82,-90, 50,-92, 118,-30, 52, 88, 104,126, 34,  0, 360},
    };
    float x = clampf(t / 0.95f, 0.0f, 1.0f) * 9.0f;
    int i = (int)x;
    if (i > 8) i = 8;
    float k = ease(x - i);
    const Pose &a = K[i], &b = K[i + 1];
    Pose p;
    p.torso = lerpf(a.torso, b.torso, k);
    p.head = lerpf(a.head, b.head, k);
    p.uaF = lerpf(a.uaF, b.uaF, k); p.faF = lerpf(a.faF, b.faF, k);
    p.uaB = lerpf(a.uaB, b.uaB, k); p.faB = lerpf(a.faB, b.faB, k);
    p.thF = lerpf(a.thF, b.thF, k); p.shF = lerpf(a.shF, b.shF, k);
    p.thB = lerpf(a.thB, b.thB, k); p.shB = lerpf(a.shB, b.shB, k);
    p.dx = lerpf(a.dx, b.dx, k); p.dy = lerpf(a.dy, b.dy, k);
    p.rot = lerpf(a.rot, b.rot, k);
    return p;
}

static Pose poseFor(const Fighter &f) {
    Pose p;
    float t = f.t;

    if (f.state == 6) return inkPose(t);
    if (f.state == 0) { // new static fighting stance
        p.torso = -84;
        p.head = -94;
        p.uaF = 32;  p.faF = -105;   // lead hand guards the face
        p.uaB = 108; p.faB = -70;    // rear hand near the ribs
        p.thF = 60;  p.shF = 96;     // bent lead leg
        p.thB = 104; p.shB = 126;    // rear leg
        p.dx = 0; p.dy = 0;
        return p;
    }
    if (f.state == 1) { // walk
        float sw = sinf(f.walkPhase);
        p.torso += sw * 2.0f;
        p.dx = sw * 2.0f;
        p.thF += sw * 18.0f;
        p.thB -= sw * 18.0f;
        p.shF += -sw * 10.0f;
        p.shB += sw * 10.0f;
        p.uaF -= sw * 12.0f;
        p.uaB += sw * 12.0f;
        p.faF += sw * 8.0f;
        p.faB -= sw * 8.0f;
    } else if (f.state == 2) { // jump
        p.thF = 68; p.shF = 120;
        p.thB = 88; p.shB = 125;
        p.uaF = 62; p.faF = -20;
        p.uaB = 72; p.faB = -12;
    } else if (f.state == 3) { // punch
        if (t < 0.10f) {
            float k = ease(t / 0.10f);
            p.torso = lerpf(-82, -96, k);
            p.uaF = lerpf(50, 130, k);
            p.faF = lerpf(-92, 55, k);
            p.dx = lerpf(0, -3, k);
        } else if (t < 0.20f) {
            float k = ease((t - 0.10f) / 0.10f);
            p.torso = lerpf(-96, -70, k);
            p.uaF = lerpf(130, 0, k);
            p.faF = lerpf(55, -2, k);
            p.dx = lerpf(-3, 9, k);
            p.thF = lerpf(52, 58, k);
            p.thB = lerpf(104, 112, k);
        } else {
            float k = ease((t - 0.20f) / 0.22f);
            p.torso = lerpf(-70, -82, k);
            p.uaF = lerpf(0, 50, k);
            p.faF = lerpf(-2, -92, k);
            p.dx = lerpf(9, 0, k);
        }
    } else if (f.state == 4) { // kick
        if (t < 0.12f) {
            float k = ease(t / 0.12f);
            p.torso = lerpf(-82, -96, k);
            p.thF = lerpf(52, 15, k);
            p.shF = lerpf(88, 118, k);
            p.uaF = lerpf(50, 125, k);
            p.faF = lerpf(-92, 40, k);
        } else if (t < 0.24f) {
            float k = ease((t - 0.12f) / 0.12f);
            p.torso = lerpf(-96, -88, k);
            p.thF = lerpf(15, 2, k);
            p.shF = lerpf(118, 4, k);
            p.uaF = lerpf(125, 135, k);
            p.faF = lerpf(40, 65, k);
            p.dx = lerpf(0, 4, k);
        } else {
            float k = ease((t - 0.24f) / 0.28f);
            p.torso = lerpf(-88, -82, k);
            p.thF = lerpf(2, 52, k);
            p.shF = lerpf(4, 88, k);
            p.uaF = lerpf(135, 50, k);
            p.faF = lerpf(65, -92, k);
            p.dx = lerpf(4, 0, k);
        }
    } else if (f.state == 7) { // heavy punch: longer windup, bigger body drive
        if (t < 0.16f) {
            float k = ease(t / 0.16f);
            p.torso = lerpf(-82, -101, k);
            p.uaF = lerpf(50, 135, k);
            p.faF = lerpf(-92, 60, k);
            p.dx = lerpf(0, -5, k);
            p.thF = lerpf(52, 60, k);
            p.thB = lerpf(104, 116, k);
            p.uaB = lerpf(118, 150, k);
        } else if (t < 0.32f) {
            float k = ease((t - 0.16f) / 0.16f);
            p.torso = lerpf(-101, -64, k);
            p.uaF = lerpf(135, -6, k);
            p.faF = lerpf(60, -3, k);
            p.dx = lerpf(-5, 15, k);
            p.thF = lerpf(60, 48, k);
            p.thB = lerpf(116, 124, k);
            p.uaB = lerpf(150, 140, k);
        } else {
            float k = ease((t - 0.32f) / 0.30f);
            p.torso = lerpf(-64, -82, k);
            p.uaF = lerpf(-6, 50, k);
            p.faF = lerpf(-3, -92, k);
            p.dx = lerpf(15, 0, k);
        }
    } else if (f.state == 8) { // heavy kick: deeper leg load
        if (t < 0.18f) {
            float k = ease(t / 0.18f);
            p.torso = lerpf(-82, -102, k);
            p.thF = lerpf(52, 8, k);
            p.shF = lerpf(88, 122, k);
            p.uaF = lerpf(50, 130, k);
            p.faF = lerpf(-92, 45, k);
            p.dx = lerpf(0, -4, k);
        } else if (t < 0.36f) {
            float k = ease((t - 0.18f) / 0.18f);
            p.torso = lerpf(-102, -86, k);
            p.thF = lerpf(8, -4, k);
            p.shF = lerpf(122, 0, k);
            p.uaF = lerpf(130, 140, k);
            p.faF = lerpf(45, 70, k);
            p.dx = lerpf(-4, 7, k);
            p.uaB = 150; p.faB = 72;
        } else {
            float k = ease((t - 0.36f) / 0.34f);
            p.torso = lerpf(-86, -82, k);
            p.thF = lerpf(-4, 52, k);
            p.shF = lerpf(0, 88, k);
            p.uaF = lerpf(140, 50, k);
            p.faF = lerpf(70, -92, k);
            p.dx = lerpf(7, 0, k);
        }
    } else if (f.state == 10) { // low sweep
        if (t < 0.12f) {
            float k = ease(t / 0.12f);
            p.torso = lerpf(-82, -100, k);
            p.thF = lerpf(52, 34, k);
            p.shF = lerpf(88, 72, k);
            p.thB = lerpf(104, 98, k);
            p.shB = lerpf(126, 138, k);
            p.dx = lerpf(0, -3, k);
        } else if (t < 0.30f) {
            float k = ease((t - 0.12f) / 0.18f);
            p.torso = lerpf(-100, -96, k);
            p.thF = lerpf(34, 30, k);
            p.shF = lerpf(72, 45, k);
            p.thB = 96; p.shB = 141;
            p.dx = lerpf(-3, 6, k);
            p.dy = 11;
            p.uaF = lerpf(50, 145, k);
            p.faF = lerpf(-92, 80, k);
            p.uaB = lerpf(118, 45, k);
            p.faB = lerpf(-30, -85, k);
        } else {
            float k = ease((t - 0.30f) / 0.26f);
            p.torso = lerpf(-96, -82, k);
            p.thF = lerpf(30, 52, k);
            p.shF = lerpf(45, 88, k);
            p.uaF = lerpf(145, 50, k);
            p.faF = lerpf(80, -92, k);
            p.dx = lerpf(6, 0, k);
        }
    } else if (f.state == 11) { // rising knee
        if (t < 0.16f) {
            float k = ease(t / 0.16f);
            p.torso = lerpf(-82, -78, k);
            p.thF = lerpf(52, 54, k);
            p.shF = lerpf(88, 136, k);
            p.thB = lerpf(104, 100, k);
            p.shB = lerpf(126, 134, k);
            p.dx = lerpf(0, -2, k);
            p.dy = lerpf(0, 4, k);
        } else if (t < 0.34f) {
            float k = ease((t - 0.16f) / 0.18f);
            p.torso = lerpf(-78, -94, k);
            p.thF = lerpf(54, -12, k);
            p.shF = lerpf(136, 122, k);
            p.thB = 101; p.shB = 107;
            p.dx = lerpf(-2, 7, k);
            p.dy = lerpf(4, -2, k);
            p.uaF = lerpf(50, 25, k);
            p.faF = lerpf(-92, -100, k);
            p.uaB = lerpf(118, 45, k);
            p.faB = lerpf(-30, -82, k);
        } else {
            float k = ease((t - 0.34f) / 0.32f);
            p.torso = lerpf(-94, -82, k);
            p.thF = lerpf(-12, 52, k);
            p.shF = lerpf(122, 88, k);
            p.dx = lerpf(7, 0, k);
            p.dy = lerpf(-2, 0, k);
        }
    } else if (f.state == 12) { // dash
        float k = clampf(f.t / 0.18f, 0, 1);
        int sdir = (f.dashDir == f.facing) ? 1 : -1;
        p.torso = -96 - sdir * 6;
        p.head = -92;
        p.uaF = sdir > 0 ? 130 : 40; p.faF = -55;
        p.uaB = sdir > 0 ? 45 : 125; p.faB = -35;
        p.thF = 38; p.shF = 100;
        p.thB = 104; p.shB = 128;
        p.dx = sdir * (3 + 4 * (1 - k));
    } else if (f.state == 13) { // air kick
        float k = clampf(f.t / 0.22f, 0.0f, 1.0f);
        p.torso = -96;
        p.head = -92;
        p.uaF = 112; p.faF = 30;
        p.uaB = 72;  p.faB = -20;
        p.thF = 34 - 10 * k; p.shF = 30 - 20 * k;
        p.thB = 96; p.shB = 120;
        p.dx = 5 * k;
        p.dy = -2;
    } else if (f.state == 14) { // backward roll, tucked
        float k = clampf(f.t / 0.34f, 0.0f, 1.0f);
        p.torso = -100;
        p.head = -76;
        p.uaF = 150; p.faF = 95;
        p.uaB = 42;  p.faB = 35;
        p.thF = 18; p.shF = 145;
        p.thB = 38; p.shB = 150;
        p.dx = -f.facing * 4;
        p.dy = 14;
        p.rot = f.facing * 360.0f * k;
    } else if (f.state == 15) { // counter stance
        p.torso = -96;
        p.head = -88;
        p.uaF = 34; p.faF = -120;
        p.uaB = 78; p.faB = -96;
        p.thF = 46; p.shF = 92;
        p.thB = 108; p.shB = 130;
        p.dx = -2;
    } else if (f.state == 16) { // grab reach
        float k = clampf(f.t / 0.16f, 0.0f, 1.0f);
        p.torso = -92;
        p.head = -92;
        p.uaF = 75 + 30 * k; p.faF = -90 + 55 * k;
        p.uaB = 95 + 25 * k; p.faB = -80 + 50 * k;
        p.thF = 48; p.shF = 92;
        p.thB = 104; p.shB = 126;
        p.dx = 5 * k;
    } else if (f.state == 17) { // throw
        float k = clampf(f.t / 0.22f, 0.0f, 1.0f);
        p.torso = -70;
        p.head = -70;
        p.uaF = 110 + 30 * k; p.faF = 10 - 20 * k;
        p.uaB = 125 + 25 * k; p.faB = 5 - 20 * k;
        p.thF = 38; p.shF = 78;
        p.thB = 100; p.shB = 124;
        p.dx = 4;
    } else if (f.state == 18) { // ink burst
        float k = clampf(f.t / 0.22f, 0.0f, 1.0f);
        p.torso = -102;
        p.head = -94;
        p.uaF = 45 + 90 * k; p.faF = -60 + 70 * k;
        p.uaB = 110 - 60 * k; p.faB = -30 + 70 * k;
        p.thF = 28; p.shF = 68;
        p.thB = 112; p.shB = 132;
        p.dx = -3;
        p.dy = 6;
    } else if (f.state == 19) { // shadow rush
        float k = clampf(f.t / 0.18f, 0.0f, 1.0f);
        p.torso = -98;
        p.head = -92;
        p.uaF = 132; p.faF = 42;
        p.uaB = 50;  p.faB = -55;
        p.thF = 34; p.shF = 88;
        p.thB = 106; p.shB = 126;
        p.dx = 6 + 5 * k;
    } else if (f.state == 20) { // ink dragon ultimate
        float k = clampf(f.t / 0.30f, 0.0f, 1.0f);
        p.torso = -106;
        p.head = -96;
        p.uaF = 145 - 35 * k; p.faF = 55 - 20 * k;
        p.uaB = 35 + 60 * k; p.faB = -70 + 50 * k;
        p.thF = 24; p.shF = 52;
        p.thB = 118; p.shB = 136;
        p.dx = -4 + 12 * k;
        p.dy = 4 * (1 - k);
    } else if (f.state == 22) { // run
        float s = sinf(f.walkPhase);
        p.torso = -94;
        p.head = -92;
        p.uaF = 112 - s * 20; p.faF = 20;
        p.uaB = 48 + s * 24; p.faB = -20;
        p.thF = 55 + s * 28; p.shF = 105 - s * 18;
        p.thB = 100 - s * 28; p.shB = 128 + s * 14;
        p.dx = 4;
    } else if (f.state == 23) { // second punch / cross
        if (t < 0.10f) {
            float k = ease(t / 0.10f);
            p.torso = lerpf(-84, -98, k);
            p.uaB = lerpf(118, 125, k); p.faB = lerpf(-30, 55, k);
            p.dx = lerpf(0, -3, k);
        } else if (t < 0.20f) {
            float k = ease((t - 0.10f) / 0.10f);
            p.torso = lerpf(-98, -74, k);
            p.uaB = lerpf(125, 2, k); p.faB = lerpf(55, -2, k);
            p.uaF = lerpf(50, 100, k); p.faF = lerpf(-92, -60, k);
            p.dx = lerpf(-3, 10, k);
        } else {
            float k = ease((t - 0.20f) / 0.20f);
            p.torso = lerpf(-74, -84, k);
            p.uaB = lerpf(2, 118, k); p.faB = lerpf(-2, -30, k);
            p.dx = lerpf(10, 0, k);
        }
    } else if (f.state == 21) { // victim being thrown
        float k = clampf(f.t / 0.55f, 0.0f, 1.0f);
        p.torso = -100;
        p.head = -100;
        p.uaF = 150; p.faF = 110;
        p.uaB = 35;  p.faB = 20;
        p.thF = 20; p.shF = 40;
        p.thB = 48; p.shB = 60;
        p.rot = f.facing * 300.0f * k;
        p.dx = -f.facing * 8.0f * k;
    } else if (f.state == 9) { // hit stun
        p.torso = -101;
        p.head = -94;
        p.uaF = 118; p.faF = 22;
        p.uaB = 55;  p.faB = -70;
        p.dx = -2;
        p.thF = 66; p.shF = 112;
        p.thB = 92; p.shB = 122;
    } else if (f.state == 5) { // guard
        p.uaF = 25; p.faF = -125;
        p.uaB = 65; p.faB = -100;
        p.torso = -92;
    } else { // idle breathing
        float b = sinf(f.t * 2.2f);
        p.torso += b * 0.8f;
        p.uaF -= b * 1.2f;
        p.uaB += b * 0.8f;
    }
    // Hold down to visibly crouch; low sweep/link commands still read the
    // raw down direction before this cosmetic pose offset.
    if (&f == &p1 && in.down && (f.state == 0 || f.state == 1)) {
        p.dy += 9;
        p.torso += 2;
        p.thF += 16;
        p.shF -= 10;
        p.thB += 10;
        p.shB -= 8;
    }
    return p;
}

static void drawFighter(const Fighter &f, const Pose &p, bool isDummy) {
    Skeleton s = makeSkeleton(p);
    float pelvisY = GROUND - f.airY - HIP_H + p.dy;
    Point hip = worldPoint(f, s.hip, pelvisY);
    Point neck = worldPoint(f, s.neck, pelvisY);
    Point head = worldPoint(f, s.head, pelvisY);
    Point shF = worldPoint(f, s.shF, pelvisY), shB = worldPoint(f, s.shB, pelvisY);
    Point elF = worldPoint(f, s.elF, pelvisY), elB = worldPoint(f, s.elB, pelvisY);
    Point haF = worldPoint(f, s.haF, pelvisY), haB = worldPoint(f, s.haB, pelvisY);
    Point hiF = worldPoint(f, s.hiF, pelvisY), hiB = worldPoint(f, s.hiB, pelvisY);
    Point knF = worldPoint(f, s.knF, pelvisY), knB = worldPoint(f, s.knB, pelvisY);
    Point ftF = worldPoint(f, s.ftF, pelvisY), ftB = worldPoint(f, s.ftB, pelvisY);

    // Heavy/ink speed streaks behind the fighter.
    if (f.state == 6 || f.state == 7 || f.state == 8 || f.state == 10 || f.state == 11 || f.state == 12 || f.state == 13 || f.state == 14 || f.state == 16 || f.state == 18 || f.state == 19 || f.state == 20 || f.state == 21 || f.state == 22 || f.state == 23) {
        int dir = -f.facing;
        for (int i = 0; i < 3; ++i) {
            int y = (int)neck.y - 6 + i * 13;
            u8g2_DrawLine(g, (int)hip.x + dir * (26 + i * 8), y,
                          (int)hip.x + dir * (44 + i * 12), y);
        }
    }

    // Retro attack smear behind the striking limb.
    if (f.state == 3 || f.state == 4 || f.state == 7 || f.state == 8 || f.state == 12) {
        Point tip = (f.state == 4) ? ftF : haF;
        for (int i = 1; i <= 3; ++i) {
            int y = (int)tip.y - 10 + i * 5;
            u8g2_DrawLine(g, (int)tip.x, y, (int)tip.x - f.facing * (10 + i * 7), y);
        }
    }

    // Back limbs: thinner solid tapered limbs, so depth reads without gray.
    taperedLimb(hip, knB, 8, 5, false);
    taperedLimb(knB, ftB, 5, 4, false);
    taperedLimb(shB, elB, 8, 5, false);
    taperedLimb(elB, haB, 5, 4, false);

    // Torso: wide chest, narrow waist.
    taperedLimb(neck, hip, 22, 12, false);
    taperedLimb(shF, shB, 7, 7, false);
    taperedLimb(hiF, hiB, 6, 6, false);

    // Front legs: thick thigh, tapered shin.
    taperedLimb(hiF, knF, 11, 8, false);
    taperedLimb(knF, ftF, 8, 5, false);

    // Front arms: shoulder to elbow, elbow to wrist.
    taperedLimb(shF, elF, 8, 6, false);
    taperedLimb(elF, haF, 6, 4, false);

    // Belt.
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawLine(g, (int)hip.x - 8, (int)hip.y - 1, (int)hip.x + 8, (int)hip.y - 1);
    u8g2_SetDrawColor(g, 1);

    // Joints with white gaps: shoulder, elbow, hip, knee.
    jointNode(shF, 4); jointNode(elF, 3);
    jointNode(hiF, 4); jointNode(knF, 3);
    // Back joints smaller and without halos.
    u8g2_DrawDisc(g, (int)elB.x, (int)elB.y, 2, U8G2_DRAW_ALL);
    u8g2_DrawDisc(g, (int)knB.x, (int)knB.y, 2, U8G2_DRAW_ALL);

    // Fists and boots.
    pixelFist(haF, f.facing);
    pixelFist(haB, f.facing);
    pixelBoot(ftF, f.facing);
    pixelBoot(ftB, f.facing);

    // Head: chunky octagonal helmet with a white visor slit.
    Point hd[8] = {
        {head.x - 7, head.y - 9}, {head.x + 7, head.y - 9},
        {head.x + 10, head.y - 3}, {head.x + 8, head.y + 5},
        {head.x + 4, head.y + 9}, {head.x - 4, head.y + 9},
        {head.x - 8, head.y + 5}, {head.x - 10, head.y - 3}
    };
    fillPoly(hd, 8);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawBox(g, (int)head.x - 5, (int)head.y - 3, 11, 3);
    u8g2_DrawPixel(g, (int)head.x + f.facing * 3, (int)head.y);
    u8g2_SetDrawColor(g, 1);

    // Dummy hit marker.
    if (isDummy && f.flash > 0) {
        int bx = (int)dummy.x;
        for (int i = 0; i < 8; ++i) {
            float a = i * 45.0f;
            Point d = pol(a, 24);
            u8g2_DrawLine(g, bx, GROUND - 120, bx + (int)d.x, GROUND - 120 + (int)d.y);
        }
    }
}

static void init_input() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << JOY_SW) | (1ULL << K1) | (1ULL << K2) | (1ULL << K3) | (1ULL << K4);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));

    adc_oneshot_unit_init_cfg_t init = {};
    init.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init, &g_adc));
    adc_oneshot_chan_cfg_t chan = {};
    chan.atten = ADC_ATTEN_DB_12;
    chan.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc, ADC_CHANNEL_0, &chan));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc, ADC_CHANNEL_1, &chan));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc, ADC_CHANNEL_3, &chan)); // GPIO4 battery divider
}

static Input readInput() {
    Input r;
    int adc0 = 2048, adc1 = 2048;
    adc_oneshot_read(g_adc, ADC_CHANNEL_0, &adc0);
    adc_oneshot_read(g_adc, ADC_CHANNEL_1, &adc1);

    // X1K4 wiring on this board has VRx/VRy reversed relative to the header
    // labels: ADC1_CH0 follows the physical vertical axis, ADC1_CH1 follows
    // the physical horizontal axis. Swap them in software so the controls
    // feel like a normal side-view game.
    r.rawVrx = adc1;   // physical horizontal
    r.rawVry = adc0;   // physical vertical
    if (adc1 < 1100) r.x = 1;
    else if (adc1 > 2900) r.x = -1;
    r.up = adc0 > 2900;
    r.down = adc0 < 1100;

    r.sw = !gpio_get_level(JOY_SW);
    r.k1 = !gpio_get_level(K1);
    r.k2 = !gpio_get_level(K2);
    r.k3 = !gpio_get_level(K3);
    r.k4 = !gpio_get_level(K4);
    return r;
}

static void resetGame() {
    p1 = Fighter{};
    p1.x = 140;
    dummy = Fighter{};
    dummy.x = 300;
    dummy.facing = -1;
    ko = false;
    winner = 0;
    aiTimer = 0.8f;
    aiInkCooldown = 2.0f;
    qcfStage = 0;
    qcfDownAt = -10.0f;
    lastTapDir = 0;
    lastTapAt = -10.0f;
    prevIn = Input{};
}

static void updateFighter(Fighter &f, float dt, bool isDummy) {
    f.t += dt;
    if (f.flash > 0) f.flash = fmaxf(0, f.flash - dt);

    // Gravity is independent of state. A mid-air hit used to change state
    // from jump to hurt, which left the character stranded in the air.
    if (f.state != 6 && (f.airY > 0.0f || f.vy > 0.0f)) {
        f.airY += f.vy * dt;
        f.vy -= 900.0f * dt;
        if (f.airY <= 0) {
            f.airY = 0;
            f.vy = 0;
            if (f.state == 2) {
                f.state = 0;
                f.t = 0;
            }
        }
    }

    switch (f.state) {
        case 3: // punch
            if (f.t > 0.42f) { f.state = 0; f.t = 0; }
            break;
        case 4: // kick
            if (f.t > 0.52f) { f.state = 0; f.t = 0; }
            break;
        case 5: // guard
            if (isDummy) {
                f.guardT -= dt;
                if (f.guardT <= 0) { f.state = 0; f.t = 0; }
            } else if (!f.guardHold) {
                f.state = 0; f.t = 0;
            }
            break;
        case 6: // ink cartwheel
            if (f.t > 0.95f) { f.state = 0; f.t = 0; }
            break;
        case 7: // heavy punch
            if (f.t > 0.62f) { f.state = 0; f.t = 0; }
            break;
        case 8: // heavy kick
            if (f.t > 0.72f) { f.state = 0; f.t = 0; }
            break;
        case 9: // hurt
            if (f.t > 0.36f) { f.state = 0; f.t = 0; }
            break;
        case 10: // low sweep
            if (f.t > 0.56f) { f.state = 0; f.t = 0; }
            break;
        case 11: // rising knee
            if (f.t > 0.66f) { f.state = 0; f.t = 0; }
            break;
        case 12: // dash
            if (f.t > 0.18f) { f.state = 0; f.t = 0; f.dashDir = 0; }
            break;
        case 13: // air kick lands
            if (f.t > 0.30f || f.airY <= 0.01f) { f.state = 0; f.t = 0; }
            break;
        case 14: // roll recovery
            if (f.t > 0.34f) { f.state = 0; f.t = 0; f.dashDir = 0; }
            break;
        case 15: // counter window
            if (f.t > 0.30f) { f.state = 0; f.t = 0; }
            break;
        case 16: // grab recovery
            if (f.t > 0.30f) { f.state = 0; f.t = 0; }
            break;
        case 17: // throw recovery
            if (f.t > 0.42f) { f.state = 0; f.t = 0; }
            break;
        case 18: // ink burst
            if (f.t > 0.58f) { f.state = 0; f.t = 0; }
            break;
        case 19: // shadow rush
            if (f.t > 0.56f) { f.state = 0; f.t = 0; f.dashDir = 0; }
            break;
        case 20: // ultimate
            if (f.t > 1.20f) { f.state = 0; f.t = 0; }
            break;
        case 21: // thrown victim
            if (f.t > 0.55f || (f.airY <= 0.01f && f.t > 0.25f)) { f.state = 0; f.t = 0; }
            break;
        case 22: // run
            if (in.x == 0 || p1.facing != (in.x > 0 ? 1 : -1)) { f.state = 0; f.t = 0; }
            break;
        case 23: // cross
            if (f.t > 0.40f) { f.state = 0; f.t = 0; }
            break;
        default:
            break;
    }
}

static void startAttack(Fighter &f, int state) {
    bool chain = (state == 23 && f.state == 3 && f.t > 0.10f && f.t < 0.36f) ||
                 (state == 8 && f.state == 23 && f.t > 0.12f && f.t < 0.38f);
    if (f.state == 0 || f.state == 1 || f.state == 22 || chain) {
        f.state = state;
        f.t = 0;
        f.hitDone = false;
        if (state == 11) { // rising knee leaves the ground
            f.vy = 300.0f;
            f.airY = 0.1f;
        }
    }
}

static void startAirKick(Fighter &f) {
    if (f.state == 2 && f.airY > 0.01f) {
        f.state = 13;
        f.t = 0;
        f.hitDone = false;
    }
}

static void startRoll(Fighter &f) {
    if ((f.state == 0 || f.state == 1) && f.airY <= 0.01f) {
        f.state = 14;
        f.t = 0;
        f.dashDir = -f.facing;
        f.hitDone = false;
    }
}

static void startCounter(Fighter &f) {
    if (f.state == 5) { f.state = 15; f.t = 0; f.hitDone = false; }
}

static void startGrab(Fighter &f) {
    if ((f.state == 0 || f.state == 1) && f.airY <= 0.01f) {
        f.state = 16; f.t = 0; f.hitDone = false;
    }
}

static bool startBurst(Fighter &f) {
    if (f.gas < 2) { f.flash = 0.12f; return false; }
    f.gas -= 2; f.state = 18; f.t = 0; f.hitDone = false;
    f.airY = 0; f.vy = 0;
    return true;
}

static bool startRush(Fighter &f) {
    if (f.gas < 1) { f.flash = 0.12f; return false; }
    f.gas -= 1; f.state = 19; f.t = 0; f.hitDone = false; f.hitCount = 0; f.nextHit = 0.12f;
    f.dashDir = f.facing;
    return true;
}

static bool startUltimate(Fighter &f) {
    if (f.gas < 3) { f.flash = 0.12f; return false; }
    f.gas -= 3; f.state = 20; f.t = 0; f.hitDone = false;
    f.hitCount = 0; f.nextHit = 0.28f;
    f.airY = 0; f.vy = 0;
    return true;
}

static bool startInk(Fighter &f) {
    if (f.gas <= 0) {
        f.flash = 0.12f;
        return false;
    }
    f.gas--;
    f.state = 6;
    f.t = 0;
    f.hitDone = false;
    f.airY = 0;
    f.vy = 0;
    return true;
}

static void playSfx(int kind);

static void handleInput(float dt) {
    // Reset chord: yellow (B) + green (D) held together for 0.25 s.
    // The joystick SW is intentionally ignored to prevent accidental resets.
    static float resetChord = 0.0f;
    if (in.k4 && in.k2) {
        resetChord += dt;
        if (resetChord > 0.25f) {
            resetChord = 0.0f;
            resetGame();
            prevIn = in;
            return;
        }
    } else {
        resetChord = 0.0f;
    }

    if (ko || p1.state == 9) {
        prevIn = in;
        return;
    }

    // Facing-relative input. Update facing before decoding the motion so that
    // after crossing the opponent, QCF mirrors immediately and an old motion
    // buffer cannot leak across the side switch.
    const int opponentDir = (dummy.x >= p1.x) ? 1 : -1;
    if ((p1.state == 0 || p1.state == 1 || p1.state == 5) &&
        p1.facing != opponentDir) {
        p1.facing = opponentDir;
        qcfStage = 0;
        qcfDownAt = -10.0f;
    }
    // Double-tap forward/back = quick dash / backdash.
    if (in.x != 0 && prevIn.x != in.x) {
        if (in.x == lastTapDir && g_time - lastTapAt < 0.32f &&
            (p1.state == 0 || p1.state == 1) && p1.airY <= 0.01f) {
            p1.state = 12;
            p1.t = 0;
            p1.dashDir = in.x;
            lastTapDir = 0;
            lastTapAt = -10.0f;
            playSfx(8);
        } else {
            lastTapDir = in.x;
            lastTapAt = g_time;
        }
    }
    const int fwd = p1.facing;
    const bool forward = (in.x == fwd);
    const bool back = (in.x == -fwd);

    // --- KOF-style quarter-circle-forward buffer: ↓, ↘, → ---
    if (in.down && !forward && !back) {
        qcfStage = 1;
        qcfDownAt = g_time;
    } else if (qcfStage == 1 && in.down && forward) {
        qcfStage = 2;
    } else if (qcfStage >= 1 && !in.down && forward && g_time - qcfDownAt < 0.65f) {
        qcfStage = 3;
    } else if (!in.down && !forward && !back && g_time - qcfDownAt > 0.80f) {
        qcfStage = 0;
    }
    if (qcfStage != 0 && g_time - qcfDownAt > 0.90f) qcfStage = 0;
    const bool qcfReady = (qcfStage >= 2 && g_time - qcfDownAt < 0.65f);

    // --- KOF guard: hold back while an AI attack is active in range ---
    const bool aiAttacking = (dummy.state == 3 || dummy.state == 4 ||
                              dummy.state == 6 || dummy.state == 7 ||
                              dummy.state == 8 || dummy.state == 10 ||
                              dummy.state == 11 || dummy.state == 18 ||
                              dummy.state == 19 || dummy.state == 20);
    p1.guardHold = back && aiAttacking &&
                   fabsf(dummy.x - p1.x) < 125.0f && p1.airY <= 0.01f;
    if (p1.guardHold && (p1.state == 0 || p1.state == 1)) {
        p1.state = 5;
        p1.t = 0;
    }
    if (p1.state == 5 && !p1.guardHold) {
        p1.state = 0;
        p1.t = 0;
    }

    // --- A / B / C / D ---
    const bool aEdge = in.k3 && !prevIn.k3; // red
    const bool bEdge = in.k4 && !prevIn.k4; // yellow
    const bool cEdge = in.k1 && !prevIn.k1; // blue
    const bool dEdge = in.k2 && !prevIn.k2; // green

    float distNow = fabsf(dummy.x - p1.x);
    if (p1.state == 5 && aEdge) {
        startCounter(p1);                                  // guard + A: counter
    } else if (!p1.guardHold) {
        if (aEdge) {
            if (p1.state == 3 && p1.t > 0.10f && p1.t < 0.36f) startAttack(p1, 23);
            else if (p1.state == 23 && p1.t > 0.12f && p1.t < 0.38f) startAttack(p1, 8);
            else startAttack(p1, 3);                  // A: jab

        }
        if (bEdge) {
            if (in.down && back) startRoll(p1);           // ↓+back+B: roll
            else if (p1.state == 2 && p1.airY > 0.01f) startAirKick(p1);
            else if (in.down) startAttack(p1, 10);
            else startAttack(p1, 4);
        }
        if (cEdge || dEdge) {
            const bool both = in.k1 && in.k2;              // C+D together
            if (both) {
                startUltimate(p1);
            } else if (p1.state == 12 && p1.dashDir == fwd) {
                if (!startRush(p1)) p1.flash = 0.12f;       // dash + C/D
            } else if (in.down && back) {
                if (!startBurst(p1)) p1.flash = 0.12f;     // ↓+back+C/D
            } else if (cEdge && distNow < 62) {
                startGrab(p1);                             // close C: grab
            } else if (cEdge) {
                if (qcfReady) {
                    if (!startInk(p1)) p1.flash = 0.12f;
                    qcfStage = 0;
                } else if (forward) startAttack(p1, 11);
                else startAttack(p1, 7);
            } else {
                if (qcfReady) {
                    if (!startInk(p1)) p1.flash = 0.12f;
                    qcfStage = 0;
                } else if (forward) startAttack(p1, 11);
                else if (in.down) startAttack(p1, 10);
                else startAttack(p1, 8);
            }
        }
    }

    // --- movement: forward/back walk; back becomes guard only on incoming attack ---
    const bool canMove = (p1.state == 0 || p1.state == 1 || p1.state == 22) && p1.airY <= 0.01f;
    if (canMove && !p1.guardHold) {
        if (in.x != 0) {
            if (p1.state == 1) p1.moveHold += dt; else p1.moveHold = 0;
            if (p1.state == 1 && p1.moveHold > 0.28f && in.x == p1.facing) {
                p1.state = 22; p1.t = 0;
            }
            float spd = (p1.state == 22) ? 135.0f : 78.0f;
            p1.x = clampf(p1.x + in.x * spd * dt, 45, 355);
            if (p1.state != 22) p1.state = 1;
            p1.walkPhase += dt * (p1.state == 22 ? 16.0f : 9.0f);
        } else if (p1.state == 1 || p1.state == 22) {
            p1.state = 0; p1.t = 0; p1.moveHold = 0;
        }
    }

    if (in.up && !prevIn.up && p1.airY <= 0.01f &&
        (p1.state == 0 || p1.state == 1)) {
        p1.state = 2;
        p1.t = 0;
        p1.vy = 290.0f;
        p1.airY = 0.1f;
        playSfx(6);
    }

    // Air control: keep moving forward/back while airborne.
    if (p1.state == 2 && p1.airY > 0.01f && in.x != 0) {
        float airSpeed = (in.x == p1.facing) ? 62.0f : 52.0f;
        p1.x = clampf(p1.x + in.x * airSpeed * dt, 45, 355);
    }

    if (p1.state == 12) {
        p1.x = clampf(p1.x + p1.dashDir * 430.0f * dt, 45, 355);
    }
    if (p1.state == 14) {
        p1.x = clampf(p1.x + p1.dashDir * 260.0f * dt, 45, 355);
    }

    // Auto-face when free to turn; attacks/ink/hurt lock facing.
    if (p1.state == 0 || p1.state == 1 || p1.state == 22 || p1.state == 5) {
        p1.facing = dummy.x >= p1.x ? 1 : -1;
    }

    prevIn = in;
}

static void addGas(Fighter &f, float amount) {
    if (f.gas >= MAX_GAS) {
        f.gasCharge = 0;
        return;
    }
    f.gasCharge += amount;
    while (f.gasCharge >= GAS_PIP_REQUIREMENT && f.gas < MAX_GAS) {
        f.gas++;
        f.gasCharge -= GAS_PIP_REQUIREMENT;
    }
    if (f.gas >= MAX_GAS) f.gasCharge = 0;
}

static void spawnHitFx(float x, float y, int kind) {
    for (int i = 0; i < 8; ++i) {
        if (g_fx[i].t <= 0.0f) {
            g_fx[i].x = x;
            g_fx[i].y = y;
            g_fx[i].t = 0.001f;
            g_fx[i].kind = kind;
            return;
        }
    }
}

static void updateHitFx(float dt) {
    for (int i = 0; i < 8; ++i) {
        if (g_fx[i].t > 0.0f) {
            g_fx[i].t += dt;
            if (g_fx[i].t > 0.24f) g_fx[i].t = 0.0f;
        }
    }
}

static void drawHitFx() {
    for (int i = 0; i < 8; ++i) {
        if (g_fx[i].t <= 0.0f) continue;
        float k = g_fx[i].t / 0.24f;
        int n = g_fx[i].kind == 3 ? 12 : (g_fx[i].kind == 2 ? 8 : 5);
        if (g_fx[i].kind == 5) n = 4;
        int r = 4 + (int)(k * 13.0f);
        for (int j = 0; j < n; ++j) {
            float a = (float)j * 2.0f * PI_F / (float)n + k * 0.7f;
            int x2 = (int)(g_fx[i].x + cosf(a) * r);
            int y2 = (int)(g_fx[i].y + sinf(a) * r);
            u8g2_DrawLine(g, (int)g_fx[i].x, (int)g_fx[i].y, x2, y2);
        }
        if (k < 0.55f) {
            u8g2_DrawCircle(g, (int)g_fx[i].x, (int)g_fx[i].y,
                            2 + (int)(k * 6.0f), U8G2_DRAW_ALL);
        }
        if (g_fx[i].kind >= 2) {
            int ring = 5 + (int)(k * 24.0f);
            u8g2_DrawCircle(g, (int)g_fx[i].x, (int)g_fx[i].y, ring, U8G2_DRAW_ALL);
        }
    }
}

static void buzzer_init() {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.timer_num = LEDC_TIMER_0;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.freq_hz = 1000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);
    ledc_channel_config_t c = {};
    c.gpio_num = GPIO_NUM_0;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    c.hpoint = 0;
    ledc_channel_config(&c);
}

static void buzzer_tone(int freq, int ms) {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    g_beep_end = esp_timer_get_time() + (int64_t)ms * 1000;
}

static void buzzer_update() {
    if (g_beep_end && esp_timer_get_time() >= g_beep_end) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        g_beep_end = 0;
    }
}

static void audio_task(void *) {
    int kind = 0;
    for (;;) {
        if (xQueueReceive(g_sfx_q, &kind, portMAX_DELAY) != pdTRUE) continue;

        int ms = 50;
        float f0 = 400.0f, f1 = 300.0f, noise = 0.0f, amp = 0.45f;
        switch (kind) {
            case 1: ms = 35;  f0 = 900; f1 = 700; noise = 0.10f; amp = 0.45f; break;
            case 2: ms = 70;  f0 = 230; f1 = 90;  noise = 0.45f; amp = 0.65f; break;
            case 3: ms = 120; f0 = 120; f1 = 50;  noise = 0.55f; amp = 0.75f; break;
            case 4: ms = 60;  f0 = 360; f1 = 180; noise = 0.20f; amp = 0.55f; break;
            case 5: ms = 20;  f0 = 1400; f1 = 1200; noise = 0.05f; amp = 0.30f; break;
            case 6: ms = 60;  f0 = 500; f1 = 800; noise = 0.05f; amp = 0.30f; break;
            case 7: ms = 260; f0 = 100; f1 = 60;  noise = 0.25f; amp = 0.65f; break;
            case 8: ms = 50;  f0 = 300; f1 = 500; noise = 0.10f; amp = 0.30f; break;
            default: continue;
        }

        const int sr = 16000;
        const int total = sr * ms / 1000;
        int done = 0;
        static int16_t buf[512 * 2];
        while (done < total) {
            int n = total - done;
            if (n > 512) n = 512;
            for (int i = 0; i < n; ++i) {
                float u = (float)(done + i) / (float)total;
                float t = (float)(done + i) / (float)sr;
                float freq = f0 + (f1 - f0) * u;
                float env = (1.0f - u) * (1.0f - u);
                float s = sinf(2.0f * PI_F * freq * t) * amp * env;
                s += ((int)(esp_random() % 2001) - 1000) / 1000.0f * noise * env;
                if (s > 0.95f) s = 0.95f;
                if (s < -0.95f) s = -0.95f;
                int16_t v = (int16_t)(s * 32767.0f);
                buf[i * 2] = v;
                buf[i * 2 + 1] = v;
            }
            if (g_codec) g_codec->CodecPort_PlayWrite(buf, n * 4);
            done += n;
        }
    }
}

static void audio_init() {
    // Match the official example: allocate the large PSRAM audio buffer first.
    if (!g_audio_buf) {
        g_audio_buf = (uint8_t *)heap_caps_malloc(288000, MALLOC_CAP_SPIRAM);
    }
    g_i2c_bus = &g_i2c_static;
    if (!g_codec) {
        g_codec = new CodecPort(*g_i2c_bus, "S3_RLCD_4_2");
    }
    g_i2c_ret = g_i2c_bus ? 0 : -1;
    g_es8311 = g_es7210 = true;
    g_codec_ret = g_codec ? 0 : -1;
    g_spk_ok = g_rec_ok = (g_codec != nullptr);
    codec_debug_stage = 8;
    codec_debug_err = 0;
}

static void audio_open_late() {
    if (!g_codec) return;
    g_codec->CodecPort_SetInfo("es8311 & es7210", 1, 16000, 2, 16);
    g_codec->CodecPort_SetSpeakerVol(100);
    g_codec->CodecPort_SetMicGain(35);
    g_rec_open_ret = 0;
    g_sfx_q = xQueueCreate(8, sizeof(int));
    if (!g_sfx_q) return;
    xTaskCreate(audio_task, "audio", 4096, NULL, 3, NULL);
    g_audio_ready = true;
}

static void playSfx(int kind) {
    if (g_audio_ready && g_sfx_q) {
        xQueueSend(g_sfx_q, &kind, 0);
        return;
    }
    switch (kind) {
        case 1: buzzer_tone(880, 35); break;  // light hit
        case 2: buzzer_tone(220, 65); break;  // heavy hit
        case 3: buzzer_tone(110, 95); break;  // ink
        case 4: buzzer_tone(260, 60); break;  // knee
        case 5: buzzer_tone(1400, 18); break; // block
        case 6: buzzer_tone(500, 45); break;  // jump
        case 7: buzzer_tone(90, 260); break;  // KO
        case 8: buzzer_tone(320, 50); break;  // dash
        default: break;
    }
}

// Generic hit resolution, used by both the player and the AI.
static void tryHit(Fighter &att, Fighter &def) {
    if (ko || att.hitDone || def.state == 14 || def.state == 21) return;
    if (def.state == 9 && att.state != 19 && att.state != 20) return;
    if (def.state == 12 && def.dashDir == -def.facing && def.t < 0.16f) return; // backdash i-frames

    float active = 0, reach = 0, damage = 0;
    switch (att.state) {
        case 3: active = 0.17f; reach = 70; damage = 14; break; // light punch
        case 4: active = 0.22f; reach = 84; damage = 18; break; // light kick
        case 7: active = 0.24f; reach = 88; damage = 28; break; // heavy punch
        case 8: active = 0.28f; reach = 96; damage = 32; break; // heavy kick
        case 10: active = 0.20f; reach = 98; damage = 20; break; // low sweep
        case 11: active = 0.24f; reach = 82; damage = 26; break; // rising knee
        case 6: active = 0.22f; reach = 165; damage = 38; break; // ink cartwheel
        case 13: active = 0.10f; reach = 88; damage = 22; break; // air kick
        case 16: active = 0.10f; reach = 62; damage = 24; break; // grab
        case 18: active = 0.16f; reach = 110; damage = 30; break; // ink burst
        case 19: active = 0.12f; reach = 92; damage = 16; break; // shadow rush
        case 20: active = 0.28f; reach = 170; damage = 25; break; // ultimate
        default: return;
    }
    if (def.state == 15 && att.t < 0.26f && fabsf(def.x - att.x) < 110.0f) {
        att.state = 9; att.t = 0; att.hp = att.hp > 18 ? att.hp - 18 : 0;
        att.flash = 0.20f;
        def.state = 7; def.t = 0; def.hitDone = false;
        g_hitStop = 0.06f; g_shake = 0.16f;
        spawnHitFx((att.x + def.x) * 0.5f, GROUND - 82.0f, 2);
        playSfx(2);
        return;
    }
    if (att.state == 19 || att.state == 20) {
        if (att.t < att.nextHit || att.hitCount >= 3) return;
    } else if (att.t < active) return;
    float dx = def.x - att.x;
    if (fabsf(dx) > reach) return;
    if (dx * att.facing < -10) return;

    bool grabMove = (att.state == 16);
    att.hitDone = true;
    if (att.state == 19 || att.state == 20) {
        att.hitCount++;
        att.nextHit += (att.state == 20 ? 0.18f : 0.13f);
        if (att.hitCount < 3) att.hitDone = false;
    } else if (att.state == 16) {
        att.state = 17; att.t = 0;
    }
    int dir = dx >= 0 ? 1 : -1;
    bool blocked = (def.state == 5) && !grabMove;

    if (blocked) {
        def.hp = def.hp > damage / 2 ? def.hp - damage / 2 : 0;
        def.flash = 0.10f;
        def.x = clampf(def.x + dir * 4, 45, 355);
        addGas(def, damage * 0.55f * GAS_GAIN_SCALE);
        addGas(att, damage * 0.25f * GAS_GAIN_SCALE);
        g_hitStop = 0.03f;
        g_shake = 0.05f;
        playSfx(5);
        spawnHitFx((att.x + def.x) * 0.5f, GROUND - 88.0f, 5);
        return;
    }

    addGas(att, damage * 1.2f * GAS_GAIN_SCALE);
    addGas(def, damage * 0.8f * GAS_GAIN_SCALE);
    g_hitStop = (att.state == 6 || att.state == 11 || att.state == 20) ? 0.09f : 0.05f;
    g_shake = (att.state == 6 || att.state == 20) ? 0.24f : (att.state == 11 || att.state == 18) ? 0.16f : 0.12f;
    int fxKind = (att.state == 6 || att.state == 20) ? 3 : (att.state == 11 || att.state == 18) ? 4 : ((att.state == 7 || att.state == 8 || att.state == 10 || att.state == 16 || att.state == 19) ? 2 : 1);
    playSfx(fxKind);
    spawnHitFx((att.x + def.x) * 0.5f, GROUND - 82.0f, fxKind);
    if (att.state == 20) snprintf(g_impact_text, sizeof(g_impact_text), "INK DRAGON");
    else if (att.state == 19) snprintf(g_impact_text, sizeof(g_impact_text), "SLASH!");
    else if (att.state == 18) snprintf(g_impact_text, sizeof(g_impact_text), "BURST!");
    else if (att.state == 6) snprintf(g_impact_text, sizeof(g_impact_text), "KA-POW!");
    else snprintf(g_impact_text, sizeof(g_impact_text), "HIT!");
    g_impact_t = 0.28f;
    def.hp = def.hp > damage ? def.hp - damage : 0;
    def.flash = 0.20f;
    def.state = 9;
    def.t = 0;
    def.x = clampf(def.x + dir * 9, 45, 355);
    if (grabMove) {
        def.state = 21;
        def.t = 0;
        def.vy = 250.0f;
        def.airY = 1.0f;
    }
    if (att.state == 11 || att.state == 20 || att.state == 18) {
        def.vy = att.state == 20 ? 380.0f : 180.0f;
        def.airY = 1.0f;
    }
    if (def.hp <= 0) {
        ko = true;
        winner = (&att == &p1) ? 1 : 2;
        playSfx(7);
    }
}

// Very small distance-based AI for the second fighter.
static void updateAI(float dt) {
    if (ko || dummy.state == 9) return;
    if (!(dummy.state == 3 || dummy.state == 4 || dummy.state == 6 ||
          dummy.state == 7 || dummy.state == 8)) {
        dummy.facing = p1.x >= dummy.x ? 1 : -1;
    }
    float dx = p1.x - dummy.x;
    float dist = fabsf(dx);
    int dir = dx >= 0 ? 1 : -1;

    if (dummy.state == 5) return; // guardT is decremented by updateFighter

    if (dummy.state == 3 || dummy.state == 4 ||
        dummy.state == 6 || dummy.state == 7 || dummy.state == 8 ||
        dummy.state == 10 || dummy.state == 11) {
        return; // attack recovery
    }

    // Shuffle toward the player while the decision timer is running.
    if (dummy.state == 0 || dummy.state == 1) {
        if (dist > 82) {
            dummy.x = clampf(dummy.x + dir * 42.0f * dt, 55, 350);
            dummy.state = 1;
            dummy.walkPhase += dt * 8.0f;
        } else if (dist < 38) {
            dummy.x = clampf(dummy.x - dir * 34.0f * dt, 55, 350);
            dummy.state = 1;
            dummy.walkPhase += dt * 8.0f;
        } else if (dummy.state == 1) {
            dummy.state = 0;
        }
    }

    aiInkCooldown -= dt;
    aiTimer -= dt;
    if (aiTimer > 0) return;

    int r = (int)(esp_random() % 100);
    bool p1Attacking = (p1.state == 3 || p1.state == 4 ||
                        p1.state == 6 || p1.state == 7 ||
                        p1.state == 8 || p1.state == 10 || p1.state == 11 ||
                        p1.state == 19 || p1.state == 20);

    // React to an incoming attack with a short guard.
    if (p1Attacking && dist < 95 && r < 35) {
        dummy.state = 5;
        dummy.guardT = 0.34f;
        dummy.t = 0;
        aiTimer = 0.55f;
        return;
    }

    // Rare AI ink cartwheel at close/mid range.
    if (dist < 125 && aiInkCooldown <= 0 && dummy.gas > 0 && r < 18) {
        startInk(dummy);
        dummy.t = 0;
        dummy.hitDone = false;
        dummy.airY = 0; dummy.vy = 0;
        aiInkCooldown = 2.8f;
        aiTimer = 1.4f;
        return;
    }

    if (dist > 105) {
        aiTimer = 0.25f;
        return;
    }

    if (dist > 72) {
        if (r < 20) startAttack(dummy, 3);
        else if (r < 36) startAttack(dummy, 4);
        else if (r < 48) startAttack(dummy, 7);
        else if (r < 58) startAttack(dummy, 8);
        else if (r < 66) startAttack(dummy, 10);
        else if (r < 74) startAttack(dummy, 11);
        else if (r < 84) { dummy.state = 5; dummy.guardT = 0.30f; dummy.t = 0; }
        // else keep walking
    } else {
        if (r < 18) startAttack(dummy, 3);
        else if (r < 34) startAttack(dummy, 4);
        else if (r < 46) startAttack(dummy, 7);
        else if (r < 56) startAttack(dummy, 8);
        else if (r < 66) startAttack(dummy, 10);
        else if (r < 76) startAttack(dummy, 11);
        else if (r < 87) { dummy.state = 5; dummy.guardT = 0.28f; dummy.t = 0; }
        else {
            dummy.x = clampf(dummy.x - dir * 30.0f * dt, 55, 350);
            dummy.state = 1;
            dummy.walkPhase += dt * 8.0f;
        }
    }
    aiTimer = 0.55f + (esp_random() % 40) / 100.0f;
}

static void drawInkSprite(const Fighter &f) {
    if (f.state != 6 || f.t < 0.40f) return;
    int idx = (int)((f.t - 0.40f) / 0.18f);
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    int sy = GROUND - INK_EFFECT_ANCHOR_Y + g_shakeY;
    u8g2_SetBitmapMode(g, 1);
    if (f.facing > 0) {
        int sx = (int)(f.x - INK_EFFECT_ANCHOR_X) + g_shakeX;
        u8g2_DrawBitmap(g, sx, sy, INK_EFFECT_W_BYTES, INK_EFFECT_H, INK_EFFECT_FRAMES[idx]);
    } else {
        int sx = (int)(f.x - INK_EFFECT_MIRROR_ANCHOR_X) + g_shakeX;
        u8g2_DrawBitmap(g, sx, sy, INK_EFFECT_W_BYTES, INK_EFFECT_H, INK_EFFECT_FRAMES_MIRROR[idx]);
    }
}

static void rtc_write_epoch(time_t now);
static void time_save_nvs(time_t now);

static void updateClock() {
    time_t now = time(nullptr);
    if (now < 1700000000) {
        strcpy(g_clock, "--:--");
        return;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(g_clock, sizeof(g_clock), "%H:%M", &tm);
    if (now - g_last_time_save >= 300) {
        rtc_write_epoch(now);
        time_save_nvs(now);
        g_last_time_save = now;
    }
}

static void updateStatus(float dt) {
    g_status_t += dt;
    if (g_status_t < 1.0f) return;
    g_status_t = 0.0f;

    updateClock();

    int raw = 0, sum = 0, n = 0;
    for (int i = 0; i < 8; ++i) {
        if (adc_oneshot_read(g_adc, ADC_CHANNEL_3, &raw) == ESP_OK) {
            sum += raw;
            ++n;
        }
    }
    if (n > 0) {
        float adc_mv = (float)(sum / n) * 3300.0f / 4095.0f;
        g_batt_mv = (int)(adc_mv * 3.0f); // board divider is 3:1
    }
    if (g_batt_mv < 2500) {
        g_batt_pct = 0;
    } else {
        int pct = (g_batt_mv - 3000) * 100 / 1120;
        g_batt_pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    }
    g_charging = usb_serial_jtag_is_connected();

    if (g_wifi_sta) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            g_rssi = ap.rssi;
            strncpy(g_ssid, (const char *)ap.ssid, sizeof(g_ssid) - 1);
            g_ssid[sizeof(g_ssid) - 1] = 0;
        }
    } else {
        g_ssid[0] = 0;
    }
}

static void drawStatus() {
    g_dark_ui = false;
    g_no_invert = false;
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 4, 13, g_clock);

    // Wi-Fi bars, 4 steps from RSSI.
    int bars = 0;
    if (g_wifi_sta) {
        bars = (g_rssi > -55) ? 4 : (g_rssi > -65) ? 3 : (g_rssi > -75) ? 2 : 1;
    }
    for (int i = 0; i < 4; ++i) {
        int h = 4 + i * 3;
        int x = 42 + i * 5;
        if (i < bars) {
            u8g2_DrawBox(g, x, 13 - h, 3, h);
        } else {
            u8g2_DrawFrame(g, x, 13 - h, 3, h);
        }
    }
    if (!g_wifi_sta) {
        u8g2_DrawLine(g, 42, 2, 58, 12);
        u8g2_DrawLine(g, 58, 2, 42, 12);
    }

    // SSID/IP/version deliberately live on the System Info page, not the status bar.


    // Battery percent + 1.5x icon.
    char b[8];
    snprintf(b, sizeof(b), "%d%%", g_batt_pct);
    int bw = u8g2_GetStrWidth(g, b);
    u8g2_DrawStr(g, 326 - bw, 13, b);
    u8g2_DrawFrame(g, 340, 1, 42, 14);
    u8g2_DrawBox(g, 382, 5, 4, 4);
    int fill = 40 * g_batt_pct / 100;
    if (fill > 0) u8g2_DrawBox(g, 341, 2, fill, 12);
    if (g_charging) {
        u8g2_DrawLine(g, 337, 1, 331, 7);
        u8g2_DrawLine(g, 331, 7, 336, 7);
        u8g2_DrawLine(g, 336, 7, 330, 13);
    }
}

static void drawSpecialFx(const Fighter &f) {
    if (f.state == 18) { // ink burst: radial ink ring
        float k = clampf(f.t / 0.58f, 0.0f, 1.0f);
        int cx = (int)f.x + g_shakeX, cy = GROUND - 66 + g_shakeY;
        int r = 18 + (int)(48 * k);
        for (int i = 0; i < 12; ++i) {
            float a = i * 6.28318f / 12.0f + f.t * 2.0f;
            int x0 = cx + (int)(cosf(a) * 14);
            int y0 = cy + (int)(sinf(a) * 14);
            int x1 = cx + (int)(cosf(a) * r);
            int y1 = cy + (int)(sinf(a) * r);
            u8g2_DrawLine(g, x0, y0, x1, y1);
            if (i & 1) u8g2_DrawDisc(g, x1, y1, 2, U8G2_DRAW_ALL);
        }
        u8g2_DrawCircle(g, cx, cy, r / 2, U8G2_DRAW_ALL);
    } else if (f.state == 19) { // shadow rush: three slash trails
        int cx = (int)f.x + g_shakeX, cy = GROUND - 70 + g_shakeY;
        for (int i = 0; i < 3; ++i) {
            if (f.t < 0.12f + i * 0.13f) continue;
            int x0 = cx - 20 + i * 22, y0 = cy - 36 + i * 14;
            int x1 = cx + 70 + i * 6, y1 = cy + 18 - i * 10;
            u8g2_DrawLine(g, x0, y0, x1, y1);
            u8g2_DrawLine(g, x0, y0 + 3, x1, y1 + 3);
        }
    } else if (f.state == 20) { // ink dragon ultimate
        int cx = (int)f.x + g_shakeX, cy = GROUND - 78 + g_shakeY;
        float k = clampf(f.t / 1.2f, 0.0f, 1.0f);
        if (f.t > 0.18f) {
            int r = 22 + (int)(70 * k);
            u8g2_DrawCircle(g, cx, cy, r, U8G2_DRAW_ALL);
            u8g2_DrawCircle(g, cx, cy, r + 5, U8G2_DRAW_ALL);
            for (int i = 0; i < 16; ++i) {
                float a = i * 6.28318f / 16.0f + f.t;
                int x0 = cx + (int)(cosf(a) * (r + 6));
                int y0 = cy + (int)(sinf(a) * (r + 6));
                int x1 = cx + (int)(cosf(a) * (r + 28));
                int y1 = cy + (int)(sinf(a) * (r + 28));
                u8g2_DrawLine(g, x0, y0, x1, y1);
                u8g2_DrawDisc(g, x1, y1, 2, U8G2_DRAW_ALL);
            }
        }
        if (f.t > 0.30f) {
            for (int i = 0; i < 3; ++i) {
                int y = cy - 16 + i * 16;
                u8g2_DrawLine(g, cx - 58, y, cx + 80, y - 28);
                u8g2_DrawLine(g, cx - 50, y + 4, cx + 88, y - 24);
            }
        }
    }
}

static void drawImpactText() {
    if (g_impact_t <= 0.0f || !g_impact_text[0]) return;
    u8g2_SetFont(g, u8g2_font_10x20_tf);
    int w = u8g2_GetStrWidth(g, g_impact_text);
    u8g2_DrawStr(g, (SCREEN_W - w) / 2, 112, g_impact_text);
}

static void drawHud() {
    char txt[32];

    drawStatus();

    u8g2_SetFont(g, u8g2_font_9x15_tf);
    u8g2_DrawStr(g, 8, 46, "P1 EMBER");
    const char *ai = "AI AZURE";
    u8g2_DrawStr(g, 392 - u8g2_GetStrWidth(g, ai), 46, ai);

    snprintf(txt, sizeof(txt), "%d", p1.hp);
    u8g2_DrawStr(g, 188 - u8g2_GetStrWidth(g, txt), 46, txt);
    snprintf(txt, sizeof(txt), "%d", dummy.hp);
    u8g2_DrawStr(g, 212, 46, txt);

    // 1.5x-height HP bars.
    u8g2_DrawFrame(g, 8, 50, 180, 14);
    u8g2_DrawBox(g, 9, 51, (int)(178.0f * p1.hp / MAX_HP), 12);
    u8g2_DrawFrame(g, 212, 50, 180, 14);
    u8g2_DrawBox(g, 213, 51, (int)(178.0f * dummy.hp / MAX_HP), 12);
    u8g2_SetDrawColor(g, 0);
    for (int i = 1; i < 4; ++i) {
        u8g2_DrawVLine(g, 8 + i * 45, 51, 12);
        u8g2_DrawVLine(g, 212 + i * 45, 51, 12);
    }
    u8g2_SetDrawColor(g, 1);

    // 1.5x gas pips, with larger GAS labels.
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 78, "GAS");
    u8g2_DrawStr(g, 392 - u8g2_GetStrWidth(g, "GAS"), 78, "GAS");
    for (int i = 0; i < MAX_GAS; ++i) {
        int x = 34 + i * 22;
        u8g2_DrawFrame(g, x, 69, 18, 9);
        if (i < p1.gas) u8g2_DrawBox(g, x + 1, 70, 16, 7);
    }
    for (int i = 0; i < MAX_GAS; ++i) {
        int x = 300 + i * 22;
        u8g2_DrawFrame(g, x, 69, 18, 9);
        if (i < dummy.gas) u8g2_DrawBox(g, x + 1, 70, 16, 7);
    }

    // Bottom control hints are replaced by the global UI frame.
    if (!g_ui_frame) {
        u8g2_DrawStr(g, 8, 254, "A JAB>CROSS>HIGH   B KICK   AIR+B AIRKICK");
        u8g2_DrawStr(g, 8, 270, "GUARD+A CTR  D+BACK+B ROLL  CLOSE+C GRAB");
        u8g2_DrawStr(g, 8, 286, "QCF INK  D+BACK C BURST  DASH+C RUSH  C+D ULT");
    }
}

static void drawArena() {
    // Minimal white stage: only the ground line, no scenery.
    u8g2_DrawHLine(g, g_shakeX, GROUND + g_shakeY, SCREEN_W);
}

// ---------------------------------------------------------------------------
// Wi-Fi OTA: AP "STICKMAN-RLCD" / password "stickman123", page 192.168.4.1
// One USB flash installs the OTA partition table; later updates use the page.
// ---------------------------------------------------------------------------
static const char OTA_HTML[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Stickman RLCD OTA</title>
<style>body{font-family:system-ui;background:#f5f2e9;color:#111;padding:24px;max-width:680px;margin:auto}h1{font-size:24px}button{padding:12px 18px;font-size:16px}input{font-size:16px;margin:12px 0}pre{white-space:pre-wrap;background:#fff;padding:12px;border:1px solid #bbb}</style></head>
<body><h1>Stickman RLCD OTA</h1>
<p>AP: <b>STICKMAN-RLCD</b><br>Password: <b>stickman123</b><br>URL: <b>http://192.168.4.1</b></p>
<p id="v">Device version: ...</p>
<input id="f" type="file" accept=".bin"><br>
<button onclick="upd()">Upload firmware</button>
<pre id="s">Choose build/game_proto.bin</pre>
<script>
fetch('/version').then(r=>r.json()).then(j=>{document.getElementById('v').textContent='Device firmware: '+j.version+' '+j.name+'  build '+j.build;}).catch(()=>{});
async function upd(){const f=document.getElementById('f').files[0];const s=document.getElementById('s');if(!f){s.textContent='No file selected';return;}s.textContent='Uploading '+f.name+' ('+f.size+' bytes)...';try{const r=await fetch('/update',{method:'POST',body:f});s.textContent=await r.text();}catch(e){s.textContent='Upload failed: '+e;}}
</script></body></html>)HTML";

static esp_err_t ota_sd_list_get(httpd_req_t *req) {
    if (!sd_ensure()) {
        httpd_resp_sendstr(req, "SD not mounted");
        return ESP_FAIL;
    }
    DIR *d = opendir("/sdcard");
    if (!d) { httpd_resp_sendstr(req, "cannot list"); return ESP_FAIL; }
    char buf[2048];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != nullptr && n < (int)sizeof(buf) - 300) {
        if (de->d_name[0] == '.') continue;
        n += snprintf(buf + n, sizeof(buf) - n, "%s\n", de->d_name);
    }
    closedir(d);
    buf[n] = 0;
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t ota_note_post(httpd_req_t *req) {
    if (!sd_ensure()) { httpd_resp_sendstr(req, "SD not mounted"); return ESP_FAIL; }
    int len = req->content_len;
    if (len <= 0 || len > 2047) { httpd_resp_sendstr(req, "bad length"); return ESP_FAIL; }
    char *buf = (char *)malloc(len + 1);
    if (!buf) { httpd_resp_sendstr(req, "no memory"); return ESP_FAIL; }
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r <= 0) { free(buf); httpd_resp_sendstr(req, "recv fail"); return ESP_FAIL; }
        got += r;
    }
    buf[got] = 0;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { httpd_resp_sendstr(req, "bad json"); return ESP_FAIL; }
    cJSON *jt = cJSON_GetObjectItem(root, "text");
    cJSON *js = cJSON_GetObjectItem(root, "speaker");
    cJSON *jm = cJSON_GetObjectItem(root, "time");
    if (!cJSON_IsString(jt) || !jt->valuestring[0]) { cJSON_Delete(root); httpd_resp_sendstr(req, "no text"); return ESP_FAIL; }
    const char *spk = cJSON_IsString(js) ? js->valuestring : "后台";
    const char *tm = cJSON_IsString(jm) ? jm->valuestring : g_clock;
    FILE *f = fopen("/sdcard/voice_notes.txt", "a");
    if (f) {
        fprintf(f, "[%s] %s\n转写：%s\n\n", tm, spk, jt->valuestring);
        fclose(f);
    }
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static void files_enter();
static bool video_open(const char *path);
static void survey_start();
static void survey_save();
static void survey_build_report();
static void music_enter();
static void music_stop();
static void player_start_at(uint32_t start_ms);
static void wifi_enter();
static void notes_enter();
static esp_err_t ota_menu_get(httpd_req_t *req) {
    char q[64] = {0};
    char v[16] = {0};
    int sel = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "sel", v, sizeof(v)) == ESP_OK) {
        sel = atoi(v);
    }
    if (sel < 0 || sel > 5) sel = 0;
    g_menu_sel = sel;
    g_mode = MODE_MENU;
    char e[8] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "enter", e, sizeof(e)) == ESP_OK &&
        strcmp(e, "1") == 0) {
        if (sel == 0) {
            g_word_cat_sel = 0;
            char lv[8] = {0};
            if (httpd_query_key_value(q, "level", lv, sizeof(lv)) == ESP_OK) {
                int l = atoi(lv);
                g_word_level = l < 0 ? 0 : (l > 3 ? 3 : l);
                g_word_idx = 0;
                char rv[8] = {0};
                g_word_reveal = !(httpd_query_key_value(q, "reveal", rv, sizeof(rv)) == ESP_OK && atoi(rv) == 0);
                g_mode = MODE_WORD;
            } else g_mode = MODE_WORD_MENU;
        }
        else if (sel == 1) g_mode = MODE_PHYSICS;
        else if (sel == 2) files_enter();
        else if (sel == 3) music_enter();
        else if (sel == 4) {
            g_mode = MODE_SURVEY_MENU;
            char st[8] = {0};
            if (httpd_query_key_value(q, "start", st, sizeof(st)) == ESP_OK && atoi(st)) survey_start();
        }
        else if (sel == 5) {
            char sub[8] = {0};
            int sv = 0;
            if (httpd_query_key_value(q, "sub", sub, sizeof(sub)) == ESP_OK) sv = atoi(sub);
            if (sv == 0) g_mode = MODE_SETTINGS;
            else if (sv == 1) g_mode = MODE_SETTINGS_CTRL;
            else if (sv == 2) wifi_enter();
            else if (sv == 3) { g_info_page = 0; g_mode = MODE_INFO; }
            else if (sv == 4) g_mode = MODE_HELP;
            else if (sv == 5) { notes_enter(); g_mode = MODE_NOTES; }
            else if (sv == 6) g_mode = MODE_FEEDBACK;
        }
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_feedback_post(httpd_req_t *req) {
    char buf[2048];
    int n = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (n <= 0) { httpd_resp_sendstr(req, "empty"); return ESP_OK; }
    buf[n] = 0;
    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_sendstr(req, "bad json"); return ESP_OK; }
    cJSON *stage = cJSON_GetObjectItem(root, "stage");
    cJSON *detail = cJSON_GetObjectItem(root, "detail");
    cJSON *text = cJSON_GetObjectItem(root, "text");
    cJSON *ds = cJSON_GetObjectItem(root, "ds");
    cJSON *sv = cJSON_GetObjectItem(root, "sv");
    cJSON *wh = cJSON_GetObjectItem(root, "wh");
    if (cJSON_IsString(stage)) snprintf(g_fb_stage, sizeof(g_fb_stage), "%s", stage->valuestring);
    if (cJSON_IsString(detail)) snprintf(g_fb_detail, sizeof(g_fb_detail), "%s", detail->valuestring);
    if (cJSON_IsString(text)) snprintf(g_fb_text, sizeof(g_fb_text), "%s", text->valuestring);
    if (cJSON_IsString(ds)) snprintf(g_fb_ds, sizeof(g_fb_ds), "%s", ds->valuestring);
    if (cJSON_IsString(sv)) snprintf(g_fb_sv, sizeof(g_fb_sv), "%s", sv->valuestring);
    if (cJSON_IsString(wh)) snprintf(g_fb_wh, sizeof(g_fb_wh), "%s", wh->valuestring);
    snprintf(g_fb_time, sizeof(g_fb_time), "%s", g_clock);
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static void wifi_scan_now();
static void files_enter();
static void survey_start();
static void survey_save();
static void survey_build_report();
static void music_enter();
static void music_stop();
static void files_preview();
static void wav_start_playback();
static void mp3_start_playback();
static void image_view_file();
static int utf8_len(unsigned char c);
static void goBack();
static void files_enter();
static void survey_start();
static void survey_save();
static void survey_build_report();
static void music_enter();
static void music_stop();
static esp_err_t ota_back_get(httpd_req_t *req) {
    goBack();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_upload_post(httpd_req_t *req) {
    char q[160] = {0}, name[80] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "name", name, sizeof(name)) != ESP_OK || !name[0]) {
        httpd_resp_sendstr(req, "name required"); return ESP_OK;
    }
    char decoded[80] = {0};
    for (int i = 0, j = 0; name[i] && j < (int)sizeof(decoded) - 1; ++i) {
        if (name[i] == '%' && name[i+1] && name[i+2]) {
            auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1)); };
            int a = hex(name[i+1]), b = hex(name[i+2]);
            if (a >= 0 && b >= 0) { decoded[j++] = (char)((a << 4) | b); i += 2; continue; }
        }
        decoded[j++] = name[i];
    }
    snprintf(name, sizeof(name), "%s", decoded);
    char path[300];
    snprintf(path, sizeof(path), "/sdcard/%s", name);
    if (strncmp(name, "tts/", 4) == 0) mkdir("/sdcard/tts", 0777);
    FILE *f = fopen(path, "wb");
    if (!f) { httpd_resp_sendstr(req, "open failed"); return ESP_OK; }
    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining);
        if (n <= 0) break;
        fwrite(buf, 1, n, f);
        remaining -= n;
    }
    fclose(f);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_survey_report_get(httpd_req_t *req) {
    char q[64] = {0}, a[40] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "q", a, sizeof(a)) == ESP_OK) {
        int v[5] = {3,3,3,3,3};
        sscanf(a, "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
        for (int i = 0; i < 5; ++i) g_survey_ans[i] = v[i] < 1 ? 1 : (v[i] > 5 ? 5 : v[i]);
    }
    survey_save();
    survey_build_report();
    g_mode = MODE_SURVEY_REPORT;
    httpd_resp_sendstr(req, g_survey_report);
    return ESP_OK;
}

static esp_err_t ota_music_get(httpd_req_t *req) {
    char q[64] = {0}, tr[8] = {0}, pl[8] = {0};
    music_enter();
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "track", tr, sizeof(tr)) == ESP_OK) {
            int t = atoi(tr);
            if (t >= 0 && t < g_music_count) g_music_sel = t;
        }
        if (httpd_query_key_value(q, "play", pl, sizeof(pl)) == ESP_OK && atoi(pl)) {
            player_start_at(0);
        }
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_info_get(httpd_req_t *req) {
    char q[64] = {0}, pg[8] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "page", pg, sizeof(pg)) == ESP_OK)
        g_info_page = atoi(pg) % 4;
    g_mode = MODE_INFO;
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_wordaudio_get(httpd_req_t *req) {
    char q[64] = {0}, lv[8] = {0}, ix[8] = {0};
    int level = 0, idx = 1;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "level", lv, sizeof(lv)) == ESP_OK) level = atoi(lv);
        if (httpd_query_key_value(q, "idx", ix, sizeof(ix)) == ESP_OK) idx = atoi(ix);
    }
    if (idx < 1 || idx > 100) idx = 1;
    snprintf(g_audio_path, sizeof(g_audio_path), "/sdcard/tts/%s_%03d.mp3", level ? "pet" : "ket", idx);
    FILE *f = fopen(g_audio_path, "rb");
    if (!f) { httpd_resp_sendstr(req, "missing"); return ESP_OK; }
    fclose(f);
    mp3_start_playback();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_files_get(httpd_req_t *req) {
    files_enter();
    char q[160] = {0}, name[40] = {0}, act[16] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "name", name, sizeof(name)) == ESP_OK) {
        for (int i = 0; i < g_file_count; ++i) {
            if (strcmp(g_file_names[i], name) == 0) { g_file_sel = i; files_preview(); break; }
        }
    }
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "play", act, sizeof(act)) == ESP_OK && atoi(act) &&
        g_file_view == 1) {
        if (g_file_is_wav) wav_start_playback();
        else if (g_file_is_mp3) mp3_start_playback();
    }
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "view", act, sizeof(act)) == ESP_OK && atoi(act) &&
        g_file_is_gif) {
        image_view_file();
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_wifi_get(httpd_req_t *req) {
    char q[96] = {0}, v[16] = {0}, s[33] = {0};
    g_wifi_view = 0;
    g_wifi_sel = 0;
    g_mode = MODE_WIFI;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "view", v, sizeof(v)) == ESP_OK && atoi(v) == 1) {
            if (httpd_query_key_value(q, "ssid", s, sizeof(s)) == ESP_OK)
                snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", s);
            g_wifi_pass[0] = 0;
            g_key_page = 0; g_key_sel = 0;
            g_wifi_view = 1;
        } else {
            wifi_scan_now();
        }
    } else {
        wifi_scan_now();
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_game_get(httpd_req_t *req) {
    resetGame();
    g_hitStop = 0; g_shake = 0; g_shakeX = g_shakeY = 0;
    char q[64] = {0}, ps[8] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "pose", ps, sizeof(ps)) == ESP_OK) {
        int st = atoi(ps);
        p1.state = st;
        p1.t = (st == 14 || st == 13) ? 0.12f : 0.20f;
        p1.x = 150; p1.hitDone = false;
        dummy.x = 238; dummy.state = 0; dummy.t = 0;
        g_debug_hold = true;
    } else g_debug_hold = false;
    g_mode = MODE_GAME;
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_physics_get(httpd_req_t *req) {
    char q[64] = {0};
    char v[16] = {0};
    int view = 1;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "view", v, sizeof(v)) == ESP_OK) {
        view = atoi(v);
    }
    if (view < 1 || view > 59) view = 1;
    char pg[8] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "page", pg, sizeof(pg)) == ESP_OK) {
        g_wuli_page = atoi(pg) % 3;
    } else {
        g_wuli_page = 0;
    }
    g_phys_cat = 2;
    g_phys_view = view;
    if (view == 1) physicsReset();
    else if (view == 2) springReset();
    g_mode = MODE_PHYSICS;
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_sd_get(httpd_req_t *req) {
    if (!sd_ensure()) {
        httpd_resp_sendstr(req, "SD not mounted");
        return ESP_FAIL;
    }
    FILE *f = fopen("/sdcard/voice_notes.txt", "r");
    if (!f) {
        httpd_resp_sendstr(req, "no notes yet");
        return ESP_FAIL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 200000) { fclose(f); httpd_resp_sendstr(req, "notes too large"); return ESP_FAIL; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); httpd_resp_sendstr(req, "no memory"); return ESP_FAIL; }
    size_t n = fread(buf, 1, sz, f);
    fclose(f);
    buf[n] = 0;
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t ret = httpd_resp_send(req, buf, n);
    free(buf);
    return ret;
}

static esp_err_t ota_root_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, OTA_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ota_version_get(httpd_req_t *req) {
    char json[192];
    snprintf(json, sizeof(json),
             "{\"version\":\"%s\",\"name\":\"%s\",\"build\":\"%s\"}",
             FW_VERSION, FW_NAME, FW_BUILD);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static char *g_pbm = nullptr;
static size_t g_pbm_len = 0;

static void pbm_out(const char *s) {
    size_t n = strlen(s);
    if (g_pbm && g_pbm_len + n < 200000) {
        memcpy(g_pbm + g_pbm_len, s, n);
        g_pbm_len += n;
    }
}

static esp_err_t ota_diag_get(httpd_req_t *req) {
    char json[1500];
    snprintf(json, sizeof(json),
        "{\"version\":\"%s\",\"mode\":%d,\"menu\":%d,\"view\":%d,\"phys_cat\":%d,\"voice_state\":%d,"
        "\"voice_msg\":\"%s\",\"codec_ret\":%d,\"i2c_ret\":%d,"
        "\"es8311\":%d,\"es7210\":%d,\"codec_stage\":%d,\"codec_err\":%d,\"spk\":%d,\"rec\":%d,"
        "\"rtc_ok\":%d,\"audio_ready\":%d,\"rec_open\":%d,\"voice_bytes\":%u,\"read_ret\":%d,\"read_calls\":%d,\"cap_conn\":%d,\"cap_kb\":%u,\"cap_drop\":%u,\"vad_active\":%d,\"vad_seg\":%u,\"sd_ok\":%d,\"batt_mv\":%d,\"batt_pct\":%d,\"charging\":%d,"
        "\"wifi\":%d,\"rssi\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"time\":\"%s\","
        "\"buttons\":{\"k1\":%d,\"k2\":%d,\"k3\":%d,\"k4\":%d,\"sw\":%d},"
        "\"joy\":{\"x\":%d,\"y\":%d},"
        "\"player\":{\"run\":%d,\"pause\":%d,\"fin\":%d,\"track\":%d,\"pos\":%u,\"dur\":%u,\"gen\":%u,\"level\":%d,\"stage\":%d,\"err\":\"%s\"}}",
        FW_VERSION, g_mode, g_menu_sel, g_phys_view, g_phys_cat, g_voice_state,
        g_voice_msg, g_codec_ret, g_i2c_ret,
        g_es8311 ? 1 : 0, g_es7210 ? 1 : 0, codec_debug_stage, codec_debug_err,
        g_spk_ok ? 1 : 0, g_rec_ok ? 1 : 0,
        g_rtc_ok ? 1 : 0, g_audio_ready ? 1 : 0, g_rec_open_ret, (unsigned)g_voice_bytes, g_read_ret, g_read_calls, g_cap_connected ? 1 : 0, (unsigned)(g_cap_sent / 1024), (unsigned)g_cap_drop, g_vad_active ? 1 : 0, (unsigned)g_vad_segments, g_sd_ok ? 1 : 0, g_batt_mv, g_batt_pct, g_charging ? 1 : 0,
        g_wifi_sta ? 1 : 0, g_rssi, g_ssid, g_ota_info, g_clock,
        in.k1, in.k2, in.k3, in.k4, in.sw, in.rawVrx, in.rawVry,
        g_player_running ? 1 : 0, g_player_paused ? 1 : 0, g_player_finished ? 1 : 0,
        g_player_track, (unsigned)g_player_pos_ms, (unsigned)g_player_dur_ms,
        (unsigned)g_player_gen, g_player_level, g_player_stage, g_player_err);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t ota_screen_get(httpd_req_t *req) {
    if (!g) {
        httpd_resp_sendstr(req, "display not ready");
        return ESP_FAIL;
    }
    if (!g_pbm) {
        g_pbm = (char *)heap_caps_malloc(200000, MALLOC_CAP_SPIRAM);
    }
    if (!g_pbm) {
        httpd_resp_sendstr(req, "no PSRAM for screen");
        return ESP_FAIL;
    }
    // Freeze rendering for one and a half frames before reading the buffer.
    g_screen_pause = true;
    vTaskDelay(pdMS_TO_TICKS(120));
    g_pbm_len = 0;
    u8g2_WriteBufferPBM(g, pbm_out);
    g_screen_pause = false;
    httpd_resp_set_type(req, "image/x-portable-bitmap");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, g_pbm, g_pbm_len);
}

static esp_err_t ota_update_post(httpd_req_t *req) {
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        httpd_resp_sendstr(req, "No OTA partition");
        return ESP_FAIL;
    }
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        httpd_resp_sendstr(req, "ota_begin failed");
        return ESP_FAIL;
    }
    char buf[4096];
    int remaining = req->content_len;
    int total = remaining;
    while (remaining > 0) {
        int want = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int got = httpd_req_recv(req, buf, want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) {
            esp_ota_abort(handle);
            httpd_resp_sendstr(req, "Receive failed");
            return ESP_FAIL;
        }
        err = esp_ota_write(handle, buf, got);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            httpd_resp_sendstr(req, "Flash write failed");
            return ESP_FAIL;
        }
        remaining -= got;
    }
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        httpd_resp_sendstr(req, "ota_end failed");
        return ESP_FAIL;
    }
    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        httpd_resp_sendstr(req, "set_boot_partition failed");
        return ESP_FAIL;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "OK: %d bytes written. Rebooting...", total);
    httpd_resp_sendstr(req, msg);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool rtc_init() {
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = 0x51;
    cfg.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(g_i2c_static.Get_I2cBusHandle(), &cfg, &g_rtc_dev) != ESP_OK) return false;
    uint8_t ctrl = 0;
    if (g_i2c_static.i2c_read_buff(g_rtc_dev, 0x00, &ctrl, 1) != 0) {
        i2c_master_bus_rm_device(g_rtc_dev);
        g_rtc_dev = nullptr;
        return false;
    }
    ctrl &= ~0x20; // clear STOP
    g_i2c_static.i2c_write_buff(g_rtc_dev, 0x00, &ctrl, 1);
    g_rtc_ok = true;
    return true;
}

static bool rtc_read_tm(struct tm *out) {
    if (!g_rtc_ok || !g_rtc_dev) return false;
    uint8_t r[7] = {0};
    if (g_i2c_static.i2c_read_buff(g_rtc_dev, 0x04, r, 7) != 0) return false;
    int sec = bcd2bin(r[0] & 0x7F);
    int min = bcd2bin(r[1] & 0x7F);
    int hour = bcd2bin(r[2] & 0x3F);
    int day = bcd2bin(r[3] & 0x3F);
    int mon = bcd2bin(r[5] & 0x1F);
    int year = 2000 + bcd2bin(r[6]);
    if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || mon < 1 || mon > 12 || year < 2020) return false;
    memset(out, 0, sizeof(*out));
    out->tm_sec = sec; out->tm_min = min; out->tm_hour = hour;
    out->tm_mday = day; out->tm_mon = mon - 1; out->tm_year = year - 1900;
    out->tm_isdst = -1;
    return true;
}

static void rtc_write_epoch(time_t now) {
    if (!g_rtc_ok || !g_rtc_dev) return;
    struct tm t;
    localtime_r(&now, &t);
    uint8_t r[7] = {
        bin2bcd(t.tm_sec), bin2bcd(t.tm_min), bin2bcd(t.tm_hour),
        bin2bcd(t.tm_mday), (uint8_t)t.tm_wday, bin2bcd(t.tm_mon + 1), bin2bcd(t.tm_year % 100)
    };
    g_i2c_static.i2c_write_buff(g_rtc_dev, 0x04, r, 7);
}

static void time_save_nvs(time_t now) {
    nvs_handle_t h;
    if (nvs_open("time", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i64(h, "epoch", (int64_t)now);
        nvs_commit(h);
        nvs_close(h);
    }
}

static time_t time_load_nvs() {
    nvs_handle_t h;
    int64_t v = 0;
    if (nvs_open("time", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i64(h, "epoch", &v);
        nvs_close(h);
    }
    return (time_t)v;
}

static void ota_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_sta = false;
        // Do not block the WiFi event task. The driver/IDF retry policy handles backoff.
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        char ipbuf[16];
        esp_ip4addr_ntoa(&ev->ip_info.ip, ipbuf, sizeof(ipbuf));
        snprintf(g_ota_info, sizeof(g_ota_info), "%s", ipbuf);
        g_wifi_sta = true;
        ESP_LOGI(TAG, "OTA LAN http://%s", ipbuf);
    }
}

static bool wifi_creds_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return false;
    size_t a = ssid_len, b = pass_len;
    bool ok = nvs_get_str(h, "ssid", ssid, &a) == ESP_OK &&
              nvs_get_str(h, "pass", pass, &b) == ESP_OK && ssid[0];
    nvs_close(h);
    return ok;
}

static void wifi_creds_save(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void ota_start(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs init failed (%s), trying erase", esp_err_to_name(ret));
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs init still failed: %s; continuing with fallback", esp_err_to_name(ret));
    }
    setenv("TZ", "CST-8", 1);
    tzset();
    rtc_init();
    struct tm rtc_tm;
    if (rtc_read_tm(&rtc_tm)) {
        time_t e = mktime(&rtc_tm);
        if (e > 1700000000) {
            struct timeval tv = { .tv_sec = e, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
        }
    } else {
        time_t e = time_load_nvs();
        if (e > 1700000000) {
            struct timeval tv = { .tv_sec = e, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
        }
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ota_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ota_wifi_event, NULL, NULL));

    wifi_config_t ap = {};
    strncpy((char *)ap.ap.ssid, "STICKMAN-RLCD", sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen("STICKMAN-RLCD");
    strncpy((char *)ap.ap.password, "stickman123", sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_config_t sta = {};
    char saved_ssid[33] = {0}, saved_pass[65] = {0};
    if (wifi_creds_load(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass))) {
        strncpy((char *)sta.sta.ssid, saved_ssid, sizeof(sta.sta.ssid));
        strncpy((char *)sta.sta.password, saved_pass, sizeof(sta.sta.password));
    } else {
        strncpy((char *)sta.sta.ssid, "YOUR_WIFI_SSID", sizeof(sta.sta.ssid));
        strncpy((char *)sta.sta.password, "YOUR_WIFI_PASSWORD", sizeof(sta.sta.password));
    }
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.stack_size = 8192;
    hcfg.max_uri_handlers = 24;
    hcfg.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &hcfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = ota_root_get, .user_ctx = NULL };
    httpd_uri_t ver = { .uri = "/version", .method = HTTP_GET, .handler = ota_version_get, .user_ctx = NULL };
    httpd_uri_t diag = { .uri = "/diag", .method = HTTP_GET, .handler = ota_diag_get, .user_ctx = NULL };
    httpd_uri_t sdg = { .uri = "/sdcard.txt", .method = HTTP_GET, .handler = ota_sd_get, .user_ctx = NULL };
    httpd_uri_t sdl = { .uri = "/sdcard.list", .method = HTTP_GET, .handler = ota_sd_list_get, .user_ctx = NULL };
    httpd_uri_t phy = { .uri = "/physics", .method = HTTP_GET, .handler = ota_physics_get, .user_ctx = NULL };
    httpd_uri_t menu = { .uri = "/menu", .method = HTTP_GET, .handler = ota_menu_get, .user_ctx = NULL };
    httpd_uri_t game = { .uri = "/game", .method = HTTP_GET, .handler = ota_game_get, .user_ctx = NULL };
    httpd_uri_t wifi = { .uri = "/wifi", .method = HTTP_GET, .handler = ota_wifi_get, .user_ctx = NULL };
    httpd_uri_t files = { .uri = "/files", .method = HTTP_GET, .handler = ota_files_get, .user_ctx = NULL };
    httpd_uri_t wordaudio = { .uri = "/wordaudio", .method = HTTP_GET, .handler = ota_wordaudio_get, .user_ctx = NULL };
    httpd_uri_t info = { .uri = "/info", .method = HTTP_GET, .handler = ota_info_get, .user_ctx = NULL };
    httpd_uri_t music = { .uri = "/music", .method = HTTP_GET, .handler = ota_music_get, .user_ctx = NULL };
    httpd_uri_t survey_report = { .uri = "/survey_report", .method = HTTP_GET, .handler = ota_survey_report_get, .user_ctx = NULL };
    httpd_uri_t back = { .uri = "/back", .method = HTTP_GET, .handler = ota_back_get, .user_ctx = NULL };
    httpd_uri_t upload = { .uri = "/upload", .method = HTTP_POST, .handler = ota_upload_post, .user_ctx = NULL };
    httpd_uri_t feedback = { .uri = "/feedback", .method = HTTP_POST, .handler = ota_feedback_post, .user_ctx = NULL };
    httpd_uri_t screen = { .uri = "/screen.pbm", .method = HTTP_GET, .handler = ota_screen_get, .user_ctx = NULL };
    httpd_uri_t upd = { .uri = "/update", .method = HTTP_POST, .handler = ota_update_post, .user_ctx = NULL };
    httpd_uri_t note = { .uri = "/note", .method = HTTP_POST, .handler = ota_note_post, .user_ctx = NULL };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &ver);
    httpd_register_uri_handler(server, &diag);
    httpd_register_uri_handler(server, &sdg);
    httpd_register_uri_handler(server, &sdl);
    httpd_register_uri_handler(server, &phy);
    httpd_register_uri_handler(server, &menu);
    httpd_register_uri_handler(server, &game);
    httpd_register_uri_handler(server, &wifi);
    httpd_register_uri_handler(server, &files);
    httpd_register_uri_handler(server, &wordaudio);
    httpd_register_uri_handler(server, &info);
    httpd_register_uri_handler(server, &music);
    httpd_register_uri_handler(server, &survey_report);
    httpd_register_uri_handler(server, &back);
    httpd_register_uri_handler(server, &upload);
    httpd_register_uri_handler(server, &feedback);
    httpd_register_uri_handler(server, &screen);
    httpd_register_uri_handler(server, &upd);
    httpd_register_uri_handler(server, &note);
    ESP_LOGI(TAG, "OTA AP STICKMAN-RLCD / stickman123 / 192.168.4.1, STA YOUR_WIFI_SSID");
}

static void drawZh(const ZhGlyph &z, int x, int y) {
    u8g2_SetBitmapMode(g, 1);
    u8g2_DrawBitmap(g, x, y, z.wb, ZH_H, z.data);
    u8g2_SetBitmapMode(g, 0);
}

static void drawZhCentered(const ZhGlyph &z, int y) {
    drawZh(z, (SCREEN_W - z.w) / 2, y);
}

static void drawCmdOverlay();
static void drawBackButton() {
    if (g_mode == MODE_MENU) return;
    u8g2_SetDrawColor(g, g_no_invert ? 0 : 1);
    u8g2_DrawFrame(g, 270, 1, 26, 15);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 276, 13, "<");
}
static const char *uiPathForMode() {
    switch (g_mode) {
        case MODE_WORD_MENU: return "SYS // FLASH";
        case MODE_WORD: return "SYS // FLASH // CARD";
        case MODE_PHYSICS: return "SYS // PHYSICS";
        case MODE_SETTINGS: return "SYS // CONFIG";
        case MODE_SETTINGS_CTRL: return "SYS // CONFIG // OUTPUT";
        case MODE_INFO: return "SYS // CONFIG // INFO";
        case MODE_HELP: return "SYS // CONFIG // HELP";
        case MODE_NOTES: return "SYS // VOICE // NOTES";
        case MODE_FEEDBACK: return "SYS // VOICE // FEEDBACK";
        case MODE_WIFI: return "SYS // NETWORK";
        case MODE_FILES: return "SYS // STORAGE";
        case MODE_MUSIC: {
            static char p[48];
            if (g_music_count > 0 && g_music_sel >= 0 && g_music_sel < g_music_count) {
                const char *n = g_file_names[g_music_files[g_music_sel]];
                const char *d = strrchr(n, '.');
                snprintf(p, sizeof(p), "[SYS // AUDIO // %s 16kHz]", d ? d + 1 : "TRACK");
                for (char *q = p + 17; *q; ++q) if (*q >= 'a' && *q <= 'z') *q -= 32;
            } else snprintf(p, sizeof(p), "[SYS // AUDIO]");
            return p;
        }
        case MODE_SURVEY_MENU: return "SYS // SURVEY";
        case MODE_SURVEY: return "SYS // SURVEY // FORM";
        case MODE_SURVEY_REPORT: return "SYS // SURVEY // REPORT";
        case MODE_GAME: return "SYS // FIGHT";
        case MODE_VOICE: return "SYS // VOICE";
        default: return "SYS // LAB";
    }
}

static void uiBottomKey(int x, const char *key, const char *action) {
    ui::keycap(x, 279, key, action);
}

static void uiDrawFrame() {
    if (!g_ui_frame) return;
    if (g_mode == MODE_MENU || g_mode == MODE_VIDEO || g_mode == MODE_IMAGE) return;
    ui::begin(g, g_no_invert);
    ui::Status st;
    st.wifi = g_wifi_sta; st.sd = g_sd_ok;
    st.batteryPct = g_batt_pct; st.charging = g_charging; st.time = g_clock;
    ui::topBar(uiPathForMode(), st);
    ui::bottomBar();

    if (g_mode == MODE_GAME) {
        uiBottomKey(6, "K3", "JAB");
        uiBottomKey(72, "K4", "KICK");
        uiBottomKey(148, "K1", "HOOK");
        uiBottomKey(232, "K2", "HIGH");
        uiBottomKey(324, "SW", "BACK");
    } else if (g_mode == MODE_MUSIC) {
        uiBottomKey(4, "JOY", "TRK/VOL");
        uiBottomKey(86, "SW", "PLAY");
        uiBottomKey(148, "K1", "-5s");
        uiBottomKey(203, "K2", "+5s");
        uiBottomKey(258, "K3", "LOOP");
        uiBottomKey(320, "K4", "BACK");
    } else if (g_mode == MODE_PHYSICS) {
        uiBottomKey(4, "JOY", "ADJ");
        uiBottomKey(88, "K3", "PLAY");
        uiBottomKey(166, "K4", "RESET");
        uiBottomKey(242, "C/D", "PAGE");
        uiBottomKey(324, "SW", "BACK");
    } else if (g_mode == MODE_INFO) {
        uiBottomKey(4, "JOY", "PAGE");
        uiBottomKey(150, "K4", "BACK");
        uiBottomKey(310, "SW", "BACK");
    } else if (g_mode == MODE_SETTINGS_CTRL) {
        uiBottomKey(4, "JOY", "ADJ");
        uiBottomKey(100, "K3", "TEST");
        uiBottomKey(196, "K4", "RESET");
        uiBottomKey(310, "SW", "BACK");
    } else if (g_mode == MODE_WIFI) {
        uiBottomKey(4, "JOY", "SELECT");
        uiBottomKey(108, "K3", "CONNECT");
        uiBottomKey(220, "K4", "SCAN");
        uiBottomKey(318, "SW", "BACK");
    } else if (g_mode == MODE_HELP || g_mode == MODE_NOTES || g_mode == MODE_FEEDBACK) {
        uiBottomKey(4, "JOY", "SCROLL");
        uiBottomKey(160, "SW", "BACK");
    } else if (g_mode == MODE_SURVEY_MENU) {
        uiBottomKey(4, "JOY", "SELECT");
        uiBottomKey(150, "K3", "START");
        uiBottomKey(300, "SW", "BACK");
    } else if (g_mode == MODE_SURVEY) {
        uiBottomKey(4, "JOY", "SELECT");
        uiBottomKey(150, "K3", "NEXT");
        uiBottomKey(300, "SW", "BACK");
    } else if (g_mode == MODE_SURVEY_REPORT) {
        uiBottomKey(4, "JOY", "SCROLL");
        uiBottomKey(150, "K3", "MENU");
        uiBottomKey(300, "SW", "BACK");
    } else if (g_mode == MODE_WORD) {
        uiBottomKey(4, "JOY", "CARD");
        uiBottomKey(110, "K3", "FLIP");
        if (g_word_level < 2) uiBottomKey(210, "K2", "AUDIO");
        uiBottomKey(318, "SW", "BACK");
    } else {
        uiBottomKey(6, "JOY", "SELECT");
        uiBottomKey(108, "K3", "OPEN");
        uiBottomKey(202, "K4", "HOLD");
        uiBottomKey(310, "SW", "BACK");
    }
}

static void presentFrame() {
    if (g_cmd_recording || g_cmd_busy) drawCmdOverlay();
    uiDrawFrame();
    if (!g_ui_frame || g_mode == MODE_MENU || g_mode == MODE_VIDEO || g_mode == MODE_IMAGE) drawBackButton();
    uint8_t *fb = u8g2_GetBufferPtr(g);
    if (fb && !g_no_invert) {
        size_t fbBytes = (size_t)u8g2_GetBufferTileWidth(g) *
                         u8g2_GetBufferTileHeight(g) * 8;
        for (size_t i = 0; i < fbBytes; ++i) fb[i] ^= 0xFF;
    }
    u8g2_SendBuffer(g);
}

static bool updateBackChord(float dt) {
    if (in.k3 && in.k1) {
        g_back_hold += dt;
        if (g_back_hold >= 0.6f) {
            g_back_hold = 0.0f;
            return true;
        }
    } else {
        g_back_hold = 0.0f;
    }
    return false;
}

static const char *reset_reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default: return "OTHER";
    }
}

static void drawInfo() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 34, 396, 212);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    char b[96];
    if (g_info_page == 0) {
        esp_chip_info_t chip; esp_chip_info(&chip);
        uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(b, sizeof(b), "FW      %s", FW_VERSION); u8g2_DrawStr(g, 8, 70, b);
        snprintf(b, sizeof(b), "Build   %s %s", __DATE__, __TIME__); u8g2_DrawStr(g, 8, 92, b);
        snprintf(b, sizeof(b), "IDF     %s", esp_get_idf_version()); u8g2_DrawStr(g, 8, 114, b);
        snprintf(b, sizeof(b), "Chip    %s  %d core", chip.model == CHIP_ESP32S3 ? "ESP32-S3" : "ESP32", chip.cores); u8g2_DrawStr(g, 8, 136, b);
        snprintf(b, sizeof(b), "CPU     %u MHz", (unsigned)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ); u8g2_DrawStr(g, 8, 158, b);
        snprintf(b, sizeof(b), "MAC     %02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]); u8g2_DrawStr(g, 8, 180, b);
        snprintf(b, sizeof(b), "Display 400x300 1bpp"); u8g2_DrawStr(g, 8, 202, b);
        snprintf(b, sizeof(b), "SD      %s", g_sd_ok ? "mounted" : "not mounted"); u8g2_DrawStr(g, 8, 224, b);
    } else if (g_info_page == 1) {
        uint32_t flash = 0; esp_flash_get_size(NULL, &flash);
        size_t hf = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t hm = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t pf = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t pt = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        uint64_t st = 0, sf = 0; esp_vfs_fat_info("/sdcard", &st, &sf);
        snprintf(b, sizeof(b), "Heap    %uK free", (unsigned)(hf / 1024)); u8g2_DrawStr(g, 8, 70, b);
        snprintf(b, sizeof(b), "Heap min %uK", (unsigned)(hm / 1024)); u8g2_DrawStr(g, 8, 92, b);
        snprintf(b, sizeof(b), "PSRAM   %uK free", (unsigned)(pf / 1024)); u8g2_DrawStr(g, 8, 114, b);
        snprintf(b, sizeof(b), "PSRAM   %uK total", (unsigned)(pt / 1024)); u8g2_DrawStr(g, 8, 136, b);
        snprintf(b, sizeof(b), "Flash   %u KB", (unsigned)(flash / 1024)); u8g2_DrawStr(g, 8, 158, b);
        snprintf(b, sizeof(b), "SD free %u MB", (unsigned)(sf / (1024 * 1024))); u8g2_DrawStr(g, 8, 180, b);
        snprintf(b, sizeof(b), "SD total %u MB", (unsigned)(st / (1024 * 1024))); u8g2_DrawStr(g, 8, 202, b);
        snprintf(b, sizeof(b), "SD used %u MB", (unsigned)((st - sf) / (1024 * 1024))); u8g2_DrawStr(g, 8, 224, b);
    } else if (g_info_page == 2) {
        snprintf(b, sizeof(b), "WiFi    %s  %d dBm", g_wifi_sta ? "connected" : "off", g_rssi); u8g2_DrawStr(g, 8, 70, b);
        snprintf(b, sizeof(b), "SSID    %s", g_ssid[0] ? g_ssid : "-"); u8g2_DrawStr(g, 8, 92, b);
        snprintf(b, sizeof(b), "IP      %s", g_ota_info); u8g2_DrawStr(g, 8, 114, b);
        snprintf(b, sizeof(b), "Battery %d%%  %dmV", g_batt_pct, g_batt_mv); u8g2_DrawStr(g, 8, 136, b);
        snprintf(b, sizeof(b), "Uptime  %lu s", (unsigned long)(esp_timer_get_time() / 1000000)); u8g2_DrawStr(g, 8, 158, b);
        snprintf(b, sizeof(b), "Reset   %s", reset_reason_str(esp_reset_reason())); u8g2_DrawStr(g, 8, 180, b);
        snprintf(b, sizeof(b), "RTC     %s", g_rtc_ok ? "PCF85063A OK" : "not found"); u8g2_DrawStr(g, 8, 202, b);
        snprintf(b, sizeof(b), "UI      5 items / 2 pages"); u8g2_DrawStr(g, 8, 224, b);
    } else {
        TaskStatus_t tasks[24];
        UBaseType_t n = uxTaskGetSystemState(tasks, 24, nullptr);
        snprintf(b, sizeof(b), "TASKS %u   NAME       ST PRI STACK", (unsigned)n);
        u8g2_DrawStr(g, 8, 62, b);
        for (UBaseType_t i = 0; i < n && i < 10; ++i) {
            char st = '?';
            switch (tasks[i].eCurrentState) {
                case eRunning: st = 'R'; break;
                case eReady: st = 'r'; break;
                case eBlocked: st = 'B'; break;
                case eSuspended: st = 'S'; break;
                case eDeleted: st = 'D'; break;
                default: st = '?'; break;
            }
            snprintf(b, sizeof(b), "%-16.16s %c %2u %5u",
                     tasks[i].pcTaskName, st, (unsigned)tasks[i].uxCurrentPriority,
                     (unsigned)tasks[i].usStackHighWaterMark);
            u8g2_DrawStr(g, 8, 82 + i * 16, b);
        }
    }
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "PAGE %d/4   UP/DOWN", g_info_page + 1); u8g2_DrawStr(g, 8, 252, b);
    u8g2_DrawStr(g, 8, 292, "Compact hardware / software info");
}


static void files_scan() {
    g_file_count = 0;
    // Remove legacy upload names written as tts%2F... before URL decoding was fixed.
    DIR *clean = opendir("/sdcard");
    if (clean) {
        struct dirent *ce;
        while ((ce = readdir(clean)) != NULL) {
            if (strstr(ce->d_name, "%2F")) {
                char bad[300];
                snprintf(bad, sizeof(bad), "/sdcard/%s", ce->d_name);
                unlink(bad);
            }
        }
        closedir(clean);
    }
    DIR *d = opendir("/sdcard");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && g_file_count < 24) {
        if (e->d_name[0] == '.') continue;
        char path[300];
        snprintf(path, sizeof(path), "/sdcard/%s", e->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        strncpy(g_file_names[g_file_count], e->d_name, sizeof(g_file_names[0]) - 1);
        g_file_names[g_file_count][sizeof(g_file_names[0]) - 1] = 0;
        g_file_sizes[g_file_count] = (uint32_t)st.st_size;
        ++g_file_count;
    }
    closedir(d);
    if (g_file_sel >= g_file_count) g_file_sel = 0;
}

static void files_load_text_page() {
    if (!g_file_is_txt || g_file_sel < 0 || g_file_sel >= g_file_count) return;
    char path[300];
    snprintf(path, sizeof(path), "/sdcard/%s", g_file_names[g_file_sel]);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(g_file_preview, sizeof(g_file_preview), "无法打开文件"); return; }
    fseek(f, g_file_scroll, SEEK_SET);
    size_t got = fread(g_file_preview, 1, 1400, f);
    g_file_preview[got] = 0;
    fclose(f);
    if (!got) snprintf(g_file_preview, sizeof(g_file_preview), "(页面结束)");
}

static void wav_stop_playback() {
    if (g_wav_playing) g_wav_stop = true;
}

static void wav_play_task(void *) {
    char path[300];
    snprintf(path, sizeof(path), "/sdcard/%s", g_file_names[g_file_sel]);
    FILE *f = fopen(path, "rb");
    if (!f) { g_wav_playing = false; vTaskDelete(NULL); return; }
    uint8_t h[44] = {0};
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h, "RIFF", 4) != 0 ||
        memcmp(h + 8, "WAVE", 4) != 0) {
        fclose(f); g_wav_playing = false; vTaskDelete(NULL); return;
    }
    uint16_t ch = h[22] | (h[23] << 8);
    uint16_t bits = h[34] | (h[35] << 8);
    uint32_t data = h[40] | (h[41] << 8) | (h[42] << 16) | ((uint32_t)h[43] << 24);
    if ((ch != 1 && ch != 2) || bits != 16) {
        fclose(f); g_wav_playing = false; vTaskDelete(NULL); return;
    }
    fseek(f, 44, SEEK_SET);
    g_wav_total = data > 44 ? data - 44 : 0;
    g_wav_pos = 0;
    g_wav_playing = true;
    static int16_t in[1024];
    static int16_t out[2048];
    while (!g_wav_stop) {
        size_t got = fread(in, 1, sizeof(in), f);
        if (got < 2) break;
        if (ch == 2) {
            g_codec->CodecPort_PlayWrite(in, got);
        } else {
            int n = got / 2;
            for (int i = 0; i < n; ++i) { out[i*2] = in[i]; out[i*2+1] = in[i]; }
            g_codec->CodecPort_PlayWrite(out, n * 4);
        }
        g_wav_pos += got;
    }
    fclose(f);
    g_wav_playing = false;
    g_track_finished = true;
    vTaskDelete(NULL);
}

static void wav_start_playback() {
    if (g_wav_playing) { g_wav_stop = true; return; }
    g_wav_stop = false;
    g_wav_pos = 0;
    g_track_finished = false;
    g_wav_playing = true;
    xTaskCreate(wav_play_task, "wav_play", 16384, NULL, 3, NULL);
}

static void mp3_play_task(void *) {
    char path[300];
    if (g_audio_path[0]) snprintf(path, sizeof(path), "%s", g_audio_path);
    else snprintf(path, sizeof(path), "/sdcard/%s", g_file_names[g_file_sel]);
    FILE *f = fopen(path, "rb");
    if (!f) { g_mp3_playing = false; vTaskDelete(NULL); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(f); g_mp3_playing = false; vTaskDelete(NULL); return; }
    uint8_t *data = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!data) data = (uint8_t *)malloc(sz);
    if (!data || fread(data, 1, sz, f) != (size_t)sz) {
        if (data) free(data);
        fclose(f); g_mp3_playing = false; vTaskDelete(NULL); return;
    }
    fclose(f);
    size_t pos = 0;
    if (sz > 10 && memcmp(data, "ID3", 3) == 0) {
        pos = 10 + (((size_t)(data[6] & 0x7f) << 21) |
                    ((size_t)(data[7] & 0x7f) << 14) |
                    ((size_t)(data[8] & 0x7f) << 7) |
                    (size_t)(data[9] & 0x7f));
    }
    g_mp3_total = sz;
    g_mp3_pos = pos;
    g_mp3_playing = true;
    mp3dec_t dec;
    mp3dec_init(&dec);
    static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    static int16_t out[2048];
    int outn = 0;
    double phase = 0.0;
    while (pos + 4 < (size_t)sz && !g_mp3_stop) {
        mp3dec_frame_info_t info = {};
        int samples = mp3dec_decode_frame(&dec, data + pos, sz - pos, pcm, &info);
        if (info.frame_bytes <= 0) break;
        pos += info.frame_bytes;
        g_mp3_pos = pos;
        if (samples <= 0 || info.hz <= 0) continue;
        double step = (double)info.hz / 16000.0;
        while (phase < samples - 1) {
            int i = (int)phase;
            float frac = (float)(phase - i);
            int16_t l0 = pcm[i * info.channels];
            int16_t l1 = pcm[(i + 1) * info.channels];
            int16_t l = (int16_t)(l0 + (l1 - l0) * frac);
            int16_t r = l;
            if (info.channels > 1) {
                int16_t r0 = pcm[i * info.channels + 1];
                int16_t r1 = pcm[(i + 1) * info.channels + 1];
                r = (int16_t)(r0 + (r1 - r0) * frac);
            }
            out[outn * 2] = l;
            out[outn * 2 + 1] = r;
            if (++outn >= 1024) {
                g_codec->CodecPort_PlayWrite(out, outn * 4);
                outn = 0;
            }
            phase += step;
        }
        phase -= samples;
    }
    if (outn > 0) g_codec->CodecPort_PlayWrite(out, outn * 4);
    free(data);
    g_mp3_playing = false;
    g_track_finished = true;
    vTaskDelete(NULL);
}

static void mp3_start_playback() {
    if (g_mp3_playing) { g_mp3_stop = true; return; }
    g_mp3_stop = false;
    g_mp3_pos = 0;
    g_track_finished = false;
    g_mp3_playing = true;
    xTaskCreate(mp3_play_task, "mp3_play", 32768, NULL, 3, NULL);
}

static void image_view_file() {
    char path[300];
    snprintf(path, sizeof(path), "/sdcard/%s", g_file_names[g_file_sel]);
    int w = 0, h = 0, n = 0;
    unsigned char *data = stbi_load(path, &w, &h, &n, 1);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        snprintf(g_file_preview, sizeof(g_file_preview), "图片解码失败");
        g_file_view = 1;
        return;
    }
    if (g_img_gray) { heap_caps_free(g_img_gray); g_img_gray = nullptr; }
    g_img_gray = (uint8_t *)heap_caps_malloc(400 * 300, MALLOC_CAP_SPIRAM);
    if (!g_img_gray) g_img_gray = (uint8_t *)malloc(400 * 300);
    if (!g_img_gray) { stbi_image_free(data); snprintf(g_file_preview, sizeof(g_file_preview), "图片内存不足"); g_file_view = 1; return; }
    memset(g_img_gray, 255, 400 * 300);
    float scale = (400.0f / w < 300.0f / h) ? (400.0f / w) : (300.0f / h);
    int dw = (int)(w * scale), dh = (int)(h * scale);
    if (dw > 400) dw = 400;
    if (dh > 300) dh = 300;
    int ox = (400 - dw) / 2, oy = (300 - dh) / 2;
    for (int y = 0; y < dh; ++y) {
        int sy = (int)((long)y * h / dh);
        for (int x = 0; x < dw; ++x) {
            int sx = (int)((long)x * w / dw);
            g_img_gray[(oy + y) * 400 + (ox + x)] = data[sy * w + sx];
        }
    }
    stbi_image_free(data);
    g_mode = MODE_IMAGE;
}

static void files_free_text() {
}

static void video_close() {
    if (g_video_f) { fclose(g_video_f); g_video_f = nullptr; }
    g_video_playing = false;
}

static void video_draw() {
    g_no_invert = false;
    g_dark_ui = false;
    if (!g_video_f || g_video_w != 400 || g_video_h != 300) return;
    fseek(g_video_f, 12L + (long)g_video_idx * 15000L, SEEK_SET);
    if (fread(g_video_frame, 1, sizeof(g_video_frame), g_video_f) != sizeof(g_video_frame)) return;
    u8g2_ClearBuffer(g);
    u8g2_SetDrawColor(g, 1);
    u8g2_SetBitmapMode(g, 1);
    u8g2_DrawBitmap(g, 0, 0, 50, 300, g_video_frame);
    u8g2_SetBitmapMode(g, 0);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    char b[32];
    snprintf(b, sizeof(b), "%u/%u", g_video_idx + 1, g_video_frames);
    u8g2_DrawStr(g, 8, 292, b);
}

static bool video_open(const char *path) {
    video_close();
    g_no_invert = false;
    g_dark_ui = false;
    g_video_f = fopen(path, "rb");
    if (!g_video_f) return false;
    uint8_t h[12];
    if (fread(h, 1, sizeof(h), g_video_f) != sizeof(h) || memcmp(h, "VID1", 4) != 0) {
        video_close(); return false;
    }
    g_video_w = h[4] | (h[5] << 8);
    g_video_h = h[6] | (h[7] << 8);
    g_video_fps = h[8];
    g_video_frames = h[10] | (h[11] << 8);
    if (g_video_w != 400 || g_video_h != 300 || g_video_fps == 0 || g_video_frames == 0) {
        video_close(); return false;
    }
    g_video_idx = 0;
    g_video_playing = true;
    g_video_last_us = esp_timer_get_time();
    g_mode = MODE_VIDEO;
    return true;
}

static void video_input(float dt) {
    (void)dt;
    if (in.k3 && !prevIn.k3) g_video_playing = !g_video_playing;
    if (in.x < 0 && prevIn.x >= 0) {
        g_video_idx = g_video_idx ? g_video_idx - 1 : g_video_frames - 1;
        video_draw();
        g_video_last_us = esp_timer_get_time();
    }
    if (in.x > 0 && prevIn.x <= 0) {
        g_video_idx = (g_video_idx + 1) % g_video_frames;
        video_draw();
        g_video_last_us = esp_timer_get_time();
    }
    prevIn = in;
}

static void video_update() {
    if (!g_video_playing || !g_video_f) return;
    int64_t now = esp_timer_get_time();
    int64_t step = 1000000LL / g_video_fps;
    if (now - g_video_last_us >= step) {
        g_video_last_us += step;
        g_video_idx = (g_video_idx + 1) % g_video_frames;
        video_draw();
    }
}

static void files_enter() {
    g_audio_path[0] = 0;
    wav_stop_playback();
    if (g_mp3_playing) g_mp3_stop = true;
    if (g_img_gray) { heap_caps_free(g_img_gray); g_img_gray = nullptr; }
    g_file_view = 0;
    g_file_sel = 0;
    g_file_off = 0;
    g_file_is_txt = g_file_is_wav = g_file_is_mp3 = g_file_is_gif = false;
    g_mode = MODE_FILES;
    files_scan();
}

static void files_preview() {
    if (g_file_sel < 0 || g_file_sel >= g_file_count) return;
    g_audio_path[0] = 0;
    wav_stop_playback();
    if (g_mp3_playing) g_mp3_stop = true;
    if (g_img_gray) { heap_caps_free(g_img_gray); g_img_gray = nullptr; }
    snprintf(g_file_title, sizeof(g_file_title), "%s", g_file_names[g_file_sel]);
    g_file_preview[0] = 0;
    g_file_scroll = 0;
    char path[300];
    snprintf(path, sizeof(path), "/sdcard/%s", g_file_names[g_file_sel]);
    int n = strlen(g_file_title);
    g_file_is_wav = n > 4 && strcasecmp(g_file_title + n - 4, ".wav") == 0;
    g_file_is_txt = n > 4 && strcasecmp(g_file_title + n - 4, ".txt") == 0;
    g_file_is_mp3 = n > 4 && strcasecmp(g_file_title + n - 4, ".mp3") == 0;
    g_file_is_gif = (n > 4 && strcasecmp(g_file_title + n - 4, ".gif") == 0) ||
                    (n > 4 && strcasecmp(g_file_title + n - 4, ".png") == 0) ||
                    (n > 4 && strcasecmp(g_file_title + n - 4, ".jpg") == 0) ||
                    (n > 4 && strcasecmp(g_file_title + n - 4, ".jpeg") == 0) ||
                    (n > 4 && strcasecmp(g_file_title + n - 4, ".bmp") == 0);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(g_file_preview, sizeof(g_file_preview), "无法打开文件"); g_file_view = 1; return; }
    int fn = strlen(g_file_title);
    if (fn > 4 && strcasecmp(g_file_title + fn - 4, ".vid") == 0) {
        fclose(f);
        if (!video_open(path)) {
            snprintf(g_file_preview, sizeof(g_file_preview), "VID 格式错误");
            g_file_view = 1;
        }
        return;
    }
    if (g_file_is_txt) {
        fclose(f);
        files_load_text_page();
        g_file_view = 1;
        return;
    } else if (g_file_is_wav) {
        uint8_t h[44] = {0};
        fread(h, 1, sizeof(h), f);
        uint32_t sr = h[24] | (h[25] << 8) | (h[26] << 16) | ((uint32_t)h[27] << 24);
        uint16_t ch = h[22] | (h[23] << 8);
        uint16_t bits = h[34] | (h[35] << 8);
        uint32_t ds = h[40] | (h[41] << 8) | (h[42] << 16) | ((uint32_t)h[43] << 24);
        snprintf(g_file_preview, sizeof(g_file_preview),
                 "WAV %uHz %uch %ubit  %.1fs  %uKB",
                 (unsigned)sr, (unsigned)ch, (unsigned)bits,
                 sr && ch && bits ? (float)ds / (sr * ch * bits / 8) : 0.0f,
                 (unsigned)((ds + 1023) / 1024));
    } else if (g_file_is_mp3) {
        snprintf(g_file_preview, sizeof(g_file_preview), "MP3 音频\n%u 字节\n\nA: 播放", (unsigned)g_file_sizes[g_file_sel]);
    } else if (g_file_is_gif) {
        snprintf(g_file_preview, sizeof(g_file_preview), "图片文件\n%u 字节\n\nA: 查看", (unsigned)g_file_sizes[g_file_sel]);
    } else {
        snprintf(g_file_preview, sizeof(g_file_preview), "文件大小 %u 字节\n没有预览", (unsigned)g_file_sizes[g_file_sel]);
    }
    fclose(f);
    g_file_view = 1;
}

static void drawFiles() {
    drawStatus();
    if (g_file_view == 0) {
        u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
        u8g2_SetFont(g, u8g2_font_7x13_tf);
        char b[64];
        snprintf(b, sizeof(b), "%d FILES", g_file_count);
        u8g2_DrawStr(g, 16, 56, b);
        if (g_file_sel < g_file_off) g_file_off = g_file_sel;
        if (g_file_sel >= g_file_off + 8) g_file_off = g_file_sel - 7;
        for (int i = g_file_off; i < g_file_count && i < g_file_off + 8; ++i) {
            int y = 76 + (i - g_file_off) * 23;
            snprintf(b, sizeof(b), "%.26s", g_file_names[i]);
            u8g2_DrawStr(g, 18, y, b);
            snprintf(b, sizeof(b), "%uK", (unsigned)((g_file_sizes[i] + 1023) / 1024));
            u8g2_DrawStr(g, 348, y, b);
            if (i == g_file_sel) u8g2_DrawFrame(g, 8, y - 13, 384, 18);
        }
    } else {
        u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
        draw_wrapped_utf8(g_file_title, 8, 32, 384, 16, 1);
        if (g_file_is_txt) {
            draw_wrapped_utf8(g_file_preview, 8, 52, 384, 16, 13);
            u8g2_SetFont(g, u8g2_font_7x13_tf);
        } else if (g_file_is_wav) {
            draw_wrapped_utf8(g_file_preview, 8, 52, 384, 16, 8);
            int total = g_wav_total ? g_wav_total : 1;
            int pct = (int)((uint64_t)g_wav_pos * 100 / total);
            u8g2_SetFont(g, u8g2_font_7x13_tf);
            u8g2_DrawFrame(g, 8, 224, 384, 16);
            u8g2_DrawBox(g, 10, 226, 380 * pct / 100, 12);
            char b[48]; snprintf(b, sizeof(b), "%d%%  %s", pct, g_wav_playing ? "PLAYING" : "STOP");
            u8g2_DrawStr(g, 12, 254, b);
        } else if (g_file_is_mp3) {
            draw_wrapped_utf8(g_file_preview, 8, 52, 384, 16, 8);
            int total = g_mp3_total ? g_mp3_total : 1;
            int pct = (int)((uint64_t)g_mp3_pos * 100 / total);
            u8g2_SetFont(g, u8g2_font_7x13_tf);
            u8g2_DrawFrame(g, 8, 224, 384, 16);
            u8g2_DrawBox(g, 10, 226, 380 * pct / 100, 12);
            char b[48]; snprintf(b, sizeof(b), "%d%%  %s", pct, g_mp3_playing ? "PLAYING" : "STOP");
            u8g2_DrawStr(g, 12, 254, b);
        } else if (g_file_is_gif) {
            draw_wrapped_utf8(g_file_preview, 8, 52, 384, 16, 8);
            u8g2_SetFont(g, u8g2_font_7x13_tf);
        } else {
            draw_wrapped_utf8(g_file_preview, 8, 52, 384, 16, 10);
            u8g2_SetFont(g, u8g2_font_7x13_tf);
        }
    }
}

static void handleFilesInput() {
    if (g_file_view == 0) {
        if (in.up && !prevIn.up && g_file_count) g_file_sel = (g_file_sel + g_file_count - 1) % g_file_count;
        if (in.down && !prevIn.down && g_file_count) g_file_sel = (g_file_sel + 1) % g_file_count;
        if (g_file_sel < g_file_off) g_file_off = g_file_sel;
        if (g_file_sel >= g_file_off + 8) g_file_off = g_file_sel - 7;
        if (in.k3 && !prevIn.k3) files_preview();
    } else if (g_file_is_txt) {
        if (in.up && !prevIn.up) {
            g_file_scroll = (g_file_scroll > 1200) ? g_file_scroll - 1200 : 0;
            files_load_text_page();
        }
        if (in.down && !prevIn.down) {
            g_file_scroll += 1200;
            files_load_text_page();
        }
        if (in.k3 && !prevIn.k3) { g_file_scroll = 0; files_load_text_page(); }
    } else if (g_file_is_wav) {
        if (in.k3 && !prevIn.k3) wav_start_playback();
    } else if (g_file_is_mp3) {
        if (in.k3 && !prevIn.k3) mp3_start_playback();
    } else if (g_file_is_gif) {
        if (in.k3 && !prevIn.k3) image_view_file();
    }
    prevIn = in;
}


static bool music_is_audio(const char *name) {
    int n = strlen(name);
    return (n > 4 && strcasecmp(name + n - 4, ".mp3") == 0) ||
           (n > 4 && strcasecmp(name + n - 4, ".wav") == 0) ||
           (n > 5 && strcasecmp(name + n - 5, ".flac") == 0);
}

static const char *music_format(const char *name) {
    int n = strlen(name);
    if (n > 4 && strcasecmp(name + n - 4, ".mp3") == 0) return "MP3";
    if (n > 4 && strcasecmp(name + n - 4, ".wav") == 0) return "WAV";
    if (n > 5 && strcasecmp(name + n - 5, ".flac") == 0) return "FLAC";
    return "AUDIO";
}

static void music_scan() {
    files_scan();
    g_music_count = 0;
    for (int i = 0; i < g_file_count && g_music_count < 24; ++i)
        if (music_is_audio(g_file_names[i])) g_music_files[g_music_count++] = i;
    if (g_music_sel >= g_music_count) g_music_sel = 0;
    if (g_music_off > g_music_sel) g_music_off = g_music_sel;
}

static uint32_t music_duration_ms(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t h[16] = {0};
    uint32_t dur = 0;
    if (fread(h, 1, sizeof(h), f) == sizeof(h)) {
        int n = strlen(path);
        if (n > 5 && strcasecmp(path + n - 5, ".flac") == 0) {
            // FLAC STREAMINFO is at byte 8; sample rate is bits 80..99, total samples bits 108..143.
            fseek(f, 8, SEEK_SET);
            uint8_t si[34];
            if (fread(si, 1, sizeof(si), f) == sizeof(si)) {
                uint32_t rate = ((uint32_t)si[10] << 12) | ((uint32_t)si[11] << 4) | (si[12] >> 4);
                uint64_t total = ((uint64_t)(si[13] & 0x0F) << 32) | ((uint64_t)si[14] << 24) |
                                 ((uint64_t)si[15] << 16) | ((uint64_t)si[16] << 8) | si[17];
                if (rate > 0) dur = (uint32_t)(total * 1000 / rate);
            }
        } else if (n > 4 && strcasecmp(path + n - 4, ".mp3") == 0) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            uint32_t off = 0;
            fseek(f, 0, SEEK_SET);
            uint8_t id3[10];
            if (fread(id3, 1, 10, f) == 10 && memcmp(id3, "ID3", 3) == 0) {
                off = 10 + (((uint32_t)(id3[6] & 0x7f) << 21) | ((uint32_t)(id3[7] & 0x7f) << 14) |
                            ((uint32_t)(id3[8] & 0x7f) << 7) | (id3[9] & 0x7f));
            }
            fseek(f, off, SEEK_SET);
            uint8_t hdr[4];
            if (fread(hdr, 1, 4, f) == 4 && hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0) {
                static const int br[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
                int bi = (hdr[2] >> 4) & 0x0F;
                int sr_i = (hdr[2] >> 2) & 0x03;
                static const int sr[4] = {44100,48000,32000,0};
                int bitrate = br[bi];
                int rate = sr[sr_i];
                if (bitrate && rate && sz > (long)off) dur = (uint32_t)((sz - off) * 8ULL * 1000 / (bitrate * 1000));
            }
        } else if (n > 4 && strcasecmp(path + n - 4, ".wav") == 0) {
            uint8_t hdr[12];
            fseek(f, 0, SEEK_SET);
            if (fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "RIFF", 4) == 0) {
                uint32_t rate = 0; uint16_t ch = 0, bits = 0; uint32_t data_len = 0;
                while (true) {
                    uint8_t chk[8];
                    if (fread(chk, 1, 8, f) != 8) break;
                    uint32_t csz = chk[4] | (chk[5] << 8) | (chk[6] << 16) | ((uint32_t)chk[7] << 24);
                    if (memcmp(chk, "fmt ", 4) == 0) {
                        uint8_t fm[16];
                        if (fread(fm, 1, 16, f) != 16) break;
                        ch = fm[2] | (fm[3] << 8);
                        rate = fm[4] | (fm[5] << 8) | (fm[6] << 16) | ((uint32_t)fm[7] << 24);
                        bits = fm[14] | (fm[15] << 8);
                        if (csz > 16) fseek(f, csz - 16, SEEK_CUR);
                    } else if (memcmp(chk, "data", 4) == 0) {
                        data_len = csz;
                        break;
                    } else {
                        fseek(f, csz, SEEK_CUR);
                    }
                    if (csz & 1) fseek(f, 1, SEEK_CUR);
                }
                if (rate && ch && bits == 16) dur = (uint32_t)((uint64_t)data_len * 1000 / (rate * ch * 2));
            }
        }
    }
    fclose(f);
    return dur;
}

static void player_apply_volume() {
    if (g_codec) g_codec->CodecPort_SetSpeakerVol(g_volume);
}

static void *flac_malloc(size_t sz, void *) {
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void *flac_realloc(void *p, size_t sz, void *) {
    return heap_caps_realloc(p, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void flac_free(void *p, void *) {
    heap_caps_free(p);
}
static drflac_allocation_callbacks g_flac_cb = { nullptr, flac_malloc, flac_realloc, flac_free };

static bool load_file_mem(const char *path, uint8_t **out_data, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 20 * 1024 * 1024) { fclose(f); return false; }
    uint8_t *data = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!data) data = (uint8_t *)malloc(sz);
    if (!data || fread(data, 1, sz, f) != (size_t)sz) {
        if (data) free(data);
        fclose(f); return false;
    }
    fclose(f);
    *out_data = data; *out_size = sz;
    return true;
}

static int16_t player_out[2048];
static int player_outn = 0;

static void player_emit(const int16_t *src, int frames, int ch, int rate, double *phase) {
    if (frames <= 0 || ch <= 0 || rate <= 0) return;
    double step = (double)rate / 16000.0;
    while (*phase < frames - 1) {
        int i = (int)(*phase);
        float frac = (float)(*phase - i);
        int16_t l0 = src[i * ch];
        int16_t l1 = src[(i + 1) * ch];
        int16_t l = (int16_t)(l0 + (l1 - l0) * frac);
        int16_t r = l;
        if (ch > 1) {
            int16_t r0 = src[i * ch + 1];
            int16_t r1 = src[(i + 1) * ch + 1];
            r = (int16_t)(r0 + (r1 - r0) * frac);
        }
        player_out[player_outn * 2] = l;
        player_out[player_outn * 2 + 1] = r;
        int a = l < 0 ? -l : l;
        if (a > g_player_level) g_player_level = a;
        if (++player_outn >= 1024) {
            g_codec->CodecPort_PlayWrite(player_out, player_outn * 4);
            player_outn = 0;
        }
        *phase += step;
    }
    *phase -= frames;
}

static bool wav_parse(const uint8_t *d, size_t sz, uint32_t *rate, uint16_t *ch, uint32_t *off, uint32_t *len) {
    if (sz < 44 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return false;
    size_t p = 12;
    uint16_t bits = 0;
    bool got_fmt = false;
    while (p + 8 <= sz) {
        uint32_t csz = d[p+4] | (d[p+5] << 8) | (d[p+6] << 16) | ((uint32_t)d[p+7] << 24);
        if (memcmp(d + p, "fmt ", 4) == 0 && p + 24 <= sz) {
            *ch = d[p+10] | (d[p+11] << 8);
            *rate = d[p+12] | (d[p+13] << 8) | (d[p+14] << 16) | ((uint32_t)d[p+15] << 24);
            bits = d[p+22] | (d[p+23] << 8);
            got_fmt = true;
        } else if (memcmp(d + p, "data", 4) == 0 && got_fmt) {
            *off = p + 8;
            *len = csz;
            if (*off + *len > sz) *len = sz - *off;
            return bits == 16 && (*ch == 1 || *ch == 2) && *rate > 0;
        }
        p += 8 + csz + (csz & 1);
    }
    return false;
}

static bool player_cancelled(uint32_t gen) {
    return g_player_stop || gen != g_player_gen;
}

static void player_task(void *arg) {
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    uint8_t *data = nullptr;
    size_t sz = 0;
    g_player_stage = 1;
    snprintf(g_player_err, sizeof(g_player_err), "read");
    if (!load_file_mem(g_player_path, &data, &sz)) {
        snprintf(g_player_err, sizeof(g_player_err), "read failed");
        g_player_running = false; g_player_finished = true; vTaskDeleteWithCaps(NULL); return;
    }
    g_player_stage = 2;
    snprintf(g_player_err, sizeof(g_player_err), "parse");
    player_outn = 0;
    g_player_level = 0;
    uint32_t start_ms = g_player_pos_ms;
    const char *ext = strrchr(g_player_path, '.');
    if (!ext) ext = "";
    double phase = 0.0;

    if (strcasecmp(ext, ".wav") == 0) {
        uint32_t rate = 0, off = 0, len = 0;
        uint16_t ch = 0;
        if (wav_parse(data, sz, &rate, &ch, &off, &len)) {
            g_player_stage = 3;
            snprintf(g_player_err, sizeof(g_player_err), "wav decode");
            g_player_dur_ms = (uint32_t)((uint64_t)len * 1000 / (rate * ch * 2));
            uint64_t skip_frames = (uint64_t)start_ms * rate / 1000;
            uint64_t skip_bytes = skip_frames * ch * 2;
            if (skip_bytes >= len) skip_bytes = 0;
            off += (uint32_t)skip_bytes;
            uint32_t frames_left = (len - (uint32_t)skip_bytes) / (ch * 2);
            g_player_pos_ms = start_ms;
            uint64_t emitted = 0;
            while (!player_cancelled(gen) && frames_left > 0) {
                uint32_t nf = frames_left > 1024 ? 1024 : frames_left;
                player_emit((const int16_t *)(data + off), nf, ch, rate, &phase);
                off += nf * ch * 2;
                frames_left -= nf;
                emitted += nf;
                g_player_pos_ms = start_ms + (uint32_t)(emitted * 1000 / rate);
                g_player_level = g_player_level * 7 / 8;
                vTaskDelay(1);
            }
        }
    } else if (strcasecmp(ext, ".flac") == 0) {
        drflac *fl = drflac_open_memory(data, sz, &g_flac_cb);
        if (fl) {
            g_player_stage = 3;
            snprintf(g_player_err, sizeof(g_player_err), "flac decode");
            uint32_t rate = fl->sampleRate;
            uint32_t ch = fl->channels;
            uint64_t total = fl->totalPCMFrameCount;
            if (rate > 0) g_player_dur_ms = (uint32_t)(total * 1000 / rate);
            uint64_t start_frame = (uint64_t)start_ms * rate / 1000;
            if (start_frame > 0) drflac_seek_to_pcm_frame(fl, start_frame);
            static int16_t pcm[4096 * 2];
            uint64_t done = start_frame;
            while (!player_cancelled(gen)) {
                drflac_uint64 got = drflac_read_pcm_frames_s16(fl, 4096, pcm);
                if (got == 0) { snprintf(g_player_err, sizeof(g_player_err), "flac read 0"); break; }
                player_emit(pcm, (int)got, (int)ch, (int)rate, &phase);
                done += got;
                g_player_pos_ms = (uint32_t)(done * 1000 / rate);
                g_player_level = g_player_level * 7 / 8;
                vTaskDelay(1);
            }
            drflac_close(fl);
        } else {
            snprintf(g_player_err, sizeof(g_player_err), "flac open failed");
        }
    } else {
        size_t pos = 0;
        if (sz > 10 && memcmp(data, "ID3", 3) == 0) {
            pos = 10 + (((size_t)(data[6] & 0x7f) << 21) | ((size_t)(data[7] & 0x7f) << 14) |
                        ((size_t)(data[8] & 0x7f) << 7) | (size_t)(data[9] & 0x7f));
        }
        mp3dec_t dec;
        mp3dec_init(&dec);
        static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
        uint64_t decoded_ms = 0;
        bool started = false;
        while (pos + 4 < sz && !player_cancelled(gen)) {
            mp3dec_frame_info_t info = {};
            int samples = mp3dec_decode_frame(&dec, data + pos, sz - pos, pcm, &info);
            if (info.frame_bytes <= 0) break;
            pos += info.frame_bytes;
            if (samples <= 0 || info.hz <= 0) continue;
            uint32_t frame_ms = (uint32_t)((uint64_t)samples * 1000 / info.hz);
            if (!g_player_dur_ms && info.bitrate_kbps > 0)
                g_player_dur_ms = (uint32_t)((uint64_t)(sz - pos) * 8 * 1000 / (info.bitrate_kbps * 1000)) + (uint32_t)decoded_ms;
            if (!started) {
                if (decoded_ms + frame_ms <= start_ms) { decoded_ms += frame_ms; continue; }
                started = true;
                g_player_pos_ms = decoded_ms;
            }
            player_emit(pcm, samples, info.channels, info.hz, &phase);
            decoded_ms += frame_ms;
            g_player_pos_ms = decoded_ms;
            g_player_level = g_player_level * 7 / 8;
            if (start_ms == 0 && g_player_dur_ms == 0 && decoded_ms > 1000) { /* unknown */ }
            vTaskDelay(1);
        }
    }
    if (player_outn > 0) g_codec->CodecPort_PlayWrite(player_out, player_outn * 4);
    free(data);
    g_player_level = 0;
    if (gen == g_player_gen) {
        g_player_running = false;
        if (!g_player_stop) g_player_finished = true;
        g_player_stage = 99;
        if (strcmp(g_player_err, "flac read 0") != 0 && strcmp(g_player_err, "flac open failed") != 0)
            snprintf(g_player_err, sizeof(g_player_err), "done");
    }
    vTaskDeleteWithCaps(NULL);
}

static void player_stop(bool keep_position) {
    if (!g_player_running && !g_player_paused) return;
    ++g_player_gen;
    g_player_stop = true;
    g_player_running = false;
    g_player_paused = keep_position;
    g_player_finished = false;
}

static void player_start_at(uint32_t start_ms) {
    if (g_music_sel < 0 || g_music_sel >= g_music_count) return;
    player_stop(false);
    vTaskDelay(pdMS_TO_TICKS(60));
    uint32_t gen = ++g_player_gen;
    g_file_sel = g_music_files[g_music_sel];
    snprintf(g_player_path, sizeof(g_player_path), "/sdcard/%s", g_file_names[g_file_sel]);
    g_player_track = g_music_sel;
    g_player_pos_ms = start_ms;
    g_player_dur_ms = music_duration_ms(g_player_path);
    g_player_finished = false;
    g_player_paused = false;
    g_player_stop = false;
    g_player_running = true;
    player_apply_volume();
    BaseType_t ok = xTaskCreateWithCaps(player_task, "player", 32768, (void *)(uintptr_t)gen, 3, NULL,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) { g_player_running = false; g_player_paused = true; }
}

static void music_stop() {
    ++g_player_gen;
    g_player_stop = true;
    g_player_running = false;
    g_player_paused = false;
    g_player_finished = false;
    g_player_track = -1;
    g_track_finished = false;
}

static void music_enter() {
    ++g_player_gen;
    g_player_stop = true;
    g_player_running = false;
    if (g_wav_playing) g_wav_stop = true;
    if (g_mp3_playing) g_mp3_stop = true;
    music_scan();
    g_music_off = 0;
    g_player_track = -1;
    g_player_paused = false;
    g_player_finished = false;
    g_player_pos_ms = 0;
    g_player_dur_ms = 0;
    player_apply_volume();
    g_music_dirty = true;
    g_music_next_draw = 0;
    g_mode = MODE_MUSIC;
}

static void music_next() {
    if (g_music_count <= 0) return;
    g_music_sel = (g_player_track >= 0 ? g_player_track + 1 : g_music_sel) % g_music_count;
    player_start_at(0);
    g_music_dirty = true;
}

static void music_prev() {
    if (g_music_count <= 0) return;
    g_music_sel = (g_music_sel + g_music_count - 1) % g_music_count;
    player_start_at(0);
    g_music_dirty = true;
}

static void drawMusic() {
    ui_audio::View v;
    v.empty = (g_music_count == 0);
    v.track = g_music_sel + 1;
    v.total = g_music_count;
    v.volume = g_volume;
    v.posMs = g_player_pos_ms;
    v.durMs = g_player_dur_ms;
    v.level = g_player_level;
    v.repeatMode = g_music_repeat;
    v.running = g_player_running;
    if (!v.empty) {
        const char *fn = g_file_names[g_music_files[g_music_sel]];
        v.title = fn;
        v.format = music_format(fn);
    }
    if (g_player_running) v.state = g_player_stage < 3 ? "LOADING" : "PLAYING";
    else if (g_player_paused) v.state = "PAUSED";
    else if (g_player_finished) v.state = "FINISHED";
    else v.state = "STOP";
    ui_audio::draw(g, v);
}

static void handleMusicInput() {
    if (in.k4 && !prevIn.k4) { goBack(); prevIn = in; return; }

    // Repeat modes are handled on natural EOF.
    if (g_player_finished && !g_player_running) {
        g_player_finished = false;
        if (g_music_repeat == 2) { player_start_at(0); }
        else if (g_music_repeat == 1) { music_next(); }
    }
    if (g_music_count == 0) { prevIn = in; return; }

    // SW: play / pause / restart.
    if (in.sw && !prevIn.sw) {
        if (g_player_running) {
            uint32_t p = g_player_pos_ms;
            player_stop(true);
            g_player_pos_ms = p;
        } else if (g_player_paused) {
            g_player_paused = false;
            player_start_at(g_player_pos_ms);
        } else {
            player_start_at(0);
        }
    }

    // Joystick X: previous / next track.
    if (in.x != 0 && prevIn.x == 0) {
        if (in.x < 0) music_prev(); else music_next();
    }
    // Joystick Y: volume.
    if (in.up && !prevIn.up) { g_volume = g_volume <= 95 ? g_volume + 5 : 100; player_apply_volume(); }
    if (in.down && !prevIn.down) { g_volume = g_volume >= 5 ? g_volume - 5 : 0; player_apply_volume(); }

    // K1/K2: seek -/+5s. Do not act when both are held (C+D is voice recording).
    if (!(in.k1 && in.k2)) {
        int32_t delta = 0;
        if (in.k1 && !prevIn.k1) delta = -5000;
        if (in.k2 && !prevIn.k2) delta = 5000;
        if (delta) {
            int32_t target = (int32_t)g_player_pos_ms + delta;
            if (target < 0) target = 0;
            if (g_player_dur_ms && target > (int32_t)g_player_dur_ms) target = g_player_dur_ms;
            bool was = g_player_running;
            if (was) player_stop(true);
            g_player_pos_ms = (uint32_t)target;
            if (was) player_start_at(g_player_pos_ms);
            else g_player_paused = true;
        }
    }

    // K3: loop mode OFF -> ALL -> ONE.
    if (in.k3 && !prevIn.k3) g_music_repeat = (g_music_repeat + 1) % 3;

    if (in.x != prevIn.x || in.up != prevIn.up || in.down != prevIn.down ||
        in.k1 != prevIn.k1 || in.k2 != prevIn.k2 ||
        in.k3 != prevIn.k3 || in.k4 != prevIn.k4 || in.sw != prevIn.sw)
        g_music_dirty = true;
    prevIn = in;
}

static void drawImage() {
    drawStatus();
    if (!g_img_gray) return;
    u8g2_SetDrawColor(g, 1);
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 400; ++x) {
            if (g_img_gray[y * 400 + x] < 128) u8g2_DrawPixel(g, x, y);
        }
    }
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void drawHelp() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 32, 396, 218);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 8, 86, "摇杆左右：主目录翻页");
    u8g2_DrawUTF8(g, 8, 112, "摇杆上下：当前页选择入口");
    u8g2_DrawUTF8(g, 8, 138, "A：进入当前入口");
    u8g2_DrawUTF8(g, 8, 164, "摇杆按下 SW：返回上一级");
    u8g2_DrawUTF8(g, 8, 190, "物理实验内：左右/上下调参数");
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void drawFeedback() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 32, 396, 220);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 8, 56, g_fb_stage);
    draw_wrapped_utf8(g_fb_detail, 8, 76, 384, 16, 2);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawLine(g, 8, 108, 392, 108);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 8, 124, "识别:");
    if (g_fb_text[0]) draw_wrapped_utf8(g_fb_text, 42, 124, 350, 16, 4);
    else draw_wrapped_utf8("(等待语音)", 42, 124, 350, 16, 2);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 216, "DS:");
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    draw_wrapped_utf8(g_fb_ds[0] ? g_fb_ds : "-", 38, 216, 350, 13, 1);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 234, "SV:");
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    draw_wrapped_utf8(g_fb_sv[0] ? g_fb_sv : "-", 38, 234, 350, 13, 1);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 252, "WH:");
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    draw_wrapped_utf8(g_fb_wh[0] ? g_fb_wh : "-", 38, 252, 350, 13, 1);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 300, 276, g_fb_time);
}

static void drawMenu() {
    g_no_invert = false;
    g_dark_ui = false;
    ui::begin(g);
    ui::Status st;
    st.wifi = g_wifi_sta;
    st.sd = g_sd_ok;
    st.batteryPct = g_batt_pct;
    st.charging = g_charging;
    st.time = g_clock;
    ui::topBar("SYS // MAIN", st);

    ui::setInk(ui::Ink::Black);
    ui::label16(10, 46, "控制台");
    ui::mono(76, 45, "MAIN // MODULES");
    ui::mono(334, 45, "06");
    u8g2_DrawHLine(g, 8, 52, 384);
    for (int x = 12; x < 392; x += 20) u8g2_DrawLine(g, x, 49, x, 52);

    const char *labels[6] = { "闪卡记忆", "物理实验", "SD 卡", "音乐", "调查问卷", "设置" };
    const char *codes[6] = { "FLASH", "PHYSICS", "FILES", "AUDIO", "SURVEY", "CONFIG" };
    const char *nums[6] = { "01", "02", "03", "04", "05", "06" };
    const int xs[2] = { 10, 204 }, ys[3] = { 60, 128, 196 };
    for (int i = 0; i < 6; ++i) {
        int x = xs[i % 2], y = ys[i / 2];
        bool sel = (i == g_menu_sel);
        ui::card(x, y, 186, 60, sel);
        ui::mono(x + 10, y + 18, nums[i]);
        ui::label16(x + 42, y + 24, labels[i]);
        ui::mono(x + 10, y + 48, codes[i]);
        if (sel) ui::mono(x + 160, y + 48, ">");
        ui::setInk(ui::Ink::Black);
    }

    ui::bottomBar();
    ui::keycap(8, 279, "JOY", "MOVE");
    ui::keycap(116, 279, "K3", "OPEN");
    ui::keycap(214, 279, "K4", "HOLD BACK");
    ui::keycap(334, 279, "SW", "BACK");
}

static void handleMenuInput() {
    int row = g_menu_sel / 2, col = g_menu_sel % 2;
    if (in.down && !prevIn.down) row = (row + 1) % 3;
    if (in.up && !prevIn.up) row = (row + 2) % 3;
    if (in.x < 0 && prevIn.x >= 0) col ^= 1;
    if (in.x > 0 && prevIn.x <= 0) col ^= 1;
    g_menu_sel = row * 2 + col;

    if (in.k3 && !prevIn.k3) {
        switch (g_menu_sel) {
            case 0: g_word_cat_sel = 0; g_mode = MODE_WORD_MENU; break;
            case 1: g_phys_view = 0; g_phys_cat = 0; g_phys_sel = 0; g_mode = MODE_PHYSICS; break;
            case 2: files_enter(); break;
            case 3: music_enter(); break;
            case 4: g_mode = MODE_SURVEY_MENU; break;
            case 5: g_settings_sel = 0; g_mode = MODE_SETTINGS; break;
        }
    }
    prevIn = in;
}

static const char *SURVEY_Q[5] = {
    "最近一周，你的专注力如何？",
    "最近一周，你的睡眠质量如何？",
    "最近一周，你的情绪稳定程度？",
    "最近一周，记忆和思维清晰度？",
    "最近一周，白天的精力水平？"
};

static void survey_start() {
    g_survey_q = 0;
    for (int i = 0; i < 5; ++i) g_survey_ans[i] = 3;
    g_survey_done = false;
    g_mode = MODE_SURVEY;
}

static void survey_save() {
    if (!sd_ensure()) return;
    FILE *f = fopen("/sdcard/survey_prefrontal.csv", "a");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    bool empty = ftell(f) == 0;
    if (empty) fprintf(f, "time,q1,q2,q3,q4,q5\n");
    time_t now = time(nullptr);
    struct tm tmv = {};
    localtime_r(&now, &tmv);
    char stamp[32];
    if (tmv.tm_year >= 120) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    else snprintf(stamp, sizeof(stamp), "%s", g_clock);
    fprintf(f, "%s,%d,%d,%d,%d,%d\n", stamp, g_survey_ans[0], g_survey_ans[1],
            g_survey_ans[2], g_survey_ans[3], g_survey_ans[4]);
    fclose(f);
}

static int survey_total() {
    int t = 0;
    for (int i = 0; i < 5; ++i) t += g_survey_ans[i];
    return t;
}

static void survey_build_report() {
    static const char *names[5] = { "专注", "睡眠", "情绪", "认知", "精力" };
    int total = survey_total();
    int low = 5, low_i = 0;
    for (int i = 0; i < 5; ++i) if (g_survey_ans[i] < low) { low = g_survey_ans[i]; low_i = i; }
    const char *summary;
    if (total >= 20) summary = "整体状态较好，建议保持规律作息和运动。";
    else if (total >= 15) summary = "整体中等偏上，仍有提升空间，优先改善低分项。";
    else if (total >= 10) summary = "整体中等，建议先处理睡眠、精力和专注方面的短板。";
    else summary = "多项偏低，建议优先休息；若持续困扰，请咨询专业人士。";
    const char *tips[5] = {
        "专注：分段工作，减少同时多任务和手机干扰。",
        "睡眠：固定起床时间，睡前一小时减少屏幕。",
        "情绪：每天适量运动，和可信任的人交流。",
        "认知：阅读、复盘和笔记，给大脑留出休息。",
        "精力：规律吃饭、补水和短暂休息。"
    };
    snprintf(g_survey_report, sizeof(g_survey_report),
             "前额叶修复调查自评报告\n"
             "总分 %d/25   均分 %.1f\n"
             "专注%d 睡眠%d 情绪%d 认知%d 精力%d\n"
             "%s\n"
             "%s\n"
             "%s\n"
             "声明：自评结果，不构成医学诊断。",
             total, total / 5.0f,
             g_survey_ans[0], g_survey_ans[1], g_survey_ans[2], g_survey_ans[3], g_survey_ans[4],
             summary, tips[low_i], (low_i == 0 ? tips[1] : tips[0]));
    if (sd_ensure()) {
        FILE *f = fopen("/sdcard/survey_prefrontal_report.txt", "w");
        if (f) { fputs(g_survey_report, f); fputc('\n', f); fclose(f); }
    }
}

static void drawSurveyReport() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 32, 396, 220);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    int total = survey_total();
    char b[96];
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "TOTAL %d/25   AVG %.1f", total, total / 5.0f);
    u8g2_DrawStr(g, 8, 54, b);
    snprintf(b, sizeof(b), "FOCUS %d  SLEEP %d  MOOD %d  MIND %d  ENERGY %d",
             g_survey_ans[0], g_survey_ans[1], g_survey_ans[2], g_survey_ans[3], g_survey_ans[4]);
    u8g2_DrawStr(g, 8, 74, b);
    const char *summary;
    if (total >= 20) summary = "整体状态较好，建议保持规律作息和运动。";
    else if (total >= 15) summary = "整体中等偏上，仍有提升空间，优先改善低分项。";
    else if (total >= 10) summary = "整体中等，建议先处理睡眠、精力和专注方面的短板。";
    else summary = "多项偏低，建议优先休息；若持续困扰，请咨询专业人士。";
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    draw_wrapped_utf8(summary, 8, 104, 384, 18, 3);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 168, "SUGGESTIONS");
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    const char *tips = g_survey_report;
    for (int i = 0; i < 4 && tips; ++i) { const char *nl = strchr(tips, '\n'); if (nl) tips = nl + 1; }
    int yy = 190;
    while (tips && *tips && yy <= 258) {
        const char *nl = strchr(tips, '\n');
        int len = nl ? (int)(nl - tips) : (int)strlen(tips);
        char line[256];
        if (len > (int)sizeof(line) - 1) len = sizeof(line) - 1;
        memcpy(line, tips, len); line[len] = 0;
        u8g2_DrawUTF8(g, 8, yy, line);
        yy += 16;
        if (!nl) break;
        tips = nl + 1;
    }
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void handleSurveyReport() {
    if (in.k3 && !prevIn.k3) g_mode = MODE_MENU;
    prevIn = in;
}

static void drawSurveyMenu() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 32, 396, 214);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    ui::label16(20, 62, "调查问卷");
    ui::panel(16, 92, 368, 72);
    u8g2_DrawUTF8(g, 32, 122, "前额叶修复调查");
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 32, 146, "5 questions  |  self-rating 1-5");
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 16, 194, "自评问卷，不构成医学诊断。");
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void handleSurveyMenu() {
    if (in.k3 && !prevIn.k3) survey_start();
    if (in.k4 && !prevIn.k4) g_mode = MODE_MENU;
    prevIn = in;
}

static void drawSurvey() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 32, 396, 220);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    char b[48];
    snprintf(b, sizeof(b), "Q%d / 5", g_survey_q + 1);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 54, b);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    draw_wrapped_utf8(SURVEY_Q[g_survey_q], 12, 88, 376, 20, 3);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 150, "1 = poor / low       5 = good / high");
    for (int i = 0; i < 5; ++i) {
        int x = 40 + i * 64;
        u8g2_DrawFrame(g, x, 172, 44, 44);
        if (i + 1 == g_survey_ans[g_survey_q]) u8g2_DrawBox(g, x + 4, 176, 36, 36);
        u8g2_SetFont(g, u8g2_font_7x13_tf);
        u8g2_DrawStr(g, x + 18, 205, i == 0 ? "1" : (i == 4 ? "5" : ""));
    }
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    if (g_survey_done) {
        u8g2_DrawUTF8(g, 24, 250, "已完成，结果已保存到 SD 卡。");
    } else {
        u8g2_DrawUTF8(g, 16, 250, "左右选择，A 下一题");
        u8g2_DrawUTF8(g, 16, 274, "自评，不是医学诊断");
    }
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void handleSurvey() {
    if (g_survey_done) {
        if (in.k3 && !prevIn.k3) g_survey_done = false;
        prevIn = in;
        return;
    }
    if (in.x < 0 && prevIn.x >= 0) g_survey_ans[g_survey_q] = (g_survey_ans[g_survey_q] > 1) ? g_survey_ans[g_survey_q] - 1 : 1;
    if (in.x > 0 && prevIn.x <= 0) g_survey_ans[g_survey_q] = (g_survey_ans[g_survey_q] < 5) ? g_survey_ans[g_survey_q] + 1 : 5;
    if (in.k3 && !prevIn.k3) {
        if (g_survey_q < 4) ++g_survey_q;
        else { survey_save(); survey_build_report(); g_mode = MODE_SURVEY_REPORT; }
    }
    prevIn = in;
}

static int word_level_count(int level) {
    if (level == 0 || level == 1) return 100;
    return 10;
}

static void drawWordMenu() {
    // Global frame supplies the top/bottom bars.
    ui::setInk(ui::Ink::Black);
    ui::label16(10, 42, "闪卡记忆");
    ui::mono(110, 41, "FLASH // DECKS");
    ui::mono(340, 41, "04");
    u8g2_DrawHLine(g, 8, 48, 384);
    const char *levels[4] = { "KET", "PET", "MATH", "PHYS" };
    const char *subs[4] = { "100 句 · 英语基础", "100 句 · 英语进阶", "10 张 · 初中数学", "10 张 · 初中物理" };
    const int xs[2] = { 10, 204 }, ys[2] = { 58, 130 };
    for (int i = 0; i < 4; ++i) {
        int x = xs[i % 2], y = ys[i / 2];
        ui::card(x, y, 186, 64, i == g_word_cat_sel);
        char num[4];
        snprintf(num, sizeof(num), "%02d", i + 1);
        ui::mono(x + 10, y + 20, num);
        ui::label16(x + 50, y + 26, levels[i]);
        ui::label12(x + 10, y + 50, subs[i]);
        ui::setInk(ui::Ink::Black);
    }
}

static void handleWordMenu() {
    if (in.up && !prevIn.up) g_word_cat_sel = (g_word_cat_sel + 2) % 4;
    if (in.down && !prevIn.down) g_word_cat_sel = (g_word_cat_sel + 2) % 4;
    if (in.x < 0 && prevIn.x >= 0) g_word_cat_sel ^= 1;
    if (in.x > 0 && prevIn.x <= 0) g_word_cat_sel ^= 1;
    if (in.k3 && !prevIn.k3) {
        g_word_level = g_word_cat_sel;
        g_word_idx = 0;
        g_word_reveal = false;
        g_mode = MODE_WORD;
    }
    prevIn = in;
}

static const WordCard *word_card_at(int level, int idx) {
    const char *names[4] = { "KET", "PET", "MATH", "PHYS" };
    const char *want = names[level & 3];
    int n = 0;
    for (int i = 0; i < WORD_COUNT; ++i) {
        if (strcmp(WORD_CARDS[i].level, want) != 0) continue;
        if (n == idx) return &WORD_CARDS[i];
        ++n;
    }
    return &WORD_CARDS[0];
}

static void draw_wrapped_english(const char *s, int x, int y, int maxw, int lineh, int maxlines) {
    char line[512] = {0};
    int lines = 0;
    while (*s && lines < maxlines) {
        while (*s == ' ') ++s;
        if (!*s) break;
        const char *ws = s;
        while (*s && *s != ' ') ++s;
        int wl = s - ws;
        char word[96];
        if (wl >= (int)sizeof(word)) wl = sizeof(word) - 1;
        memcpy(word, ws, wl); word[wl] = 0;
        char cand[512] = {0};
        strncpy(cand, line, sizeof(cand) - 1);
        if (cand[0]) strncat(cand, " ", sizeof(cand) - strlen(cand) - 1);
        strncat(cand, word, sizeof(cand) - strlen(cand) - 1);
        if (line[0] && u8g2_GetStrWidth(g, cand) > maxw) {
            u8g2_DrawStr(g, x, y + lines * lineh, line);
            ++lines;
            strncpy(line, word, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
        } else {
            strncpy(line, cand, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
        }
    }
    if (line[0] && lines < maxlines) u8g2_DrawStr(g, x, y + lines * lineh, line);
}

static void drawWord() {
    const WordCard &w = *word_card_at(g_word_level, g_word_idx);
    bool stem = (strcmp(w.level, "MATH") == 0 || strcmp(w.level, "PHYS") == 0);
    ui::setInk(ui::Ink::Black);
    if (stem) {
        ui::label16(12, 54, w.en);
        u8g2_DrawHLine(g, 10, 64, 380);
        if (g_word_reveal) {
            ui::label12(12, 82, "答案");
            ui::label16(12, 108, w.zh);
            ui::label12(12, 138, "解释");
            u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
            draw_wrapped_utf8(w.sentence_zh, 12, 160, 376, 16, 5);
        } else {
            ui::label12(12, 84, "问题");
            u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
            draw_wrapped_utf8(w.sentence, 12, 110, 376, 20, 5);
            ui::label12(140, 236, "K3 显示答案");
        }
        char ib[40];
        snprintf(ib, sizeof(ib), "%s  %d / %d", w.level, g_word_idx + 1, word_level_count(g_word_level));
        ui::mono(12, 262, ib);
        return;
    }

    // Language cards: keep the word/sentence layout, shifted below the 24px frame.
    char upper[32];
    size_t n = strlen(w.en);
    if (n >= sizeof(upper)) n = sizeof(upper) - 1;
    for (size_t i = 0; i < n; ++i) upper[i] = (char)toupper((unsigned char)w.en[i]);
    upper[n] = 0;
    u8g2_SetFont(g, u8g2_font_10x20_tf);
    int ew = u8g2_GetStrWidth(g, upper);
    u8g2_DrawStr(g, (SCREEN_W - ew) / 2, 88, upper);
    if (g_word_reveal) {
        ui::label16(128, 116, w.zh);
    } else {
        ui::label12(140, 116, "K3 显示中文");
    }
    u8g2_DrawHLine(g, 10, 128, 380);
    ui::label12(8, 146, "例句");
    u8g2_SetFont(g, u8g2_font_10x20_tf);
    char sbuf[160];
    snprintf(sbuf, sizeof(sbuf), "%s", w.sentence);
    size_t sl = strlen(sbuf);
    while (sl > 0 && (sbuf[sl-1] == '.' || sbuf[sl-1] == ' ')) sbuf[--sl] = 0;
    draw_wrapped_english(sbuf, 8, 166, 384, 20, 3);
    if (g_word_reveal) {
        u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
        draw_wrapped_utf8(w.sentence_zh, 8, 234, 384, 16, 2);
    }
    char ib[40];
    snprintf(ib, sizeof(ib), "%s  %d / %d", w.level, g_word_idx + 1, word_level_count(g_word_level));
    ui::mono(8, 262, ib);
}

static void word_audio_toggle() {
    if (g_mp3_playing) { g_mp3_stop = true; return; }
    char path[64];
    snprintf(path, sizeof(path), "/sdcard/tts/%s_%03d.mp3", g_word_level ? "pet" : "ket", g_word_idx + 1);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(g_fb_stage, sizeof(g_fb_stage), "无音频");
        snprintf(g_fb_detail, sizeof(g_fb_detail), "缺少 %s，请先生成/上传 TTS。", path);
        return;
    }
    fclose(f);
    strncpy(g_audio_path, path, sizeof(g_audio_path) - 1);
    g_audio_path[sizeof(g_audio_path) - 1] = 0;
    mp3_start_playback();
}

static void handleWordInput() {
    if (in.k3 && !prevIn.k3) g_word_reveal = !g_word_reveal;
    if (in.k4 && !prevIn.k4 && g_word_level < 2) word_audio_toggle();
    if (in.up && !prevIn.up) { g_word_level = (g_word_level + 3) % 4; g_word_idx = 0; g_word_reveal = false; }
    if (in.down && !prevIn.down) { g_word_level = (g_word_level + 1) % 4; g_word_idx = 0; g_word_reveal = false; }
    if ((in.x < 0 && prevIn.x >= 0) || (in.x > 0 && prevIn.x <= 0)) {
        int cnt = word_level_count(g_word_level);
        g_word_idx = (g_word_idx + (in.x > 0 ? 1 : cnt - 1)) % cnt;
        g_word_reveal = false;
    }
    prevIn = in;
}

static void settings_save() {
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "vol", g_volume);
        nvs_set_u8(h, "contrast", g_contrast);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void settings_load() {
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "vol", &v) == ESP_OK) g_volume = v;
        if (nvs_get_u8(h, "contrast", &v) == ESP_OK) g_contrast = v;
        nvs_close(h);
    }
}

static void settings_apply() {
    if (g_codec) g_codec->CodecPort_SetSpeakerVol(g_volume);
    if (g) u8g2_SetContrast(g, g_contrast);
}

static void drawFader(int x, int y, int w, int value, int maxv, bool selected) {
    u8g2_DrawFrame(g, x, y, w, 14);
    int blocks = 16;
    int fill = (value * blocks + maxv / 2) / maxv;
    for (int i = 0; i < blocks; ++i) {
        int bx = x + 2 + i * (w - 4) / blocks;
        if (i < fill) u8g2_DrawBox(g, bx, y + 2, (w - 4) / blocks - 1, 10);
        else u8g2_DrawFrame(g, bx, y + 2, (w - 4) / blocks - 1, 10);
    }
    if (selected) u8g2_DrawFrame(g, x - 3, y - 3, w + 6, 20);
}

static void wifi_scan_now();
static void files_enter();
static void survey_start();
static void survey_save();
static void survey_build_report();
static void music_enter();
static void music_stop();
static void files_preview();
static void wav_start_playback();
static void mp3_start_playback();
static void image_view_file();
static int utf8_len(unsigned char c);
static void goBack();
static void wifi_enter();

static void drawSettingsMenu() {
    const char *labels[6] = { "声音", "Wi-Fi", "系统信息", "使用帮助", "语音记录", "命令反馈" };
    const char *subs[6] = {
        "音量 / 亮度", "扫描 / 密码", "3 页信息",
        "摇杆 / 按键", "录音记录", "执行反馈"
    };
    const int xs[2] = { 10, 204 }, ys[3] = { 42, 108, 174 };
    for (int i = 0; i < 6; ++i) {
        int x = xs[i % 2], y = ys[i / 2];
        ui::card(x, y, 186, 58, i == g_settings_sel);
        char num[4]; snprintf(num, sizeof(num), "%02d", i + 1);
        ui::mono(x + 10, y + 18, num);
        ui::label16(x + 12, y + 42, labels[i]);
        ui::label12(x + 100, y + 42, subs[i]);
        ui::setInk(ui::Ink::Black);
    }
    (void)subs;
}

static void handleSettingsMenu() {
    if (in.up && !prevIn.up) { g_settings_sel = (g_settings_sel >= 2) ? g_settings_sel - 2 : g_settings_sel; }
    if (in.down && !prevIn.down) { if (g_settings_sel + 2 < 6) g_settings_sel += 2; }
    if (in.x < 0 && prevIn.x >= 0) { if ((g_settings_sel & 1) && g_settings_sel > 0) --g_settings_sel; }
    if (in.x > 0 && prevIn.x <= 0) { if (!(g_settings_sel & 1) && g_settings_sel + 1 < 6) ++g_settings_sel; }
    if (in.k3 && !prevIn.k3) {
        switch (g_settings_sel) {
            case 0: g_set_sel = 0; g_mode = MODE_SETTINGS_CTRL; break;
            case 1: wifi_enter(); break;
            case 2: g_mode = MODE_INFO; break;
            case 3: g_mode = MODE_HELP; break;
            case 4: notes_enter(); break;
            case 5: g_mode = MODE_FEEDBACK; break;
        }
    }
    if (in.k4 && !prevIn.k4) g_mode = MODE_MENU;
    prevIn = in;
}

static void drawSettingsCtrl() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(8, 32, 384, 132);
    ui::panel(8, 168, 384, 90);
    ui::setInk(ui::Ink::Black);
    char b[32];
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 24, 82, "音量");
    drawFader(100, 70, 260, g_volume, 100, g_set_sel == 0);
    snprintf(b, sizeof(b), "%d%%", g_volume);
    u8g2_SetFont(g, u8g2_font_7x13_tf); u8g2_DrawStr(g, 366, 82, b);

    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 24, 136, "亮度");
    drawFader(100, 124, 260, g_contrast, 255, g_set_sel == 1);
    snprintf(b, sizeof(b), "%d", g_contrast);
    u8g2_SetFont(g, u8g2_font_7x13_tf); u8g2_DrawStr(g, 366, 136, b);

    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 16, 190, "音量：真实 ES8311 输出");
    u8g2_DrawUTF8(g, 16, 210, "亮度：ST7305 电子对比度");
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void handleSettingsCtrl() {
    bool changed = false;
    if (in.up && !prevIn.up) g_set_sel = 0;
    if (in.down && !prevIn.down) g_set_sel = 1;
    if (g_set_sel == 0) {
        if (in.x < 0 && prevIn.x >= 0) { g_volume = (g_volume >= 5) ? g_volume - 5 : 0; changed = true; }
        if (in.x > 0 && prevIn.x <= 0) { g_volume = (g_volume <= 95) ? g_volume + 5 : 100; changed = true; }
    } else {
        if (in.x < 0 && prevIn.x >= 0) { g_contrast = (g_contrast >= 8) ? g_contrast - 8 : 0; changed = true; }
        if (in.x > 0 && prevIn.x <= 0) { g_contrast = (g_contrast <= 247) ? g_contrast + 8 : 255; changed = true; }
    }
    if (in.k3 && !prevIn.k3) playSfx(1);
    if (in.k4 && !prevIn.k4) { g_volume = 100; g_contrast = 128; changed = true; }
    if (changed) {
        settings_apply();
        settings_save();
    }
    prevIn = in;
}

static const char *WIFI_LOWER = "abcdefghijklmnopqrstuvwxyz0123456789";
static const char *WIFI_UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()-_=+";

static void wifi_scan_now() {
    g_wifi_count = 0;
    wifi_scan_config_t sc = {};
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        wifi_ap_record_t tmp[16];
        uint16_t n = 16;
        if (esp_wifi_scan_get_ap_records(&n, tmp) == ESP_OK) {
            for (int i = 0; i < n && g_wifi_count < 8; ++i) {
                if (!tmp[i].ssid[0]) continue;
                bool dup = false;
                for (int j = 0; j < g_wifi_count; ++j) {
                    if (strcmp((const char *)tmp[i].ssid, (const char *)g_wifi_aps[j].ssid) == 0) {
                        dup = true;
                        if (tmp[i].rssi > g_wifi_aps[j].rssi) g_wifi_aps[j] = tmp[i];
                        break;
                    }
                }
                if (!dup) g_wifi_aps[g_wifi_count++] = tmp[i];
            }
        }
    }
    if (g_wifi_sel >= g_wifi_count) g_wifi_sel = 0;
}

static void wifi_enter() {
    g_wifi_ssid[0] = 0;
    g_wifi_pass[0] = 0;
    g_wifi_view = 0;
    g_wifi_sel = 0;
    g_key_page = 0;
    g_key_sel = 0;
    g_mode = MODE_WIFI;
    wifi_scan_now();
}

static void wifi_connect_saved() {
    wifi_config_t cfg = {};
    strncpy((char *)cfg.sta.ssid, g_wifi_ssid, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, g_wifi_pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_creds_save(g_wifi_ssid, g_wifi_pass);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_wifi_connect();
    g_wifi_view = 2;
}

static void drawWifi() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 32, 396, 220);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);

    if (g_wifi_view == 0) {
        char line[64];
        u8g2_SetFont(g, u8g2_font_7x13_tf);
        snprintf(line, sizeof(line), "SCAN %d AP", g_wifi_count);
        u8g2_DrawStr(g, 16, 52, line);
        for (int i = 0; i < g_wifi_count && i < 6; ++i) {
            int y = 72 + i * 25;
            u8g2_SetFont(g, u8g2_font_7x13_tf);
            char ssid[33];
            strncpy(ssid, (const char *)g_wifi_aps[i].ssid, 32); ssid[32] = 0;
            snprintf(line, sizeof(line), "%.24s %d", ssid, g_wifi_aps[i].rssi);
            u8g2_DrawStr(g, 22, y, line);
            if (i == g_wifi_sel) u8g2_DrawFrame(g, 12, y - 13, 376, 19);
        }
        u8g2_SetFont(g, u8g2_font_7x13_tf);
        return;
    }

    if (g_wifi_view == 1) {
        u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(g, 12, 54, "网络:");
        u8g2_DrawUTF8(g, 12, 76, g_wifi_ssid[0] ? g_wifi_ssid : "(未选择)");
        char mask[70]; int n = strlen(g_wifi_pass);
        for (int i = 0; i < n && i < 64; ++i) mask[i] = '*';
        mask[n < 64 ? n : 64] = 0;
        u8g2_DrawUTF8(g, 12, 98, "密码:");
        u8g2_SetFont(g, u8g2_font_7x13_tf);
        u8g2_DrawStr(g, 58, 98, mask);
        const char *keys = (g_key_page == 0) ? WIFI_LOWER : WIFI_UPPER;
        int cols = 6, cellw = 60, cellh = 19;
        for (int i = 0; keys[i]; ++i) {
            int cx = 20 + (i % cols) * cellw;
            int cy = 112 + (i / cols) * cellh;
            char k[2] = {keys[i], 0};
            u8g2_DrawStr(g, cx + 20, cy + 13, k);
            if (i == g_key_sel) u8g2_DrawFrame(g, cx, cy, cellw - 4, cellh - 1);
        }
        u8g2_SetFont(g, u8g2_font_7x13_tf);
        u8g2_DrawStr(g, 8, 270, "A TYPE  B DEL  C OK  D CASE");
        return;
    }

    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 20, 82, g_wifi_ssid);
    u8g2_DrawUTF8(g, 20, 110, "密码已保存，正在连接...");
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void handleWifiInput() {
    if (g_wifi_view == 0) {
        if (!g_wifi_count) {
            if (in.k3 && !prevIn.k3) wifi_scan_now();
        } else {
            if (in.up && !prevIn.up) g_wifi_sel = (g_wifi_sel + g_wifi_count - 1) % g_wifi_count;
            if (in.down && !prevIn.down) g_wifi_sel = (g_wifi_sel + 1) % g_wifi_count;
            if (in.k4 && !prevIn.k4) wifi_scan_now();
            if (in.k3 && !prevIn.k3) {
                strncpy(g_wifi_ssid, (const char *)g_wifi_aps[g_wifi_sel].ssid, sizeof(g_wifi_ssid) - 1);
                g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = 0;
                g_wifi_pass[0] = 0;
                g_key_page = 0; g_key_sel = 0;
                g_wifi_view = 1;
            }
        }
    } else if (g_wifi_view == 1) {
        const char *keys = (g_key_page == 0) ? WIFI_LOWER : WIFI_UPPER;
        int count = strlen(keys);
        int cols = 6;
        if (in.x < 0 && prevIn.x >= 0) g_key_sel = (g_key_sel + count - 1) % count;
        if (in.x > 0 && prevIn.x <= 0) g_key_sel = (g_key_sel + 1) % count;
        if (in.up && !prevIn.up) g_key_sel = (g_key_sel + count - cols) % count;
        if (in.down && !prevIn.down) g_key_sel = (g_key_sel + cols) % count;
        if (in.k2 && !prevIn.k2) { g_key_page = !g_key_page; g_key_sel = 0; }
        if (in.k3 && !prevIn.k3) {
            int n = strlen(g_wifi_pass);
            if (n < 64) { g_wifi_pass[n] = keys[g_key_sel]; g_wifi_pass[n+1] = 0; }
        }
        if (in.k4 && !prevIn.k4) {
            int n = strlen(g_wifi_pass);
            if (n > 0) g_wifi_pass[n-1] = 0;
        }
        if (in.k1 && !prevIn.k1) {
            if (g_wifi_ssid[0]) wifi_connect_saved();
        }
    }
    prevIn = in;
}

static void __attribute__((unused)) notes_enter() {
    if (g_notes_buf) { heap_caps_free(g_notes_buf); g_notes_buf = nullptr; }
    g_notes_scroll = 0;
    g_notes_buf = (char *)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    if (!g_notes_buf) return;
    g_notes_buf[0] = 0;
    if (!sd_ensure()) {
        snprintf(g_notes_buf, 32768, "SD 卡未挂载");
        return;
    }
    FILE *f = fopen("/sdcard/voice_notes.txt", "r");
    if (!f) {
        snprintf(g_notes_buf, 32768, "还没有录音文字\n先到 4.语音记录 按 A 录一段");
        return;
    }
    size_t n = fread(g_notes_buf, 1, 32767, f);
    fclose(f);
    g_notes_buf[n] = 0;
    if (n == 0) snprintf(g_notes_buf, 32768, "录音文字为空");
}

static void __attribute__((unused)) drawNotes() {
    drawStatus();
    ui::setInk(ui::Ink::Black);
    ui::panel(2, 32, 396, 220);
    ui::setInk(ui::Ink::Black);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    draw_wrapped_utf8(g_notes_buf ? g_notes_buf : "无内容", 12, 58 - g_notes_scroll * 16, 376, 16, 40);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void __attribute__((unused)) handleNotesInput() {
    if (in.down && !prevIn.down) g_notes_scroll++;
    if (in.up && !prevIn.up && g_notes_scroll > 0) g_notes_scroll--;
    prevIn = in;
}


static void wav_u32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static void wav_header_bytes(uint8_t *h, uint32_t data) {
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + data;
    h[4]=v; h[5]=v>>8; h[6]=v>>16; h[7]=v>>24;
    memcpy(h+8, "WAVEfmt ", 8);
    h[16]=16; h[17]=0; h[18]=0; h[19]=0;
    h[20]=1; h[21]=0; h[22]=2; h[23]=0; // codec capture is stereo interleaved
    h[24]=0x80; h[25]=0x3e; h[26]=0; h[27]=0; // 16000
    v = 64000; h[28]=v; h[29]=v>>8; h[30]=v>>16; h[31]=v>>24;
    h[32]=4; h[33]=0; h[34]=16; h[35]=0;
    memcpy(h+36, "data", 4);
    h[40]=data; h[41]=data>>8; h[42]=data>>16; h[43]=data>>24;
}

static void wav_patch(FILE *f, uint32_t data) {
    uint8_t h[44];
    wav_header_bytes(h, data);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fseek(f, 0, SEEK_END);
}

static void finalize_temp(FILE *f, const char *path, uint32_t data) {
    if (!f) return;
    wav_patch(f, data);
    fclose(f);
    if (g_cap_q) {
        if (xQueueSend(g_cap_q, path, 0) != pdTRUE) {
            g_cap_drop = g_cap_drop + 1;
            // Leave the file on SD if the upload queue is full.
        }
    }
}

static void cleanup_temp_files() {
    DIR *d = opendir("/sdcard");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != nullptr) {
        if (strncmp(de->d_name, "vtmp_", 5) == 0) {
            char p[300];
            snprintf(p, sizeof(p), "/sdcard/%s", de->d_name);
            unlink(p);
        }
    }
    closedir(d);
}

static void __attribute__((unused)) capture_task(void *) {
    static int16_t raw[640]; // 320 frames x 2ch
    static int16_t preroll[15][320];
    int pre_i = 0, pre_n = 0;
    int noise_peak = 200;
    int speech_run = 0, silence_run = 0;
    bool active = false;
    FILE *wf = nullptr;
    char wf_path[CAP_PATH_LEN];
    uint32_t wf_data = 0;
    int64_t last_patch = 0;
    uint32_t seq = 0;

    for (;;) {
        if (!g_audio_ready || g_capture_pause || !g_codec) {
            if (wf) {
                finalize_temp(wf, wf_path, wf_data);
                wf = nullptr; active = false; g_vad_active = false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        int st = g_codec->CodecPort_EchoRead(raw, sizeof(raw));
        if (st != 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        int peak = 0;
        for (int i = 0; i < 640; ++i) {
            int v = raw[i] < 0 ? -raw[i] : raw[i];
            if (v > peak) peak = v;
        }
        int db = (peak > 0) ? (int)(20.0f * log10f((float)peak / 32768.0f)) : -90;
        int level = (db + 60) * 100 / 60;
        if (level < 0) level = 0;
        if (level > 100) level = 100;
        g_mic_level = (g_mic_level * 3 + level) / 4;

        int16_t pcm[320];
        for (int i = 0; i < 320; ++i) {
            pcm[i] = (int16_t)(((int)raw[i * 2] + (int)raw[i * 2 + 1]) / 2);
        }

        bool voice = (peak > 1800 && peak > noise_peak * 3);
        if (!active) {
            memcpy(preroll[pre_i], pcm, sizeof(pcm));
            pre_i = (pre_i + 1) % 15;
            if (pre_n < 15) pre_n++;
            if (voice) speech_run++; else speech_run = 0;
            if (!voice) {
                noise_peak = (noise_peak * 98 + peak * 2) / 100;
                if (noise_peak < 100) noise_peak = 100;
                if (noise_peak > 3000) noise_peak = 3000;
            }
            if (speech_run < 2) continue;

            if (!g_sd_ok) {
                g_cap_drop = g_cap_drop + 1;
                speech_run = 0;
                continue;
            }
            snprintf(wf_path, sizeof(wf_path), "/sdcard/vtmp_%lu.wav", (unsigned long)seq++);
            wf = fopen(wf_path, "wb");
            if (!wf) {
                g_cap_drop = g_cap_drop + 1;
                speech_run = 0;
                continue;
            }
            uint8_t h[44];
            wav_header_bytes(h, 0);
            fwrite(h, 1, 44, wf);
            int start_i = (pre_i - pre_n + 15) % 15;
            for (int k = 0; k < pre_n; ++k) {
                fwrite(preroll[(start_i + k) % 15], 1, 640, wf);
                wf_data += 640;
            }
            last_patch = esp_timer_get_time();
            active = true;
            g_vad_active = true;
            g_vad_segments = g_vad_segments + 1;
            speech_run = 0;
            silence_run = 0;
            continue;
        }

        fwrite(pcm, 1, 640, wf);
        wf_data += 640;
        int64_t now = esp_timer_get_time();
        if (now - last_patch > 1000000) {
            wav_patch(wf, wf_data);
            last_patch = now;
        }
        if (voice) silence_run = 0; else silence_run++;
        if (silence_run >= 25) {
            finalize_temp(wf, wf_path, wf_data);
            wf = nullptr;
            active = false;
            g_vad_active = false;
            pre_n = 0; pre_i = 0;
            speech_run = 0; silence_run = 0;
        }
    }
}

static int send_all(int fd, const uint8_t *p, int n) {
    while (n > 0) {
        int r = send(fd, p, n, 0);
        if (r <= 0) return -1;
        p += r; n -= r;
        g_cap_sent = g_cap_sent + (uint32_t)r;
    }
    return 0;
}

static int send_msg(int fd, uint8_t type, const uint8_t *data, int data_len) {
    uint32_t len = 1 + data_len;
    uint8_t hdr[5] = {
        (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24), type
    };
    if (send_all(fd, hdr, 5) != 0) return -1;
    if (data_len && send_all(fd, data, data_len) != 0) return -1;
    return 0;
}

static int recv_exact(int fd, uint8_t *buf, int n) {
    while (n > 0) {
        int r = recv(fd, buf, n, 0);
        if (r <= 0) return -1;
        buf += r; n -= r;
    }
    return 0;
}

static void capture_net_task(void *) {
    char path[CAP_PATH_LEN];
    int sock = -1;
    for (;;) {
        if (xQueueReceive(g_cap_q, path, pdMS_TO_TICKS(500)) != pdTRUE) continue;
        FILE *f = fopen(path, "rb");
        if (!f) { unlink(path); continue; }
        uint8_t pcm[640];
        for (;;) {
            if (!g_wifi_sta) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
            if (sock < 0) {
                sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock >= 0) {
                    struct timeval tv = {5, 0};
                    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    sockaddr_in addr = {};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(CAPTURE_SERVER_PORT);
                    inet_pton(AF_INET, CAPTURE_SERVER_IP, &addr.sin_addr);
                    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) != 0) {
                        close(sock); sock = -1;
                    }
                }
                g_cap_connected = (sock >= 0);
                if (sock < 0) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
            }
            fseek(f, 44, SEEK_SET);
            if (send_msg(sock, CAP_START, nullptr, 0) != 0) goto retry;
            while (true) {
                size_t n = fread(pcm, 1, sizeof(pcm), f);
                if (n == 0) break;
                if (send_msg(sock, CAP_DATA, pcm, (int)n) != 0) goto retry;
            }
            if (send_msg(sock, CAP_END, nullptr, 0) != 0) goto retry;
            {
                uint8_t ack[5];
                if (recv_exact(sock, ack, 5) == 0 && ack[4] == 4) {
                    fclose(f);
                    unlink(path);
                    break;
                }
            }
        retry:
            if (sock >= 0) { close(sock); sock = -1; g_cap_connected = false; }
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
}

static void wav_u16(uint8_t *p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

static int utf8_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static void draw_wrapped_utf8(const char *s, int x, int y, int maxw, int lineh, int maxlines) {
    int line = 0;
    while (*s && line < maxlines) {
        const char *p = s;
        const char *last = s;
        char tmp[256];
        int tlen = 0;
        while (*p) {
            int clen = utf8_len((unsigned char)*p);
            if (clen <= 0 || tlen + clen >= (int)sizeof(tmp)) break;
            memcpy(tmp + tlen, p, clen);
            tlen += clen;
            tmp[tlen] = 0;
            int w = u8g2_GetUTF8Width(g, tmp);
            if (w > maxw && tlen > clen) {
                tlen -= clen;
                tmp[tlen] = 0;
                break;
            }
            p += clen;
            last = p;
        }
        if (tlen == 0) break;
        u8g2_DrawUTF8(g, x, y, tmp);
        y += lineh;
        ++line;
        s = last;
    }
}

static void draw_mic_level(int y) {
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 12, y, "MIC");
    u8g2_DrawFrame(g, 50, y - 10, 310, 11);
    int w = (g_mic_level * 306) / 100;
    if (w > 0) u8g2_DrawBox(g, 52, y - 8, w, 7);
}

static void drawVoice() {
    drawStatus();

    if (g_v_text[0] || g_v_reply[0]) {
        u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
        char head[64];
        snprintf(head, sizeof(head), "%s   %d/2", g_v_speaker[0] ? g_v_speaker : "未知", g_voice_page + 1);
        u8g2_DrawUTF8(g, 16, 38, head);
        if (g_voice_page == 0) {
            u8g2_DrawUTF8(g, 16, 62, "转写:");
            draw_wrapped_utf8(g_v_text[0] ? g_v_text : "(没有听清)", 16, 82, 368, 17, 7);
        } else {
            u8g2_DrawUTF8(g, 16, 62, "Qwen 建议:");
            draw_wrapped_utf8(g_v_reply[0] ? g_v_reply : "(暂无回复)", 16, 82, 368, 17, 8);
        }
        draw_mic_level(240);
        u8g2_SetFont(g, u8g2_font_7x13_tf);
        u8g2_DrawStr(g, 8, 276, "C+D HOLD: TALK 60s");
        return;
    }

    u8g2_SetFont(g, u8g2_font_9x15_tf);
    u8g2_DrawStr(g, 20, 95, "C+D HOLD: TALK 60s");
    u8g2_DrawStr(g, 20, 155, "PC 192.168.3.39:8765");
    draw_mic_level(190);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 20, 220, g_voice_msg);
    u8g2_DrawStr(g, 8, 270, "SPEAKER + TRANSCRIPT + QWEN");
}

static void drawCmdOverlay() {
    if (g_mode == MODE_FEEDBACK) return; // the feedback page already owns this area
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawBox(g, 0, 246, 400, 22);
    u8g2_SetDrawColor(g, 1);
    char b[80];
    if (g_cmd_recording) {
        int sec = (int)(g_cmd_elapsed_ms / 1000);
        if (sec > 60) sec = 60;
        snprintf(b, sizeof(b), "REC %02d/60  C+D RELEASE TO SEND", sec);
    } else {
        snprintf(b, sizeof(b), "%s", g_cmd_msg);
    }
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 8, 261, b);
}

static void cmd_upload_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(g_cmd_msg, sizeof(g_cmd_msg), "CMD FILE MISSING"); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 44) { fclose(f); snprintf(g_cmd_msg, sizeof(g_cmd_msg), "CMD EMPTY"); return; }
    snprintf(g_cmd_msg, sizeof(g_cmd_msg), "SENDING %ld B", sz);
    esp_http_client_config_t cfg = {};
    cfg.url = "http://192.168.3.39:8765/voicecmd";
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 180000;
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) { fclose(f); snprintf(g_cmd_msg, sizeof(g_cmd_msg), "HTTP INIT FAIL"); return; }
    esp_http_client_set_header(cli, "Content-Type", "audio/wav");
    esp_http_client_set_header(cli, "X-Device", "rlcd-voicecmd");
    if (esp_http_client_open(cli, sz) != ESP_OK) {
        fclose(f); esp_http_client_cleanup(cli);
        snprintf(g_cmd_msg, sizeof(g_cmd_msg), "HTTP OPEN FAIL"); return;
    }
    char buf[2048]; size_t sent = 0;
    while (sent < (size_t)sz) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        if (esp_http_client_write(cli, buf, n) <= 0) break;
        sent += n;
    }
    fclose(f);
    int code = 0;
    if (esp_http_client_fetch_headers(cli) >= 0) code = esp_http_client_get_status_code(cli);
    char *resp = (char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    int rlen = 0;
    if (resp) {
        memset(resp, 0, 4096);
        while (rlen < 4095) {
            int n = esp_http_client_read(cli, resp + rlen, 4095 - rlen);
            if (n <= 0) break;
            rlen += n;
        }
    }
    esp_http_client_close(cli); esp_http_client_cleanup(cli);
    if (code == 200) {
        snprintf(g_cmd_msg, sizeof(g_cmd_msg), "VOICE SENT -> DSH");
        // Do not overwrite an ASR/DSH stage that arrived while the upload was in flight.
        if (strcmp(g_fb_stage, "上传中") == 0) {
            snprintf(g_fb_stage, sizeof(g_fb_stage), "已发送");
            snprintf(g_fb_detail, sizeof(g_fb_detail), "电脑已收到，等待转写和 DSH 执行。");
        }
    } else {
        snprintf(g_cmd_msg, sizeof(g_cmd_msg), "SEND FAIL HTTP %d", code);
        snprintf(g_fb_stage, sizeof(g_fb_stage), "发送失败");
        snprintf(g_fb_detail, sizeof(g_fb_detail), "上传返回 HTTP %d，请检查电脑服务。", code);
    }
    heap_caps_free(resp);
}

static void cmd_record_task(void *) {
    CapturePauseGuard pause;
    const char *path = "/sdcard/vcmd.wav";
    FILE *f = fopen(path, "wb");
    if (!f) {
        g_cmd_recording = false; g_cmd_busy = false;
        snprintf(g_cmd_msg, sizeof(g_cmd_msg), "SD OPEN FAIL");
        vTaskDelete(NULL); return;
    }
    uint8_t hdr[44] = {0};
    wav_header_bytes(hdr, 0);
    fwrite(hdr, 1, 44, f);
    uint8_t buf[2560];
    uint32_t total = 0;
    int64_t t0 = esp_timer_get_time();
    while (!g_cmd_stop && (esp_timer_get_time() - t0) < 60000000LL) {
        int r = g_codec ? g_codec->CodecPort_EchoRead(buf, sizeof(buf)) : -1;
        if (r != 0) break;
        fwrite(buf, 1, sizeof(buf), f);
        total += sizeof(buf);
        g_cmd_elapsed_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    wav_patch(f, total);
    fclose(f);
    g_cmd_recording = false;
    g_cmd_elapsed_ms = (esp_timer_get_time() - t0) / 1000;
    snprintf(g_cmd_msg, sizeof(g_cmd_msg), "SENDING...");
    snprintf(g_fb_stage, sizeof(g_fb_stage), "上传中");
    snprintf(g_fb_detail, sizeof(g_fb_detail), "录音 %.1f 秒，正在发送到电脑。", g_cmd_elapsed_ms / 1000.0f);
    cmd_upload_file(path);
    g_cmd_busy = false;
    vTaskDelete(NULL);
}

static bool cmd_start() {
    if (g_cmd_busy) return false;
    g_cmd_busy = true;
    g_cmd_recording = true;
    g_cmd_stop = false;
    g_cmd_elapsed_ms = 0;
    g_mode = MODE_FEEDBACK;
    snprintf(g_fb_stage, sizeof(g_fb_stage), "录音中");
    snprintf(g_fb_detail, sizeof(g_fb_detail), "C+D 松开结束；说话后静音 2.5 秒自动收尾。");
    BaseType_t ok = xTaskCreate(cmd_record_task, "cmd_rec", 8192, nullptr, 3, nullptr);
    if (ok != pdPASS) {
        g_cmd_busy = false; g_cmd_recording = false;
        snprintf(g_cmd_msg, sizeof(g_cmd_msg), "TASK CREATE FAIL");
        return false;
    }
    return true;
}

static void voice_record_upload() {
    if (!g_codec) {
        snprintf(g_voice_msg, sizeof(g_voice_msg), "NO MIC / CODEC");
        g_voice_state = 4;
        return;
    }
    CapturePauseGuard capture_pause;
    const int sr = 16000;
    const int seconds = 6;
    const size_t data_bytes = (size_t)sr * seconds * 2 * 2; // raw 2ch
    uint8_t *wav = (uint8_t *)heap_caps_malloc(44 + data_bytes, MALLOC_CAP_SPIRAM);
    if (!wav) {
        snprintf(g_voice_msg, sizeof(g_voice_msg), "NO PSRAM BUFFER");
        g_voice_state = 4;
        return;
    }

    // WAV header, filled after recording.
    memcpy(wav, "RIFF", 4);
    wav_u32(wav + 4, 36 + data_bytes);
    memcpy(wav + 8, "WAVEfmt ", 8);
    wav_u32(wav + 16, 16);
    wav_u16(wav + 20, 1);
    wav_u16(wav + 22, 2);
    wav_u32(wav + 24, sr);
    wav_u32(wav + 28, sr * 4);
    wav_u16(wav + 32, 4);
    wav_u16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    wav_u32(wav + 40, data_bytes);

    // CodecPort_EchoRead returns ESP_CODEC_DEV_OK (0) on success, not a byte count.
    g_read_ret = g_codec->CodecPort_EchoRead(wav + 44, data_bytes);
    g_read_calls = 1;
    size_t got = (g_read_ret == 0) ? data_bytes : 0;
    g_voice_bytes = got;
    wav_u32(wav + 4, 36 + got);
    wav_u32(wav + 40, got);

    if (!g_ota_started) {
        g_ota_started = true;
        ota_start();
        vTaskDelay(pdMS_TO_TICKS(8000));
    }

    snprintf(g_voice_msg, sizeof(g_voice_msg), "UPLOADING %u B", (unsigned)got);
    g_voice_state = 2;
    u8g2_ClearBuffer(g);
    u8g2_SetDrawColor(g, 1);
    drawVoice();
    presentFrame();

    esp_http_client_config_t cfg = {};
    cfg.url = "http://192.168.3.39:8765/voice";
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 180000;
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        snprintf(g_voice_msg, sizeof(g_voice_msg), "HTTP INIT FAIL");
        g_voice_state = 4;
        heap_caps_free(wav);
        return;
    }
    esp_http_client_set_header(cli, "Content-Type", "audio/wav");
    esp_http_client_set_header(cli, "X-Device", "rlcd-stickman");
    size_t total = 44 + got;
    if (esp_http_client_open(cli, total) != ESP_OK) {
        snprintf(g_voice_msg, sizeof(g_voice_msg), "UPLOAD OPEN FAIL");
        g_voice_state = 4;
        esp_http_client_cleanup(cli);
        heap_caps_free(wav);
        return;
    }
    size_t sent = 0;
    while (sent < total) {
        int n = esp_http_client_write(cli, (char *)wav + sent, total - sent);
        if (n <= 0) break;
        sent += n;
    }
    int code = 0;
    if (esp_http_client_fetch_headers(cli) >= 0) {
        code = esp_http_client_get_status_code(cli);
    }
    char *resp = (char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    int rlen = 0;
    if (resp) {
        memset(resp, 0, 4096);
        while (rlen < 4095) {
            int n = esp_http_client_read(cli, resp + rlen, 4095 - rlen);
            if (n <= 0) break;
            rlen += n;
        }
    }
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    heap_caps_free(wav);

    if (code == 200 && sent == total) {
        g_voice_state = 3;
        g_voice_page = 0;
        if (resp && rlen > 0) {
            cJSON *root = cJSON_Parse(resp);
            if (root) {
                cJSON *spk = cJSON_GetObjectItem(root, "speaker");
                cJSON *txt = cJSON_GetObjectItem(root, "transcript");
                cJSON *rep = cJSON_GetObjectItem(root, "reply");
                if (cJSON_IsString(spk)) snprintf(g_v_speaker, sizeof(g_v_speaker), "%s", spk->valuestring);
                if (cJSON_IsString(txt)) snprintf(g_v_text, sizeof(g_v_text), "%s", txt->valuestring);
                if (cJSON_IsString(rep)) snprintf(g_v_reply, sizeof(g_v_reply), "%s", rep->valuestring);
                cJSON_Delete(root);
            }
        }
        save_voice_note_sd();
        snprintf(g_voice_msg, sizeof(g_voice_msg), "SENT OK - SEE SCREEN");
    } else {
        snprintf(g_voice_msg, sizeof(g_voice_msg), "SEND FAIL HTTP %d", code);
        g_voice_state = 4;
    }
    heap_caps_free(resp);
}

static void handleVoiceInput() {
    if (g_voice_state == 3) {
        if (in.rawVry < 1000 && prevIn.rawVry >= 1000) g_voice_page = 0;
        if (in.rawVry > 2800 && prevIn.rawVry <= 2800) g_voice_page = 1;
    }
    if (false && in.k3 && !prevIn.k3 && !in.k1 && g_voice_state != 1 && g_voice_state != 2) {
        g_v_text[0] = 0; g_v_reply[0] = 0; g_v_speaker[0] = 0; g_voice_page = 0;
        g_voice_state = 1;
        snprintf(g_voice_msg, sizeof(g_voice_msg), "RECORDING 6s...");
        u8g2_ClearBuffer(g);
        u8g2_SetDrawColor(g, 1);
        drawVoice();
        presentFrame();
        voice_record_upload();
        u8g2_ClearBuffer(g);
        u8g2_SetDrawColor(g, 1);
        drawVoice();
        presentFrame();
    }
    prevIn = in;
}


static void darkBegin() {
    u8g2_ClearBuffer(g);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawBox(g, 0, 0, SCREEN_W, SCREEN_H);
    u8g2_SetDrawColor(g, 0);
    g_dark_ui = true;
    g_no_invert = true;
}

static void darkHeader(const char *, const char *, int) {
    // Global UI frame owns the top bar and page path.
}

static void drawCard(int x, int y, int w, int h) {
    u8g2_DrawFrame(g, x, y, w, h);
}

static void drawArrow(int x0, int y0, int x1, int y1) {
    u8g2_DrawLine(g, x0, y0, x1, y1);
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len;
    int ax = x1 - (int)(ux * 5 - uy * 3);
    int ay = y1 - (int)(uy * 5 + ux * 3);
    int bx = x1 - (int)(ux * 5 + uy * 3);
    int by = y1 - (int)(uy * 5 - ux * 3);
    u8g2_DrawLine(g, x1, y1, ax, ay);
    u8g2_DrawLine(g, x1, y1, bx, by);
}

static void drawDashH(int x0, int x1, int y, int dash) {
    for (int x = x0; x < x1; x += dash * 2) u8g2_DrawHLine(g, x, y, dash);
}
static void drawDashV(int x, int y0, int y1, int dash) {
    for (int y = y0; y < y1; y += dash * 2) u8g2_DrawVLine(g, x, y, dash);
}

static void projectileResetMotion() {
    ph_t = 0.0f; ph_x = 0.0f; ph_y = ph_h;
    ph_flying = false; ph_landed = false; ph_paused = false; ph_tn = 0;
}

static void physicsReset() {
    projectileResetMotion();
}

static float projectile_tfall() {
    return sqrtf(fmaxf(0.05f, 2.0f * ph_h / ph_g));
}

static const char *PROJ_PARAM_NAME[8] = {
    "初速度 v0", "高度 h", "重力 g", "播放速度",
    "频闪点", "x 投影", "自由落体对比球", "速度矢量"
};

static void projectileParamText(int i, char *b, size_t n) {
    switch (i) {
        case 0: snprintf(b, n, "%.2f m/s", ph_v0); break;
        case 1: snprintf(b, n, "%.2f m", ph_h); break;
        case 2: snprintf(b, n, "%.2f m/s2", ph_g); break;
        case 3: snprintf(b, n, "%s", ph_speed == 0 ? "0.1x" : (ph_speed == 1 ? "0.25x" : "1x")); break;
        case 4: snprintf(b, n, "%s", (ph_overlay & 1) ? "ON" : "OFF"); break;
        case 5: snprintf(b, n, "%s", (ph_overlay & 2) ? "ON" : "OFF"); break;
        case 6: snprintf(b, n, "%s", (ph_overlay & 4) ? "ON" : "OFF"); break;
        default: snprintf(b, n, "%s", (ph_overlay & 8) ? "ON" : "OFF"); break;
    }
}

static void projectileInput(float dt) {
    if (in.up && !prevIn.up) g_wuli_page = (g_wuli_page + 2) % 3;
    if (in.down && !prevIn.down) g_wuli_page = (g_wuli_page + 1) % 3;
    if (in.k3 && !prevIn.k3) {
        if (ph_flying) ph_paused = !ph_paused;
        else {
            physicsReset();
            ph_flying = true; ph_paused = false;
            ph_t = 0; ph_x = 0; ph_y = ph_h;
        }
    }
    if (in.k4 && !prevIn.k4) physicsReset();

    if (g_wuli_page == 0) {
        if (in.k1 && !prevIn.k1) g_wuli_sel = (g_wuli_sel + 7) % 8;
        if (in.k2 && !prevIn.k2) g_wuli_sel = (g_wuli_sel + 1) % 8;
        if (g_wuli_sel <= 2 && in.x != 0) {
            if (g_wuli_sel == 0) ph_v0 = fmaxf(1.0f, fminf(6.0f, ph_v0 + (in.x > 0 ? 1 : -1) * dt * 1.2f));
            else if (g_wuli_sel == 1) ph_h = fmaxf(0.5f, fminf(5.0f, ph_h + (in.x > 0 ? 1 : -1) * dt * 0.8f));
            else ph_g = fmaxf(8.0f, fminf(20.0f, ph_g + (in.x > 0 ? 1 : -1) * dt * 1.5f));
            if (!ph_flying) ph_y = ph_h;
        } else if (g_wuli_sel == 3 && in.x != 0 && !prevIn.x) {
            ph_speed = (ph_speed + (in.x > 0 ? 1 : 2)) % 3;
        } else if (g_wuli_sel >= 4 && in.x != 0 && !prevIn.x) {
            ph_overlay ^= (uint8_t)(1u << (g_wuli_sel - 4));
        }
    } else {
        // Page 2 graph: C/D toggles y-t/x-t; left/right scrubs time.
        if ((in.k1 && !prevIn.k1) || (in.k2 && !prevIn.k2)) {
            ph_overlay ^= 0x10;
            ph_paused = true;
        }
        if (in.x != 0) {
            ph_paused = true;
            float tmax = projectile_tfall();
            ph_t = fmaxf(0.0f, fminf(tmax, ph_t + (in.x > 0 ? 1 : -1) * dt * 0.35f));
            ph_x = ph_v0 * ph_t;
            ph_y = ph_h - 0.5f * ph_g * ph_t * ph_t;
            ph_flying = true;
            if (ph_t >= tmax) { ph_landed = true; ph_flying = false; }
        }
    }
    prevIn = in;
}

static void projectileUpdate(float dt) {
    if (!ph_flying || ph_paused) return;
    float mult = ph_speed == 0 ? 0.1f : (ph_speed == 1 ? 0.25f : 1.0f);
    ph_t += dt * mult;
    ph_x = ph_v0 * ph_t;
    ph_y = ph_h - 0.5f * ph_g * ph_t * ph_t;
    if (ph_y <= 0.0f) {
        ph_y = 0; ph_flying = false; ph_landed = true;
    }
    if (ph_tn < 256) { ph_tx[ph_tn] = ph_x; ph_ty[ph_tn] = ph_y; ph_tn++; }
}

static void projectileReadouts() {
    float tmax = projectile_tfall();
    float range = ph_v0 * tmax;
    float vx = ph_v0;
    float vy = -ph_g * ph_t;
    float v = sqrtf(vx * vx + vy * vy);
    char b[96];
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "vx %5.2f   vy %6.2f   v %5.2f m/s", vx, vy, v);
    u8g2_DrawStr(g, 8, 212, b);
    snprintf(b, sizeof(b), "t %4.2fs  x %4.2fm  y %4.2fm", ph_t, ph_x, ph_y);
    u8g2_DrawStr(g, 8, 228, b);
    snprintf(b, sizeof(b), "tfall %4.2fs   R %4.2fm   g %4.2f", tmax, range, ph_g);
    u8g2_DrawStr(g, 8, 244, b);
}

static void projectileParamStrip() {
    char val[32], b[96];
    projectileParamText(g_wuli_sel, val, sizeof(val));
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    snprintf(b, sizeof(b), "> %s", PROJ_PARAM_NAME[g_wuli_sel]);
    u8g2_DrawUTF8(g, 8, 258, b);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 160, 258, val);
    snprintf(b, sizeof(b), "v0 %.1f  h %.1f  g %.1f  %s  STROBE %s",
             ph_v0, ph_h, ph_g,
             ph_speed == 0 ? "0.1x" : (ph_speed == 1 ? "0.25x" : "1x"),
             (ph_overlay & 1) ? "ON" : "OFF");
    u8g2_DrawStr(g, 8, 272, b);
}

static void drawProjectileScene() {
    drawCard(8, 44, 384, 156);
    const int L = 34, R = 380, T = 56, B = 186;
    float tmax = projectile_tfall();
    float range = ph_v0 * tmax;
    if (range < 0.2f) range = 0.2f;
    float sx = (R - L - 8) / range;
    float sy = (B - T - 8) / fmaxf(0.2f, ph_h);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    for (int i = 0; i <= 4; ++i) {
        int y = B - i * (B - T) / 4;
        drawDashH(L, R, y, 2);
        char q[16]; snprintf(q, sizeof(q), "%.1f", ph_h * i / 4.0f);
        u8g2_DrawStr(g, 2, y + 4, q);
    }
    for (int i = 0; i <= 4; ++i) {
        int x = L + i * (R - L) / 4;
        drawDashV(x, T, B, 2);
        char q[16]; snprintf(q, sizeof(q), "%.1f", range * i / 4.0f);
        u8g2_DrawStr(g, x - 6, B + 12, q);
    }
    u8g2_DrawLine(g, L, B, R, B);
    u8g2_DrawLine(g, L, T, L, B);
    u8g2_DrawStr(g, 8, 54, "y/m");
    u8g2_DrawStr(g, 344, B + 12, "x/m");
    u8g2_DrawStr(g, L - 8, B + 12, "O");

    // Launch platform at (L,T).
    u8g2_DrawLine(g, L, T, L + 24, T);
    u8g2_DrawLine(g, L, T, L, T + 9);
    u8g2_DrawLine(g, L + 21, T, L + 21, T + 9);

    // Ghost comparison / free-fall reference.
    if (ph_overlay & 4) {
        drawDashV(L + 12, T, B, 3);
        float y0 = T + (ph_h - ph_y) * sy;
        u8g2_DrawDisc(g, L + 12, (int)y0, 2, U8G2_DRAW_ALL);
    }

    // Theoretical trajectory, dashed.
    for (int k = 0; k < 120; ++k) {
        float t0 = tmax * k / 120.0f, t1 = tmax * (k + 1) / 120.0f;
        int x0 = L + (int)(ph_v0 * t0 * sx);
        int y0 = B - (int)((ph_h - 0.5f * ph_g * t0 * t0) * sy);
        int x1 = L + (int)(ph_v0 * t1 * sx);
        int y1 = B - (int)((ph_h - 0.5f * ph_g * t1 * t1) * sy);
        if (k % 3 != 0) u8g2_DrawLine(g, x0, y0, x1, y1);
    }

    // Strobe points.
    if (ph_overlay & 1) {
        for (float tt = 0; tt <= tmax; tt += 0.055f) {
            int px = L + (int)(ph_v0 * tt * sx);
            int py = B - (int)((ph_h - 0.5f * ph_g * tt * tt) * sy);
            u8g2_DrawDisc(g, px, py, 1, U8G2_DRAW_ALL);
        }
    }

    int bx = L + (int)(ph_x * sx);
    int by = B - (int)(ph_y * sy);
    if (bx < L) bx = L;
    if (bx > R) bx = R;
    if (by < T) by = T;
    if (by > B) by = B;

    if (ph_overlay & 2) drawDashV(bx, by, B, 3);
    if (ph_overlay & 8) {
        float vy = -ph_g * ph_t;
        drawArrow(bx, by, bx + (int)(ph_v0 * 5), by);
        drawArrow(bx, by, bx, by - (int)(-vy * 5));
        drawArrow(bx, by, bx + (int)(ph_v0 * 5), by - (int)(-vy * 5));
    }
    u8g2_DrawDisc(g, bx, by, 4, U8G2_DRAW_ALL);
    u8g2_DrawCircle(g, bx, by, 5, U8G2_DRAW_ALL);

    // Range marker.
    int rx = L + (int)(range * sx);
    u8g2_DrawLine(g, rx, B - 5, rx, B);
    u8g2_DrawLine(g, rx - 4, B, rx + 4, B);
    char b[32]; snprintf(b, sizeof(b), "R %.2fm", range);
    u8g2_DrawStr(g, L + 8, B - 22, b);
}

static void drawProjectileGraph() {
    drawCard(8, 44, 384, 156);
    const int L = 54, R = 378, T = 60, B = 184;
    float tmax = projectile_tfall();
    float range = ph_v0 * tmax;
    bool yt = (ph_overlay & 0x10) == 0; // bit4 selects x-t
    float ymax = yt ? fmaxf(0.2f, ph_h) : fmaxf(0.2f, range);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 12, 56, yt ? "y-t" : "x-t");
    u8g2_DrawStr(g, R - 30, 56, yt ? "y/m" : "x/m");
    u8g2_DrawStr(g, L + 2, T - 4, "t/s");
    for (int i = 0; i <= 4; ++i) {
        int y = B - i * (B - T) / 4;
        drawDashH(L, R, y, 2);
        char q[16]; snprintf(q, sizeof(q), "%.1f", ymax * i / 4.0f);
        u8g2_DrawStr(g, 4, y + 4, q);
    }
    for (int i = 0; i <= 4; ++i) {
        int x = L + i * (R - L) / 4;
        drawDashV(x, T, B, 2);
        char q[16]; snprintf(q, sizeof(q), "%.2f", tmax * i / 4.0f);
        u8g2_DrawStr(g, x - 8, B + 12, q);
    }
    u8g2_DrawLine(g, L, B, R, B); u8g2_DrawLine(g, L, T, L, B);
    for (int k = 0; k < 120; ++k) {
        float t0 = tmax * k / 120.0f, t1 = tmax * (k + 1) / 120.0f;
        float v0 = yt ? ph_h - 0.5f * ph_g * t0 * t0 : ph_v0 * t0;
        float v1 = yt ? ph_h - 0.5f * ph_g * t1 * t1 : ph_v0 * t1;
        int x0 = L + (int)(t0 / tmax * (R - L));
        int y0 = B - (int)(v0 / ymax * (B - T));
        int x1 = L + (int)(t1 / tmax * (R - L));
        int y1 = B - (int)(v1 / ymax * (B - T));
        if (k % 3 != 0) u8g2_DrawLine(g, x0, y0, x1, y1);
    }
    int cx = L + (int)(ph_t / tmax * (R - L));
    int cy = B - (int)((yt ? ph_y : ph_x) / ymax * (B - T));
    if (cx < L) cx = L;
    if (cx > R) cx = R;
    if (cy < T) cy = T;
    if (cy > B) cy = B;
    drawDashV(cx, T, B, 3);
    u8g2_DrawDisc(g, cx, cy, 3, U8G2_DRAW_ALL);
}

static void drawProjectile() {
    if (!ph_flying && !ph_landed) ph_y = ph_h;
    darkBegin();
    darkHeader("曲线运动与万有引力 / 实验 2", "平抛运动", g_wuli_page);
    if (g_wuli_page == 0) drawProjectileScene();
    else if (g_wuli_page == 1) drawProjectileGraph();
    else drawProjectileFormula();
    projectileReadouts();
    projectileParamStrip();
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    const char *foot = g_wuli_page == 1
        ? "UP/DN PAGE  C/D TAB  L/R SCRUB  A PLAY  B RESET"
        : "UP/DN PAGE  C/D PARAM  L/R ADJ  A PLAY  B RESET";
    u8g2_DrawStr(g, 8, 288, foot);
}

static void springResetMotion() {
    sp_t = 0.0f;
    sp_paused = false;
}

static void springReset() {
    sp_A = 8.0f; sp_k = 20.0f; sp_m = 1.0f;
    sp_phi = PI_F; sp_speed = 1; sp_overlay = 0x07;
    sp_graph = 0; sp_sel = 0;
    springResetMotion();
}

static float spring_omega() { return sqrtf(sp_k / fmaxf(0.1f, sp_m)); }
static float spring_T() { return 2.0f * PI_F / spring_omega(); }
static float spring_x() { return sp_A * cosf(spring_omega() * sp_t + sp_phi); }
static float spring_v() { return -sp_A * spring_omega() * sinf(spring_omega() * sp_t + sp_phi); }
static float spring_a() { return -spring_omega() * spring_omega() * spring_x(); }

static const char *SPRING_PARAM_NAME[8] = {
    "质量 m", "劲度系数 k", "振幅 A", "初相 phi",
    "播放速度", "回复力 F", "速度 v", "加速度 a"
};

static void springParamText(int i, char *b, size_t n) {
    switch (i) {
        case 0: snprintf(b, n, "%.1f kg", sp_m); break;
        case 1: snprintf(b, n, "%.0f N/m", sp_k); break;
        case 2: snprintf(b, n, "%.0f cm", sp_A); break;
        case 3: snprintf(b, n, "%s", sp_phi < 0.1f ? "0" : (sp_phi < 2.0f ? "pi/2" : "pi")); break;
        case 4: snprintf(b, n, "%s", sp_speed ? "1x" : "0.25x"); break;
        case 5: snprintf(b, n, "%s", (sp_overlay & 1) ? "ON" : "OFF"); break;
        case 6: snprintf(b, n, "%s", (sp_overlay & 2) ? "ON" : "OFF"); break;
        default: snprintf(b, n, "%s", (sp_overlay & 4) ? "ON" : "OFF"); break;
    }
}

static void springInput(float dt) {
    if (in.up && !prevIn.up) g_wuli_page = (g_wuli_page + 2) % 3;
    if (in.down && !prevIn.down) g_wuli_page = (g_wuli_page + 1) % 3;
    if (in.k3 && !prevIn.k3) sp_paused = !sp_paused;
    if (in.k4 && !prevIn.k4) springResetMotion();

    if (g_wuli_page == 0) {
        if (in.k1 && !prevIn.k1) g_wuli_sel = (g_wuli_sel + 7) % 8; // reuse shared selector
        if (in.k2 && !prevIn.k2) g_wuli_sel = (g_wuli_sel + 1) % 8;
        sp_sel = g_wuli_sel;
        if (sp_sel <= 2 && in.x != 0) {
            if (sp_sel == 0) sp_m = fmaxf(0.5f, fminf(3.0f, sp_m + (in.x > 0 ? 1 : -1) * dt * 0.8f));
            else if (sp_sel == 1) sp_k = fmaxf(5.0f, fminf(40.0f, sp_k + (in.x > 0 ? 1 : -1) * dt * 12.0f));
            else sp_A = fmaxf(2.0f, fminf(12.0f, sp_A + (in.x > 0 ? 1 : -1) * dt * 4.0f));
        } else if (sp_sel == 3 && in.x != 0 && !prevIn.x) {
            sp_phi = sp_phi < 0.1f ? PI_F * 0.5f : (sp_phi < 2.0f ? PI_F : 0.0f);
        } else if (sp_sel == 4 && in.x != 0 && !prevIn.x) {
            sp_speed = !sp_speed;
        } else if (sp_sel >= 5 && in.x != 0 && !prevIn.x) {
            sp_overlay ^= (uint8_t)(1u << (sp_sel - 5));
        }
    } else {
        if ((in.k1 && !prevIn.k1) || (in.k2 && !prevIn.k2)) {
            sp_graph = (sp_graph + ((in.k2 && !prevIn.k2) ? 1 : 2)) % 3;
            sp_paused = true;
        }
        if (in.x != 0) {
            sp_paused = true;
            float T = spring_T();
            sp_t = fmodf(sp_t + (in.x > 0 ? 1 : -1) * dt * 0.35f, 2.0f * T);
            if (sp_t < 0) sp_t += 2.0f * T;
        }
    }
    prevIn = in;
}

static void springUpdate(float dt) {
    if (!sp_paused) sp_t += dt * (sp_speed ? 1.0f : 0.25f);
}

static void springReadouts() {
    float T = spring_T(), w = spring_omega();
    float x = spring_x(), v = spring_v(), a = spring_a();
    float F = -sp_k * (x / 100.0f);
    char b[96];
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "T %4.2fs   f %4.2fHz   w %4.2f", T, 1.0f / T, w);
    u8g2_DrawStr(g, 8, 212, b);
    snprintf(b, sizeof(b), "x %+5.1fcm  v %+5.2f  a %+5.2f", x, v, a);
    u8g2_DrawStr(g, 8, 228, b);
    snprintf(b, sizeof(b), "F %+5.2fN   k %.0f   m %.1f", F, sp_k, sp_m);
    u8g2_DrawStr(g, 8, 244, b);
}

static void springParamStrip() {
    char val[32], b[96];
    springParamText(g_wuli_sel, val, sizeof(val));
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    snprintf(b, sizeof(b), "> %s", SPRING_PARAM_NAME[g_wuli_sel]);
    u8g2_DrawUTF8(g, 8, 258, b);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 170, 258, val);
    snprintf(b, sizeof(b), "A %.0fcm  phi %s  %s  F %s v %s a %s",
             sp_A, sp_phi < 0.1f ? "0" : (sp_phi < 2.0f ? "pi/2" : "pi"),
             sp_speed ? "1x" : ".25x",
             (sp_overlay & 1) ? "Y" : "N", (sp_overlay & 2) ? "Y" : "N", (sp_overlay & 4) ? "Y" : "N");
    u8g2_DrawStr(g, 8, 272, b);
}

static void drawSpringCoil(int left, int right, int base, int amp, int coils) {
    int span = right - left;
    if (span < 8) span = 8;
    int px = left, py = base;
    for (int i = 1; i <= coils; ++i) {
        int x = left + span * i / coils;
        int y = base + ((i % 2) ? -amp : amp);
        u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
    u8g2_DrawLine(g, px, py, right, base);
}

static void drawSpringScene() {
    drawCard(8, 44, 384, 156);
    const int wall = 44, eq = 254, track = 184;
    const int massHalf = 24;
    float xcm = spring_x();
    int massX = eq + (int)(xcm * 6.0f);
    int springRight = massX - massHalf;
    if (springRight < wall + 20) springRight = wall + 20;
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    // Wall and floor.
    u8g2_DrawLine(g, wall, 58, wall, track);
    u8g2_DrawLine(g, wall, track, 382, track);
    u8g2_DrawLine(g, wall - 8, 58, wall, 58);
    drawSpringCoil(wall, springRight, track - 48, 10, 13);
    // Equilibrium and amplitude guide.
    drawDashV(eq, 56, track, 3);
    drawDashV(eq + (int)(sp_A * 6), 62, track - 4, 2);
    drawDashV(eq - (int)(sp_A * 6), 62, track - 4, 2);
    u8g2_DrawStr(g, eq - 10, 60, "x=0");
    char b[24];
    snprintf(b, sizeof(b), "-A"); u8g2_DrawStr(g, eq - (int)(sp_A * 6) - 14, 62, b);
    snprintf(b, sizeof(b), "+A"); u8g2_DrawStr(g, eq + (int)(sp_A * 6) + 4, 62, b);
    // Mass block.
    u8g2_DrawFrame(g, massX - massHalf, track - 62, massHalf * 2, 30);
    u8g2_DrawDisc(g, massX - 12, track - 34, 4, U8G2_DRAW_ALL);
    u8g2_DrawDisc(g, massX + 12, track - 34, 4, U8G2_DRAW_ALL);
    u8g2_DrawDisc(g, massX, track - 48, 3, U8G2_DRAW_ALL);
    // Arrows: F opposite x, v according to velocity, a opposite x.
    if (sp_overlay & 1) {
        int dir = xcm >= 0 ? -1 : 1;
        drawArrow(massX + dir * massHalf, track - 47, massX + dir * (massHalf + 28), track - 47);
        u8g2_DrawStr(g, massX + dir * (massHalf + 10) - 3, track - 51, "F");
    }
    if (sp_overlay & 2) {
        int dir = spring_v() >= 0 ? 1 : -1;
        drawArrow(massX, track - 70, massX + dir * 28, track - 70);
        u8g2_DrawStr(g, massX + dir * 12 - 3, track - 76, "v");
    }
    if (sp_overlay & 4) {
        int dir = spring_a() >= 0 ? 1 : -1;
        drawArrow(massX, track - 24, massX + dir * 24, track - 24);
        u8g2_DrawStr(g, massX + dir * 10 - 3, track - 18, "a");
    }
}

static void springAxes(int L, int R, int T, int B, float ymin, float ymax, const char *ylab) {
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    for (int i = 0; i <= 4; ++i) {
        int y = B - i * (B - T) / 4;
        drawDashH(L, R, y, 2);
        char q[16]; snprintf(q, sizeof(q), "%.1f", ymin + (ymax - ymin) * i / 4.0f);
        u8g2_DrawStr(g, 2, y + 4, q);
    }
    u8g2_DrawLine(g, L, B, R, B);
    u8g2_DrawLine(g, L, T, L, B);
    (void)ylab;
}

static void springPlotCurve(int L, int R, int T, int B, int mode, int series) {
    float Tp = spring_T();
    float tmax = 2.0f * Tp;
    float w = spring_omega();
    float E = 0.5f * sp_k * (sp_A / 100.0f) * (sp_A / 100.0f);
    int prev_x = -1, prev_y = -1;
    for (int k = 0; k <= 180; ++k) {
        float t = tmax * k / 180.0f;
        float x = sp_A * cosf(w * t + sp_phi);
        float v = -sp_A * w * sinf(w * t + sp_phi);
        float a = -w * w * x;
        float val;
        if (mode == 0) val = series == 0 ? x / fmaxf(0.1f, sp_A) : (series == 1 ? v / fmaxf(0.1f, sp_A * w) : a / fmaxf(0.1f, sp_A * w * w));
        else {
            float xx = x / 100.0f;
            float U = 0.5f * sp_k * xx * xx;
            float K = 0.5f * sp_m * v * v;
            float tot = fmaxf(1e-6f, K + U);
            val = series == 0 ? K / tot : (series == 1 ? U / tot : (K + U) / fmaxf(1e-6f, E));
        }
        int px = L + (int)(k * (R - L) / 180.0f);
        int py = (mode == 0) ? (T + B) / 2 - (int)(val * (B - T) / 2)
                            : B - (int)(val * (B - T));
        if (prev_x >= 0) {
            bool on = (series == 0) || (series == 1 && (k / 3) % 2 == 0) || (series == 2 && (k / 2) % 2 == 0);
            if (on) u8g2_DrawLine(g, prev_x, prev_y, px, py);
        }
        prev_x = px; prev_y = py;
    }
}

static void drawSpringGraph() {
    drawCard(8, 44, 384, 156);
    const int L = 54, R = 378, T = 64, B = 182;
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    const char *tabs[3] = { "PHASE", "ENERGY", "PERIOD" };
    for (int i = 0; i < 3; ++i) {
        int x = 220 + i * 54;
        if (i == sp_graph) { u8g2_DrawBox(g, x - 4, 46, 50, 14); u8g2_SetDrawColor(g, 1); }
        u8g2_DrawStr(g, x, 57, tabs[i]);
        u8g2_SetDrawColor(g, 0);
    }
    if (sp_graph == 0) {
        springAxes(L, R, T, B, -1.0f, 1.0f, "x/v/a");
        springPlotCurve(L, R, T, B, 0, 0);
        springPlotCurve(L, R, T, B, 0, 1);
        springPlotCurve(L, R, T, B, 0, 2);
        // Current time marker.
        float Tp = spring_T();
        float tt = fmodf(sp_t, 2.0f * Tp);
        int cx = L + (int)(tt / (2.0f * Tp) * (R - L));
        drawDashV(cx, T, B, 3);
        u8g2_DrawStr(g, L + 2, T - 4, "x/v/a");
        u8g2_DrawStr(g, L + 2, 196, "x");
        u8g2_DrawStr(g, L + 16, 196, "v");
        u8g2_DrawStr(g, L + 30, 196, "a");
    } else if (sp_graph == 1) {
        springAxes(L, R, T, B, 0.0f, 1.0f, "E/E0");
        springPlotCurve(L, R, T, B, 1, 0);
        springPlotCurve(L, R, T, B, 1, 1);
        springPlotCurve(L, R, T, B, 1, 2);
        u8g2_DrawStr(g, 44, 196, "K");
        u8g2_DrawStr(g, 58, 196, "U");
        u8g2_DrawStr(g, 72, 196, "E");
    } else {
        springAxes(L, R, T, B, 0.0f, 2.0f, "T/s");
        float Tp = spring_T();
        int y = B - (int)(Tp / 2.0f * (B - T));
        u8g2_DrawLine(g, L, y, R, y);
        for (int i = 0; i < 5; ++i) {
            int x = L + i * (R - L) / 4;
            u8g2_DrawDisc(g, x, y, 2, U8G2_DRAW_ALL);
        }
        u8g2_DrawStr(g, L + 10, y - 6, "T constant");
    }
}

static void drawProjectileFormula() {
    drawCard(8, 44, 384, 156);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 20, 70, "原理 / 公式");
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 20, 96, "x = v0 * t          horizontal uniform");
    u8g2_DrawStr(g, 20, 116, "y = h - 0.5*g*t^2   vertical free fall");
    u8g2_DrawStr(g, 20, 136, "tfall = sqrt(2h/g)  independent of v0");
    u8g2_DrawStr(g, 20, 156, "R = v0 * tfall");
    u8g2_DrawStr(g, 20, 176, "y-t: parabola   x-t: straight line");
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 20, 188, "改 v0 只改射程；改 h、g 才改落地时间。");
}

static void drawSpringFormula() {
    drawCard(8, 44, 384, 156);
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 20, 70, "公式 / 详情");
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 20, 96, "F = -k*x          restoring force");
    u8g2_DrawStr(g, 20, 116, "w = sqrt(k/m)");
    u8g2_DrawStr(g, 20, 136, "T = 2*pi*sqrt(m/k)");
    u8g2_DrawStr(g, 20, 156, "x = A*cos(w*t+phi)");
    u8g2_DrawStr(g, 20, 176, "v = -A*w*sin(...)");
    u8g2_DrawStr(g, 20, 196, "a = -w^2*x       T independent of A");
}

static void drawSpring() {
    darkBegin();
    darkHeader("机械振动 / 实验 1", "弹簧振子", g_wuli_page);
    if (g_wuli_page == 0) drawSpringScene();
    else if (g_wuli_page == 1) drawSpringGraph();
    else drawSpringFormula();
    springReadouts();
    springParamStrip();
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    const char *foot = g_wuli_page == 1
        ? "UP/DN PAGE  C/D TAB  L/R SCRUB  A PLAY  B RESET"
        : "UP/DN PAGE  C/D PARAM  L/R ADJ  A PLAY  B RESET";
    u8g2_DrawStr(g, 8, 288, foot);
}

static void freefallReset() {
    ff_t = 0; ff_y = ff_h; ff_run = false; ff_landed = false;
}

static void freefallInput(float dt) {
    if (!ff_run) {
        if (in.down) ff_h = fmaxf(5.0f, ff_h - dt * 15.0f);
        if (in.up) ff_h = fminf(90.0f, ff_h + dt * 15.0f);
        if (!ff_landed) ff_y = ff_h;
    }
    if (in.k3 && !prevIn.k3) {
        if (ff_run || ff_landed) freefallReset();
        else { ff_run = true; ff_t = 0; ff_y = ff_h; ff_landed = false; }
    }
    if (in.k4 && !prevIn.k4) freefallReset();
    prevIn = in;
}

static void freefallUpdate(float dt) {
    if (!ff_run) return;
    ff_t += dt; ff_y = ff_h - 4.9f * ff_t * ff_t;
    if (ff_y <= 0) { ff_y = 0; ff_run = false; ff_landed = true; }
}

static void drawFreefall() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "自由落体");

    const int top = 62, bottom = 224, cx = 205;
    u8g2_DrawLine(g, 28, bottom, 388, bottom);
    u8g2_DrawLine(g, cx, top, cx, bottom);
    u8g2_DrawLine(g, cx - 42, top, cx + 14, top);

    // Real metre scale.
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    for (int i = 0; i <= 4; ++i) {
        int y = top + (bottom - top) * i / 4;
        u8g2_DrawLine(g, 310, y, 328, y);
        char lb[24];
        snprintf(lb, sizeof(lb), "%.0fm", ff_h * (4 - i) / 4.0f);
        u8g2_DrawStr(g, 332, y + 4, lb);
        u8g2_DrawLine(g, cx - 14, y, 310, y);
    }

    float y = ff_y;
    if (y < 0) y = 0;
    if (y > ff_h) y = ff_h;
    int by = top + (int)((ff_h - y) / fmaxf(ff_h, 1.0f) * (bottom - top));
    u8g2_DrawDisc(g, cx, by, 7, U8G2_DRAW_ALL);

    float v = 9.8f * ff_t;
    int vlen = 12 + (int)(v * 2.0f);
    if (vlen > 55) vlen = 55;
    int ax = cx + 34;
    u8g2_DrawLine(g, ax, by, ax, by + vlen);
    u8g2_DrawLine(g, ax, by + vlen, ax - 5, by + vlen - 6);
    u8g2_DrawLine(g, ax, by + vlen, ax + 5, by + vlen - 6);
    u8g2_DrawStr(g, ax + 7, by + 14, "v");

    int gx = cx + 62;
    u8g2_DrawLine(g, gx, by, gx, by + 34);
    u8g2_DrawLine(g, gx, by + 34, gx - 5, by + 28);
    u8g2_DrawLine(g, gx, by + 34, gx + 5, by + 28);
    u8g2_DrawStr(g, gx + 7, by + 20, "g");

    char b[96];
    float sfall = ff_h - y;
    snprintf(b, sizeof(b), "h %.0fm  s %.1fm  y %.1fm", ff_h, sfall, y);
    u8g2_DrawStr(g, 8, 244, b);
    snprintf(b, sizeof(b), "t %.2fs v %.1f m/s a 9.8 m/s2", ff_t, v);
    u8g2_DrawStr(g, 8, 260, b);
    drawPhysicsHint(3);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "U/D:H A:DROP B:RESET SW:BACK");
}


static void uniformReset() { um_t = 0; um_x = 0; um_run = false; }
static void uniformInput(float dt) {
    if (!um_run) {
        if (in.x < 0) um_v = fmaxf(2.0f, um_v - dt * 8.0f);
        if (in.x > 0) um_v = fminf(30.0f, um_v + dt * 8.0f);
    }
    if (in.k3 && !prevIn.k3) { if (um_run) uniformReset(); else um_run = true; }
    if (in.k4 && !prevIn.k4) uniformReset();
    prevIn = in;
}
static void uniformUpdate(float dt) { if (um_run) { um_t += dt; um_x = um_v * um_t; } }
static void drawUniform() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 160, 38, "匀速直线运动");
    const int y = 190, x0 = 40;
    u8g2_DrawLine(g, 30, y, 380, y);
    int px = x0 + (int)(um_x * 6.0f);
    if (px > 370) px = 370;
    u8g2_DrawDisc(g, px, y - 14, 9, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, px + 12, y - 14, px + 32, y - 14);
    u8g2_DrawLine(g, px + 32, y - 14, px + 25, y - 19);
    u8g2_DrawLine(g, px + 32, y - 14, px + 25, y - 9);
    char b[80]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "v %.1f m/s   t %.2fs   x %.1fm", um_v, um_t, um_x); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "x = v * t   (a = 0)"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(4);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:V A:RUN B:RESET SW:BACK");
}

static void accelReset() { am_t = 0; am_v0 = 0; am_run = false; }
static void accelInput(float dt) {
    if (!am_run) {
        if (in.x < 0) am_a = fmaxf(0.5f, am_a - dt * 3.0f);
        if (in.x > 0) am_a = fminf(8.0f, am_a + dt * 3.0f);
    }
    if (in.k3 && !prevIn.k3) { if (am_run) accelReset(); else am_run = true; }
    if (in.k4 && !prevIn.k4) accelReset();
    prevIn = in;
}
static void accelUpdate(float dt) { if (am_run) { am_t += dt; } }
static void drawAccel() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 152, 38, "匀加速直线");
    const int y = 190, x0 = 40;
    u8g2_DrawLine(g, 30, y, 380, y);
    float v = am_v0 + am_a * am_t;
    float x = am_v0 * am_t + 0.5f * am_a * am_t * am_t;
    int px = x0 + (int)(x * 5.0f); if (px > 370) px = 370;
    u8g2_DrawDisc(g, px, y - 14, 9, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, px + 12, y - 14, px + 18 + (int)(v * 2), y - 14);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "a %.1f m/s2  t %.2fs  x %.1fm", am_a, am_t, x); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "v %.1f m/s  v = v0 + a t", v); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(5);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:A A:RUN B:RESET SW:BACK");
}

static void pendulumReset() { pd_t = 0; pd_A = 35; pd_run = true; }
static void pendulumInput(float dt) {
    if (in.x < 0) pd_A = fmaxf(5.0f, pd_A - dt * 18.0f);
    if (in.x > 0) pd_A = fminf(75.0f, pd_A + dt * 18.0f);
    if (in.k3 && !prevIn.k3) pd_run = !pd_run;
    if (in.k4 && !prevIn.k4) pendulumReset();
    prevIn = in;
}
static void pendulumUpdate(float dt) { if (pd_run) pd_t += dt; }
static void drawPendulum() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "单摆");
    const int px0 = 200, py0 = 58, len = 110;
    float omega = sqrtf(9.8f / 1.0f);
    float ang = pd_A * cosf(omega * pd_t) * PI_F / 180.0f;
    int bx = px0 + (int)(len * sinf(ang));
    int by = py0 + (int)(len * cosf(ang));
    u8g2_DrawLine(g, px0, py0, bx, by);
    u8g2_DrawDisc(g, bx, by, 9, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, px0 - 14, py0, px0 + 14, py0);
    u8g2_DrawCircle(g, px0, py0, 3, U8G2_DRAW_ALL);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "L 1.0m  A %.0fdeg  T %.2fs", pd_A, 2.0f * PI_F / omega); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "t %.2fs  theta %.0fdeg", pd_t, ang * 180.0f / PI_F); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(6);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:AMP A:PAUSE B:RESET SW:BACK");
}

static void forceReset() { fc_f1 = 30; fc_f2 = 20; fc_ang = 60; }
static void forceInput(float dt) {
    if (in.x < 0) fc_f1 = fmaxf(5.0f, fc_f1 - dt * 15.0f);
    if (in.x > 0) fc_f1 = fminf(60.0f, fc_f1 + dt * 15.0f);
    if (in.down) fc_f2 = fmaxf(5.0f, fc_f2 - dt * 15.0f);
    if (in.up) fc_f2 = fminf(60.0f, fc_f2 + dt * 15.0f);
    if (in.k3 && !prevIn.k3) fc_ang = (fc_ang == 60) ? 90 : (fc_ang == 90 ? 120 : 60);
    if (in.k4 && !prevIn.k4) forceReset();
    prevIn = in;
}
static void drawForce() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "力的合成");
    const int ox = 80, oy = 212;
    float a2 = fc_ang * PI_F / 180.0f;
    float f1x = fc_f1 * 4, f1y = 0;
    float f2x = fc_f2 * 4 * cosf(a2), f2y = -fc_f2 * 4 * sinf(a2);
    u8g2_DrawLine(g, ox, oy, ox + (int)f1x, oy + (int)f1y);
    u8g2_DrawLine(g, ox, oy, ox + (int)f2x, oy + (int)f2y);
    u8g2_DrawLine(g, ox, oy, ox + (int)(f1x + f2x), oy + (int)(f1y + f2y));
    u8g2_DrawCircle(g, ox, oy, 4, U8G2_DRAW_ALL);
    u8g2_DrawStr(g, ox + (int)f1x + 4, oy, "F1");
    u8g2_DrawStr(g, ox + (int)f2x + 4, oy + (int)f2y, "F2");
    u8g2_DrawStr(g, ox + (int)(f1x + f2x) + 4, oy + (int)(f1y + f2y), "R");
    float R = sqrtf(fc_f1 * fc_f1 + fc_f2 * fc_f2 + 2 * fc_f1 * fc_f2 * cosf(a2));
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "F1 %.0fN  F2 %.0fN  angle %ddeg", fc_f1, fc_f2, fc_ang); u8g2_DrawStr(g, 8, 244, b);
    snprintf(b, sizeof(b), "R %.1f N", R); u8g2_DrawStr(g, 8, 260, b);
    drawPhysicsHint(7);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F1 U/D:F2 A:ANGLE B:RESET SW:BACK");
}

static const char *PHYS_ROOT[] = { "初中实验", "高中实验", "返回主目录" };
static const char *PHYS_MIDDLE[] = { "匀速直线", "平均速度", "弹力与重力", "二力平衡", "同一直线二力", "声音", "平面镜成像", "光的折射", "凸透镜", "密度", "液体压强", "串并联电路", "欧姆定律", "电功率", "浮力", "杠杆", "电荷作用", "电流表", "变阻器", "焦耳定律", "返回" };
static const char *PHYS_HIGH[] = { "匀加速直线", "自由落体", "运动的合成", "平抛运动", "力的合成", "牛顿第二定律", "静摩擦", "胡克定律", "滑轮与张力", "圆周运动", "向心力", "机械功", "动能", "重力势能", "机械能守恒", "机车功率", "冲量", "动量守恒", "弹性碰撞", "非弹性碰撞", "反冲火箭", "欧姆定律", "串联电路", "并联电路", "闭合电路", "滑动变阻器", "安培力", "法拉第电磁感应", "楞次定律", "机械波", "波的干涉", "反射与折射", "全反射", "薄透镜成像", "双缝干涉", "棱镜色散", "理想气体状态方程", "气体等温变化", "温度计与热传递", "光电效应", "黑体辐射", "放射性衰变", "单摆", "弹簧振子", "返回" };
static const int PHYS_MIDDLE_VIEW[] = { 4, 53, 47, 46, 48, 49, 50, 35, 37, 54, 55, 26, 25, 56, 51, 52, 57, 58, 29, 59, 0 };
static const int PHYS_HIGH_VIEW[] = { 5, 3, 8, 1, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 6, 2, 0 };
static constexpr int PHYS_ROOT_N = 3;
static constexpr int PHYS_MIDDLE_N = 21;
static constexpr int PHYS_HIGH_N = 45;

static int phys_list_count() {
    return g_phys_cat == 0 ? PHYS_ROOT_N : (g_phys_cat == 1 ? PHYS_MIDDLE_N : PHYS_HIGH_N);
}

static const char *phys_list_name(int i) {
    if (g_phys_cat == 0) return PHYS_ROOT[i];
    if (g_phys_cat == 1) return PHYS_MIDDLE[i];
    return PHYS_HIGH[i];
}


static void motionCompReset() { mc_t = 0; mc_run = false; }
static void motionCompInput(float dt) {
    if (!mc_run) {
        if (in.x < 0) mc_vx = fmaxf(1.0f, mc_vx - dt * 4.0f);
        if (in.x > 0) mc_vx = fminf(10.0f, mc_vx + dt * 4.0f);
        if (in.down) mc_vy = fmaxf(1.0f, mc_vy - dt * 4.0f);
        if (in.up) mc_vy = fminf(10.0f, mc_vy + dt * 4.0f);
    }
    if (in.k3 && !prevIn.k3) { if (mc_run) motionCompReset(); else { mc_t = 0; mc_run = true; } }
    if (in.k4 && !prevIn.k4) motionCompReset();
    prevIn = in;
}
static void motionCompUpdate(float dt) { if (mc_run) mc_t += dt; }
static void drawMotionComp() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 148, 38, "运动的合成");
    const int ox = 58, oy = 198;
    float x = mc_vx * mc_t, y = mc_vy * mc_t;
    int bx = ox + (int)(x * 7), by = oy - (int)(y * 7);
    if (bx > 370) bx = 370;
    if (by < 58) by = 58;
    u8g2_DrawLine(g, 30, oy, 380, oy);
    u8g2_DrawLine(g, ox, 58, ox, 220);
    u8g2_DrawLine(g, ox, oy, bx, oy);
    u8g2_DrawLine(g, ox, oy, ox, by);
    u8g2_DrawLine(g, ox, oy, bx, by);
    u8g2_DrawDisc(g, bx, by, 7, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, bx, by, bx + 28, by);
    u8g2_DrawLine(g, bx + 28, by, bx + 21, by - 5);
    u8g2_DrawLine(g, bx + 28, by, bx + 21, by + 5);
    u8g2_DrawLine(g, bx, by, bx, by - 28);
    u8g2_DrawLine(g, bx, by - 28, bx - 5, by - 21);
    u8g2_DrawLine(g, bx, by - 28, bx + 5, by - 21);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "vx %.1f  vy %.1f  v %.1f m/s", mc_vx, mc_vy, sqrtf(mc_vx*mc_vx + mc_vy*mc_vy)); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "t %.2fs  x %.1fm  y %.1fm", mc_t, x, y); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(8);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:VX U/D:VY A:RUN B:RESET SW:BACK");
}

static void newtonReset() { n2_t = 0; n2_run = false; }
static void newtonInput(float dt) {
    if (!n2_run) {
        if (in.x < 0) n2_F = fmaxf(2.0f, n2_F - dt * 15.0f);
        if (in.x > 0) n2_F = fminf(40.0f, n2_F + dt * 15.0f);
        if (in.down) n2_m = fmaxf(0.5f, n2_m - dt * 1.5f);
        if (in.up) n2_m = fminf(5.0f, n2_m + dt * 1.5f);
    }
    if (in.k3 && !prevIn.k3) { if (n2_run) newtonReset(); else n2_run = true; }
    if (in.k4 && !prevIn.k4) newtonReset();
    prevIn = in;
}
static void newtonUpdate(float dt) { if (n2_run) n2_t += dt; }
static void drawNewton() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 154, 38, "牛顿第二定律");
    float a = n2_F / n2_m, x = 0.5f * a * n2_t * n2_t, v = a * n2_t;
    const int y = 188; int cx = 55 + (int)(x * 6); if (cx > 340) cx = 340;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, cx, y - 30, 34, 24);
    u8g2_DrawDisc(g, cx + 7, y - 4, 4, U8G2_DRAW_ALL);
    u8g2_DrawDisc(g, cx + 27, y - 4, 4, U8G2_DRAW_ALL);
    int fw = 20 + (int)(n2_F * 2); if (fw > 70) fw = 70;
    u8g2_DrawLine(g, cx + 36, y - 18, cx + 36 + fw, y - 18);
    u8g2_DrawLine(g, cx + 36 + fw, y - 18, cx + 30 + fw, y - 24);
    u8g2_DrawLine(g, cx + 36 + fw, y - 18, cx + 30 + fw, y - 12);
    u8g2_DrawStr(g, cx + 38 + fw, y - 22, "F");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "F %.0fN  m %.1fkg  a %.1f m/s2", n2_F, n2_m, a); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "t %.2fs  v %.1f m/s  x %.1fm", n2_t, v, x); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(9);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F U/D:M A:RUN B:RESET SW:BACK");
}

static void frictionReset() { fr_t = 0; fr_v = 0; fr_x = 0; fr_run = false; }
static void frictionInput(float dt) {
    if (!fr_run) {
        if (in.x < 0) fr_F = fmaxf(0.0f, fr_F - dt * 8.0f);
        if (in.x > 0) fr_F = fminf(25.0f, fr_F + dt * 8.0f);
    }
    if (in.k3 && !prevIn.k3) { if (fr_run) frictionReset(); else { fr_t = 0; fr_run = true; } }
    if (in.k4 && !prevIn.k4) frictionReset();
    prevIn = in;
}
static void frictionUpdate(float dt) {
    if (!fr_run) return;
    fr_t += dt;
    float fmax = 0.5f * 2.0f * 9.8f;
    if (fr_F <= fmax && fr_v <= 0.01f) { fr_v = 0; return; }
    float a = (fr_F - 0.3f * 2.0f * 9.8f) / 2.0f;
    if (a < 0) a = 0;
    fr_v += a * dt; fr_x += fr_v * dt;
}
static void drawFriction() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 154, 38, "静摩擦与滑动摩擦");
    float fmax = 0.5f * 2.0f * 9.8f;
    const int y = 190; int cx = 90 + (int)(fr_x * 6); if (cx > 300) cx = 300;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, cx, y - 28, 42, 24);
    int fw = (int)(fr_F * 4); if (fw > 80) fw = 80;
    u8g2_DrawLine(g, cx + 44, y - 16, cx + 44 + fw, y - 16);
    u8g2_DrawLine(g, cx + 44 + fw, y - 16, cx + 38 + fw, y - 22);
    u8g2_DrawLine(g, cx + 44 + fw, y - 16, cx + 38 + fw, y - 10);
    u8g2_DrawStr(g, cx + 46 + fw, y - 20, "F");
    int ff = (fr_F > fmax ? (int)(0.3f * 2.0f * 9.8f * 4) : fw);
    if (ff > 0) {
        u8g2_DrawLine(g, cx - 4, y - 8, cx - 4 - ff, y - 8);
        u8g2_DrawLine(g, cx - 4 - ff, y - 8, cx + 2 - ff, y - 14);
        u8g2_DrawLine(g, cx - 4 - ff, y - 8, cx + 2 - ff, y - 2);
        u8g2_DrawStr(g, cx - 8 - ff, y - 12, "f");
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "F %.1fN  fmax %.1fN", fr_F, fmax); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "t %.2fs  v %.1f  x %.1fm  %s", fr_t, fr_v, fr_x, fr_v > 0.01f ? "滑动" : "静止"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(10);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F A:RUN B:RESET SW:BACK");
}

static void hookeReset() { hk_m = 1.0f; hk_k = 20.0f; }
static void hookeInput(float dt) {
    if (in.x < 0) hk_k = fmaxf(5.0f, hk_k - dt * 8.0f);
    if (in.x > 0) hk_k = fminf(40.0f, hk_k + dt * 8.0f);
    if (in.down) hk_m = fmaxf(0.5f, hk_m - dt * 1.2f);
    if (in.up) hk_m = fminf(3.0f, hk_m + dt * 1.2f);
    if (in.k4 && !prevIn.k4) hookeReset();
    prevIn = in;
}
static void drawHooke() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "胡克定律");
    const int top = 58, cx = 205;
    float F = hk_m * 9.8f;
    float x = F / hk_k;
    int L = 50 + (int)(x * 20); if (L > 150) L = 150;
    u8g2_DrawLine(g, cx - 40, top, cx + 40, top);
    u8g2_DrawLine(g, cx, top, cx, top + L);
    // spring zig-zag
    for (int i = 0; i < 10; ++i) {
        int y0 = top + L * i / 10, y1 = top + L * (i + 1) / 10;
        int x0 = cx + ((i % 2) ? 12 : -12);
        int x1 = cx + (((i + 1) % 2) ? 12 : -12);
        u8g2_DrawLine(g, x0, y0, x1, y1);
    }
    u8g2_DrawBox(g, cx - 22, top + L, 44, 28);
    u8g2_DrawLine(g, cx, top + L + 28, cx, top + L + 48);
    u8g2_DrawLine(g, cx - 14, top + L + 48, cx + 14, top + L + 48);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "m %.1fkg  F %.1fN  k %.1fN/m", hk_m, F, hk_k); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "x = F/k = %.2fm  (%.0fcm)", x, x * 100); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(11);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:K U/D:M B:RESET SW:BACK");
}


static void pulleyReset() { pv_t = 0; pv_run = false; }
static void pulleyInput(float dt) {
    if (!pv_run) {
        if (in.x < 0) pv_m1 = fmaxf(0.5f, pv_m1 - dt * 1.2f);
        if (in.x > 0) pv_m1 = fminf(4.0f, pv_m1 + dt * 1.2f);
        if (in.down) pv_m2 = fmaxf(0.5f, pv_m2 - dt * 1.2f);
        if (in.up) pv_m2 = fminf(4.0f, pv_m2 + dt * 1.2f);
    }
    if (in.k3 && !prevIn.k3) { if (pv_run) pulleyReset(); else pv_run = true; }
    if (in.k4 && !prevIn.k4) pulleyReset();
    prevIn = in;
}
static void pulleyUpdate(float dt) { if (pv_run) pv_t += dt; }
static void drawPulley() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 154, 38, "滑轮与张力");
    float sum = pv_m1 + pv_m2;
    float a = (pv_m2 - pv_m1) * 9.8f / sum;
    float T = 2.0f * pv_m1 * pv_m2 * 9.8f / sum;
    float d = 0.5f * a * pv_t * pv_t;
    const int px = 200, py = 76;
    u8g2_DrawCircle(g, px, py, 13, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, px - 13, py, 155, py + 35);
    u8g2_DrawLine(g, px + 13, py, 245, py + 35);
    u8g2_DrawBox(g, 135, py + 35 + (int)(d * 12), 40, 28);
    u8g2_DrawBox(g, 225, py + 35 - (int)(d * 12), 40, 28);
    u8g2_DrawLine(g, 155, py + 35, 155, py + 35 + (int)(d * 12));
    u8g2_DrawLine(g, 245, py + 35, 245, py + 35 - (int)(d * 12));
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "m1 %.1fkg  m2 %.1fkg  a %+.1f m/s2", pv_m1, pv_m2, a); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "T %.1fN  t %.2fs", T, pv_t); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(12);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:M1 U/D:M2 A:RUN B:RESET SW:BACK");
}

static void circularReset() { cm_t = 0; cm_run = false; }
static void circularInput(float dt) {
    if (!cm_run) {
        if (in.x < 0) cm_v = fmaxf(1.0f, cm_v - dt * 4.0f);
        if (in.x > 0) cm_v = fminf(10.0f, cm_v + dt * 4.0f);
        if (in.down) cm_r = fmaxf(0.5f, cm_r - dt * 1.0f);
        if (in.up) cm_r = fminf(4.0f, cm_r + dt * 1.0f);
    }
    if (in.k3 && !prevIn.k3) { if (cm_run) cm_run = false; else cm_run = true; }
    if (in.k4 && !prevIn.k4) circularReset();
    prevIn = in;
}
static void circularUpdate(float dt) { if (cm_run) cm_t += dt; }
static void drawCircular() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "圆周运动");
    const int cx = 210, cy = 140;
    float R = cm_r * 28.0f;
    float omega = cm_v / cm_r;
    float ang = omega * cm_t;
    int bx = cx + (int)(R * cosf(ang));
    int by = cy + (int)(R * sinf(ang));
    u8g2_DrawCircle(g, cx, cy, (int)R, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, cx, cy, bx, by);
    u8g2_DrawDisc(g, bx, by, 7, U8G2_DRAW_ALL);
    float tx = -sinf(ang), ty = cosf(ang);
    u8g2_DrawLine(g, bx, by, bx + (int)(tx * 32), by + (int)(ty * 32));
    u8g2_DrawLine(g, bx + (int)(tx * 32), by + (int)(ty * 32), bx + (int)(tx * 32) - (int)(tx * 7) + (int)(-ty * 5), by + (int)(ty * 32) - (int)(ty * 7) + (int)(tx * 5));
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "v %.1f m/s  r %.1fm  T %.2fs", cm_v, cm_r, 2.0f * PI_F / omega); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "ac %.1f m/s2  t %.2fs", cm_v * cm_v / cm_r, cm_t); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(13);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:V U/D:R A:RUN/PAUSE B:RESET");
}

static void centripetalReset() { cp_m = 1.0f; cp_v = 4.0f; cp_r = 2.0f; }
static void centripetalInput(float dt) {
    if (in.x < 0) cp_v = fmaxf(1.0f, cp_v - dt * 4.0f);
    if (in.x > 0) cp_v = fminf(10.0f, cp_v + dt * 4.0f);
    if (in.down) cp_r = fmaxf(0.5f, cp_r - dt * 1.0f);
    if (in.up) cp_r = fminf(4.0f, cp_r + dt * 1.0f);
    if (in.k3 && !prevIn.k3) cp_m = (cp_m > 1.5f) ? 0.5f : (cp_m > 0.8f ? 2.0f : 1.0f);
    if (in.k4 && !prevIn.k4) centripetalReset();
    prevIn = in;
}
static void drawCentripetal() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "向心力");
    const int cx = 210, cy = 140;
    float R = cp_r * 28.0f;
    int bx = cx + (int)R, by = cy;
    u8g2_DrawCircle(g, cx, cy, (int)R, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, cx, cy, bx, by);
    u8g2_DrawDisc(g, bx, by, 7, U8G2_DRAW_ALL);
    float F = cp_m * cp_v * cp_v / cp_r;
    int fl = 20 + (int)(F * 2.0f); if (fl > 75) fl = 75;
    u8g2_DrawLine(g, bx, by, bx - fl, by);
    u8g2_DrawLine(g, bx - fl, by, bx - fl + 8, by - 6);
    u8g2_DrawLine(g, bx - fl, by, bx - fl + 8, by + 6);
    u8g2_DrawStr(g, bx - fl - 4, by - 8, "F");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "m %.1fkg  v %.1f  r %.1fm", cp_m, cp_v, cp_r); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "F = mv2/r = %.1fN", F); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(14);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:V U/D:R A:MASS B:RESET SW:BACK");
}

static void workReset() { wk_F = 10; wk_s = 5; }
static void workInput(float dt) {
    if (in.x < 0) wk_F = fmaxf(1.0f, wk_F - dt * 8.0f);
    if (in.x > 0) wk_F = fminf(30.0f, wk_F + dt * 8.0f);
    if (in.down) wk_s = fmaxf(1.0f, wk_s - dt * 4.0f);
    if (in.up) wk_s = fminf(15.0f, wk_s + dt * 4.0f);
    if (in.k4 && !prevIn.k4) workReset();
    prevIn = in;
}
static void drawWork() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "机械功");
    const int y = 190, x0 = 70;
    u8g2_DrawLine(g, 30, y, 380, y);
    int len = (int)(wk_s * 14); if (len > 300) len = 300;
    u8g2_DrawBox(g, x0, y - 28, 40, 24);
    u8g2_DrawBox(g, x0 + len, y - 28, 40, 24);
    u8g2_DrawLine(g, x0 + 42, y - 16, x0 + 42 + (int)(wk_F * 2), y - 16);
    u8g2_DrawStr(g, x0 + 44 + (int)(wk_F * 2), y - 20, "F");
    u8g2_DrawLine(g, x0 + 20, y + 8, x0 + len + 20, y + 8);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "F %.0fN  s %.0fm  W %.0fJ", wk_F, wk_s, wk_F * wk_s); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "W = F s  (同向为正功)"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(15);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F U/D:S B:RESET SW:BACK");
}


static void kineticReset() { ke_m = 2; ke_v = 5; }
static void kineticInput(float dt) {
    if (in.x < 0) ke_m = fmaxf(0.5f, ke_m - dt * 1.5f);
    if (in.x > 0) ke_m = fminf(5.0f, ke_m + dt * 1.5f);
    if (in.down) ke_v = fmaxf(1.0f, ke_v - dt * 3.0f);
    if (in.up) ke_v = fminf(12.0f, ke_v + dt * 3.0f);
    if (in.k4 && !prevIn.k4) kineticReset();
    prevIn = in;
}
static void drawKinetic() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 166, 38, "动能");
    float Ek = 0.5f * ke_m * ke_v * ke_v;
    const int y = 170;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, 150, y - 34, 46, 28);
    int al = (int)(ke_v * 14); if (al > 110) al = 110;
    u8g2_DrawLine(g, 198, y - 20, 198 + al, y - 20);
    u8g2_DrawLine(g, 198 + al, y - 20, 190 + al, y - 26);
    u8g2_DrawLine(g, 198 + al, y - 20, 190 + al, y - 14);
    u8g2_DrawStr(g, 200 + al, y - 24, "v");
    int bar = (int)(Ek * 2); if (bar > 300) bar = 300;
    u8g2_DrawFrame(g, 40, 202, 320, 10);
    if (bar > 0) u8g2_DrawBox(g, 41, 203, bar, 8);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "m %.1fkg  v %.1f m/s  Ek %.1fJ", ke_m, ke_v, Ek); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "Ek = 1/2 m v2"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(16);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:M U/D:V B:RESET SW:BACK");
}

static void potentialReset() { pe_m = 2; pe_h = 5; }
static void potentialInput(float dt) {
    if (in.x < 0) pe_m = fmaxf(0.5f, pe_m - dt * 1.5f);
    if (in.x > 0) pe_m = fminf(5.0f, pe_m + dt * 1.5f);
    if (in.down) pe_h = fmaxf(0.0f, pe_h - dt * 4.0f);
    if (in.up) pe_h = fminf(15.0f, pe_h + dt * 4.0f);
    if (in.k4 && !prevIn.k4) potentialReset();
    prevIn = in;
}
static void drawPotential() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 154, 38, "重力势能");
    float Ep = pe_m * 9.8f * pe_h;
    const int ground = 210, cx = 250;
    u8g2_DrawLine(g, 30, ground, 380, ground);
    int py = ground - (int)(pe_h * 9); if (py < 55) py = 55;
    u8g2_DrawLine(g, cx - 55, py, cx + 45, py);
    u8g2_DrawLine(g, cx - 55, py, cx - 55, ground);
    u8g2_DrawBox(g, cx - 12, py - 22, 32, 22);
    u8g2_DrawLine(g, 90, ground, 90, py);
    u8g2_DrawLine(g, 90, py, 98, py + 8);
    u8g2_DrawLine(g, 90, py, 82, py + 8);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "m %.1fkg  h %.1fm  Ep %.1fJ", pe_m, pe_h, Ep); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "Ep = mgh   (零势能面在 ground)"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(17);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:M U/D:H B:RESET SW:BACK");
}

static void energyReset() { ec_t = 0; ec_y = ec_h; ec_v = 0; ec_run = false; }
static void energyInput(float dt) {
    if (!ec_run) {
        if (in.x < 0) ec_h = fmaxf(2.0f, ec_h - dt * 4.0f);
        if (in.x > 0) ec_h = fminf(20.0f, ec_h + dt * 4.0f);
        if (in.down) ec_m = fmaxf(0.5f, ec_m - dt * 1.0f);
        if (in.up) ec_m = fminf(4.0f, ec_m + dt * 1.0f);
        if (!ec_run) ec_y = ec_h;
    }
    if (in.k3 && !prevIn.k3) { if (ec_run) energyReset(); else { ec_t = 0; ec_y = ec_h; ec_v = 0; ec_run = true; } }
    if (in.k4 && !prevIn.k4) energyReset();
    prevIn = in;
}
static void energyUpdate(float dt) {
    if (!ec_run) return;
    ec_t += dt; ec_y = ec_h - 4.9f * ec_t * ec_t; ec_v = 9.8f * ec_t;
    if (ec_y <= 0) { ec_y = 0; ec_run = false; }
}
static void drawEnergy() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 148, 38, "机械能守恒");
    float E0 = ec_m * 9.8f * ec_h;
    float Ep = ec_m * 9.8f * ec_y;
    float Ek = 0.5f * ec_m * ec_v * ec_v;
    const int ground = 200, cx = 220, top = 58;
    u8g2_DrawLine(g, 30, ground, 380, ground);
    u8g2_DrawLine(g, cx, top, cx, ground);
    int by = top + (int)((ec_h - ec_y) / fmaxf(ec_h, 1.0f) * (ground - top));
    u8g2_DrawDisc(g, cx, by, 7, U8G2_DRAW_ALL);
    // bars
    int epw = E0 > 0 ? (int)(260 * Ep / E0) : 0;
    int ekw = E0 > 0 ? (int)(260 * Ek / E0) : 0;
    u8g2_DrawFrame(g, 60, 208, 260, 8); u8g2_DrawBox(g, 61, 209, epw, 6);
    u8g2_DrawFrame(g, 60, 222, 260, 8); u8g2_DrawBox(g, 61, 223, ekw, 6);
    u8g2_SetFont(g, u8g2_font_7x13_tf); u8g2_DrawStr(g, 36, 214, "Ep"); u8g2_DrawStr(g, 36, 228, "Ek");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "h %.0fm m %.1fkg t %.2fs", ec_h, ec_m, ec_t); u8g2_DrawStr(g, 8, 242, b);
    snprintf(b, sizeof(b), "Ep %.1fJ  Ek %.1fJ  E %.1fJ", Ep, Ek, Ep + Ek); u8g2_DrawStr(g, 8, 256, b);
    u8g2_DrawStr(g, 8, 270, "Ep = mgh   Ek = 1/2mv2");
    drawPhysicsHint(18);
    u8g2_DrawStr(g, 8, 292, "L/R:H U/D:M A:RUN B:RESET SW:BACK");
}

static void powerReset() { pw_F = 10; pw_v = 5; }
static void powerInput(float dt) {
    if (in.x < 0) pw_F = fmaxf(1.0f, pw_F - dt * 6.0f);
    if (in.x > 0) pw_F = fminf(30.0f, pw_F + dt * 6.0f);
    if (in.down) pw_v = fmaxf(1.0f, pw_v - dt * 3.0f);
    if (in.up) pw_v = fminf(10.0f, pw_v + dt * 3.0f);
    if (in.k4 && !prevIn.k4) powerReset();
    prevIn = in;
}
static void drawPower() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(g, 154, 38, "机车功率");
    float P = pw_F * pw_v;
    const int y = 180;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, 160, y - 32, 52, 26);
    u8g2_DrawDisc(g, 172, y - 4, 5, U8G2_DRAW_ALL);
    u8g2_DrawDisc(g, 202, y - 4, 5, U8G2_DRAW_ALL);
    int fw = (int)(pw_F * 2); if (fw > 55) fw = 55;
    u8g2_DrawLine(g, 212, y - 20, 212 + fw, y - 20);
    u8g2_DrawLine(g, 212 + fw, y - 20, 204 + fw, y - 26);
    u8g2_DrawLine(g, 212 + fw, y - 20, 204 + fw, y - 14);
    u8g2_DrawStr(g, 212 + fw, y - 24, "F");
    int vx = 100;
    u8g2_DrawLine(g, vx, y + 14, vx + (int)(pw_v * 14), y + 14);
    u8g2_DrawLine(g, vx + (int)(pw_v * 14), y + 14, vx + (int)(pw_v * 14) - 8, y + 8);
    u8g2_DrawLine(g, vx + (int)(pw_v * 14), y + 14, vx + (int)(pw_v * 14) - 8, y + 20);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "F %.0fN  v %.1f m/s  P %.0fW", pw_F, pw_v, P); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "P = F v  (work rate)"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(19);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F U/D:V B:RESET SW:BACK");
}


static void impulseReset() { im_F = 10; im_t = 2; }
static void impulseInput(float dt) {
    if (in.x < 0) im_F = fmaxf(1.0f, im_F - dt * 6.0f);
    if (in.x > 0) im_F = fminf(30.0f, im_F + dt * 6.0f);
    if (in.down) im_t = fmaxf(0.2f, im_t - dt * 1.2f);
    if (in.up) im_t = fminf(5.0f, im_t + dt * 1.2f);
    if (in.k4 && !prevIn.k4) impulseReset();
    prevIn = in;
}
static void drawImpulse() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "冲量");
    const int y = 180;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, 150, y - 30, 42, 24);
    int fw = (int)(im_F * 3); if (fw > 90) fw = 90;
    u8g2_DrawLine(g, 194, y - 18, 194 + fw, y - 18);
    u8g2_DrawLine(g, 194 + fw, y - 18, 186 + fw, y - 24);
    u8g2_DrawLine(g, 194 + fw, y - 18, 186 + fw, y - 12);
    int bw = (int)(im_t * 60); if (bw > 300) bw = 300;
    u8g2_DrawFrame(g, 40, 210, 300, 9); u8g2_DrawBox(g, 41, 211, bw, 7);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "F %.0fN  t %.1fs  J %.1fN·s", im_F, im_t, im_F * im_t); u8g2_DrawStr(g, 8, 238, b);
    snprintf(b, sizeof(b), "J = F t   (过程量)"); u8g2_DrawStr(g, 8, 256, b);
    drawPhysicsHint(20); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F U/D:T B:RESET SW:BACK");
}

static void momentumReset() { mo_m = 2; mo_v = 5; }
static void momentumInput(float dt) {
    if (in.x < 0) mo_m = fmaxf(0.5f, mo_m - dt * 1.2f);
    if (in.x > 0) mo_m = fminf(5.0f, mo_m + dt * 1.2f);
    if (in.down) mo_v = fmaxf(1.0f, mo_v - dt * 3.0f);
    if (in.up) mo_v = fminf(10.0f, mo_v + dt * 3.0f);
    if (in.k4 && !prevIn.k4) momentumReset();
    prevIn = in;
}
static void drawMomentum() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "动量");
    const int y = 180;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, 145, y - 30, 46, 24);
    int vw = (int)(mo_v * 14); if (vw > 110) vw = 110;
    u8g2_DrawLine(g, 193, y - 18, 193 + vw, y - 18);
    u8g2_DrawLine(g, 193 + vw, y - 18, 185 + vw, y - 24);
    u8g2_DrawLine(g, 193 + vw, y - 18, 185 + vw, y - 12);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "m %.1fkg  v %.1f m/s  p %.1f kg·m/s", mo_m, mo_v, mo_m * mo_v); u8g2_DrawStr(g, 8, 238, b);
    snprintf(b, sizeof(b), "p = m v   (方向同 v)"); u8g2_DrawStr(g, 8, 256, b);
    drawPhysicsHint(21); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:M U/D:V B:RESET SW:BACK");
}

static void elasticReset() { el_m1 = 2; el_v1 = 3; el_m2 = 1; el_v2 = -2; }
static void elasticInput(float dt) {
    if (in.x < 0) el_v1 = fmaxf(-8.0f, el_v1 - dt * 3.0f);
    if (in.x > 0) el_v1 = fminf(8.0f, el_v1 + dt * 3.0f);
    if (in.down) el_v2 = fmaxf(-8.0f, el_v2 - dt * 3.0f);
    if (in.up) el_v2 = fminf(8.0f, el_v2 + dt * 3.0f);
    if (in.k3 && !prevIn.k3) { el_m1 = (el_m1 > 1.5f) ? 1.0f : (el_m1 > 0.8f ? 2.0f : 0.5f); }
    if (in.k4 && !prevIn.k4) elasticReset();
    prevIn = in;
}
static void drawElastic() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "弹性碰撞");
    float v1 = ((el_m1 - el_m2) * el_v1 + 2 * el_m2 * el_v2) / (el_m1 + el_m2);
    float v2 = ((el_m2 - el_m1) * el_v2 + 2 * el_m1 * el_v1) / (el_m1 + el_m2);
    const int y = 180;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, 120, y - 30, 46, 24);
    u8g2_DrawBox(g, 250, y - 30, 46, 24);
    int a1 = (int)(el_v1 * 8), a2 = (int)(el_v2 * 8);
    u8g2_DrawLine(g, 168, y - 18, 168 + a1, y - 18);
    u8g2_DrawLine(g, 250, y - 18, 250 + a2, y - 18);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "前 v1 %+.1f  v2 %+.1f", el_v1, el_v2); u8g2_DrawStr(g, 8, 222, b);
    snprintf(b, sizeof(b), "后 v1' %+.1f  v2' %+.1f", v1, v2); u8g2_DrawStr(g, 8, 238, b);
    snprintf(b, sizeof(b), "p %.1f   Ek 守恒", el_m1 * el_v1 + el_m2 * el_v2); u8g2_DrawStr(g, 8, 254, b);
    drawPhysicsHint(22); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:V1 U/D:V2 A:MASS B:RESET");
}

static void inelasticReset() { in_m1 = 2; in_v1 = 3; in_m2 = 1; in_v2 = 0; }
static void inelasticInput(float dt) {
    if (in.x < 0) in_v1 = fmaxf(-8.0f, in_v1 - dt * 3.0f);
    if (in.x > 0) in_v1 = fminf(8.0f, in_v1 + dt * 3.0f);
    if (in.down) in_v2 = fmaxf(-8.0f, in_v2 - dt * 3.0f);
    if (in.up) in_v2 = fminf(8.0f, in_v2 + dt * 3.0f);
    if (in.k4 && !prevIn.k4) inelasticReset();
    prevIn = in;
}
static void drawInelastic() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "非弹性碰撞");
    float v = (in_m1 * in_v1 + in_m2 * in_v2) / (in_m1 + in_m2);
    const int y = 180;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, 120, y - 30, 46, 24);
    u8g2_DrawBox(g, 250, y - 30, 46, 24);
    int a1 = (int)(in_v1 * 8), a2 = (int)(in_v2 * 8);
    u8g2_DrawLine(g, 168, y - 18, 168 + a1, y - 18);
    u8g2_DrawLine(g, 250, y - 18, 250 + a2, y - 18);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "前 v1 %+.1f  v2 %+.1f", in_v1, in_v2); u8g2_DrawStr(g, 8, 222, b);
    snprintf(b, sizeof(b), "碰后共同速度 v' %+.1f", v); u8g2_DrawStr(g, 8, 238, b);
    snprintf(b, sizeof(b), "p %.1f   Ek 损失", in_m1 * in_v1 + in_m2 * in_v2); u8g2_DrawStr(g, 8, 254, b);
    drawPhysicsHint(23); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:V1 U/D:V2 B:RESET SW:BACK");
}

static void recoilReset() { rc_m = 10; rc_fuel = 1; rc_ve = 50; }
static void recoilInput(float dt) {
    if (in.x < 0) rc_m = fmaxf(2.0f, rc_m - dt * 2.0f);
    if (in.x > 0) rc_m = fminf(30.0f, rc_m + dt * 2.0f);
    if (in.down) rc_fuel = fmaxf(0.2f, rc_fuel - dt * 0.4f);
    if (in.up) rc_fuel = fminf(3.0f, rc_fuel + dt * 0.4f);
    if (in.k4 && !prevIn.k4) recoilReset();
    prevIn = in;
}
static void drawRecoil() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "反冲火箭");
    const int y = 180;
    u8g2_DrawLine(g, 30, y, 380, y);
    u8g2_DrawBox(g, 180, y - 42, 38, 32);
    u8g2_DrawLine(g, 199, y - 42, 199, y - 58);
    u8g2_DrawLine(g, 199, y - 58, 190, y - 50);
    u8g2_DrawLine(g, 199, y - 58, 208, y - 50);
    // exhaust
    u8g2_DrawLine(g, 199, y - 10, 185, y + 18);
    u8g2_DrawLine(g, 199, y - 10, 199, y + 22);
    u8g2_DrawLine(g, 199, y - 10, 213, y + 18);
    float dv = rc_fuel * rc_ve / rc_m;
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "M %.0fkg  fuel %.1fkg  ve %.0fm/s", rc_m, rc_fuel, rc_ve); u8g2_DrawStr(g, 8, 238, b);
    snprintf(b, sizeof(b), "dv = fuel*ve/M = %.1f m/s", dv); u8g2_DrawStr(g, 8, 256, b);
    drawPhysicsHint(24); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:M U/D:FUEL B:RESET SW:BACK");
}


static void ohmReset() { oh_V = 6; oh_R = 3; }
static void ohmInput(float dt) {
    if (in.x < 0) oh_V = fmaxf(1.0f, oh_V - dt * 2.0f);
    if (in.x > 0) oh_V = fminf(12.0f, oh_V + dt * 2.0f);
    if (in.down) oh_R = fmaxf(0.5f, oh_R - dt * 1.5f);
    if (in.up) oh_R = fminf(10.0f, oh_R + dt * 1.5f);
    if (in.k4 && !prevIn.k4) ohmReset();
    prevIn = in;
}
static void drawOhm() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "欧姆定律");
    float I = oh_V / oh_R;
    const int y = 170;
    u8g2_DrawFrame(g, 80, y - 40, 240, 80);
    u8g2_DrawLine(g, 80, y, 60, y); u8g2_DrawLine(g, 60, y, 60, y - 15);
    u8g2_DrawLine(g, 55, y - 15, 65, y - 15);
    u8g2_DrawLine(g, 80, y + 20, 320, y + 20);
    u8g2_DrawBox(g, 180, y - 10, 40, 16);
    int aw = (int)(I * 50); if (aw > 100) aw = 100;
    u8g2_DrawLine(g, 200, y + 20, 200 + aw, y + 20);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "U %.1fV  R %.1fΩ  I %.2fA", oh_V, oh_R, I); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "I = U / R"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(25); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:U U/D:R B:RESET SW:BACK");
}

static void seriesReset() { sr_R1 = 2; sr_R2 = 3; sr_V = 6; }
static void seriesInput(float dt) {
    if (in.x < 0) sr_R1 = fmaxf(0.5f, sr_R1 - dt * 1.0f);
    if (in.x > 0) sr_R1 = fminf(8.0f, sr_R1 + dt * 1.0f);
    if (in.down) sr_R2 = fmaxf(0.5f, sr_R2 - dt * 1.0f);
    if (in.up) sr_R2 = fminf(8.0f, sr_R2 + dt * 1.0f);
    if (in.k4 && !prevIn.k4) seriesReset();
    prevIn = in;
}
static void drawSeries() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "串联电路");
    float Req = sr_R1 + sr_R2, I = sr_V / Req;
    const int y = 170;
    u8g2_DrawFrame(g, 80, y - 40, 240, 70);
    u8g2_DrawBox(g, 160, y - 8, 36, 16); u8g2_DrawBox(g, 240, y - 8, 36, 16);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "R1 %.1f  R2 %.1f  Req %.1fΩ", sr_R1, sr_R2, Req); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "I %.2fA  U1 %.1fV  U2 %.1fV", I, I * sr_R1, I * sr_R2); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(26); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:R1 U/D:R2 B:RESET SW:BACK");
}

static void parallelReset() { pr_R1 = 2; pr_R2 = 3; pr_V = 6; }
static void parallelInput(float dt) {
    if (in.x < 0) pr_R1 = fmaxf(0.5f, pr_R1 - dt * 1.0f);
    if (in.x > 0) pr_R1 = fminf(8.0f, pr_R1 + dt * 1.0f);
    if (in.down) pr_R2 = fmaxf(0.5f, pr_R2 - dt * 1.0f);
    if (in.up) pr_R2 = fminf(8.0f, pr_R2 + dt * 1.0f);
    if (in.k4 && !prevIn.k4) parallelReset();
    prevIn = in;
}
static void drawParallel() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "并联电路");
    float Req = 1.0f / (1.0f / pr_R1 + 1.0f / pr_R2);
    float I = pr_V / Req;
    const int y = 170;
    u8g2_DrawLine(g, 80, y - 35, 300, y - 35); u8g2_DrawLine(g, 80, y + 25, 300, y + 25);
    u8g2_DrawLine(g, 130, y - 35, 130, y - 10); u8g2_DrawLine(g, 130, y + 10, 130, y + 25);
    u8g2_DrawLine(g, 240, y - 35, 240, y - 10); u8g2_DrawLine(g, 240, y + 10, 240, y + 25);
    u8g2_DrawBox(g, 115, y - 10, 30, 20); u8g2_DrawBox(g, 225, y - 10, 30, 20);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "R1 %.1f  R2 %.1f  Req %.2fΩ", pr_R1, pr_R2, Req); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "I %.2fA  I1 %.2fA  I2 %.2fA", I, pr_V / pr_R1, pr_V / pr_R2); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(27); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:R1 U/D:R2 B:RESET SW:BACK");
}

static void closedReset() { ce_E = 6; ce_r = 0.5f; ce_R = 3; }
static void closedInput(float dt) {
    if (in.x < 0) ce_R = fmaxf(0.5f, ce_R - dt * 1.5f);
    if (in.x > 0) ce_R = fminf(10.0f, ce_R + dt * 1.5f);
    if (in.down) ce_r = fmaxf(0.1f, ce_r - dt * 0.5f);
    if (in.up) ce_r = fminf(3.0f, ce_r + dt * 0.5f);
    if (in.k4 && !prevIn.k4) closedReset();
    prevIn = in;
}
static void drawClosed() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "闭合电路欧姆定律");
    float I = ce_E / (ce_R + ce_r), U = ce_E - I * ce_r;
    const int y = 170;
    u8g2_DrawFrame(g, 80, y - 40, 240, 70);
    u8g2_DrawBox(g, 100, y - 10, 34, 20); u8g2_DrawBox(g, 230, y - 10, 34, 20);
    u8g2_DrawStr(g, 102, y - 18, "r"); u8g2_DrawStr(g, 232, y - 18, "R");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "E %.1fV  r %.1fΩ  R %.1fΩ", ce_E, ce_r, ce_R); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "I %.2fA  U %.2fV  U=E-Ir", I, U); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(28); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:R U/D:r B:RESET SW:BACK");
}

static void rheostatReset() { rh_V = 6; rh_R0 = 2; rh_Rx = 3; }
static void rheostatInput(float dt) {
    if (in.x < 0) rh_Rx = fmaxf(0.0f, rh_Rx - dt * 1.5f);
    if (in.x > 0) rh_Rx = fminf(8.0f, rh_Rx + dt * 1.5f);
    if (in.down) rh_R0 = fmaxf(0.5f, rh_R0 - dt * 1.0f);
    if (in.up) rh_R0 = fminf(5.0f, rh_R0 + dt * 1.0f);
    if (in.k4 && !prevIn.k4) rheostatReset();
    prevIn = in;
}
static void drawRheostat() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "滑动变阻器");
    float R = rh_R0 + rh_Rx, I = rh_V / R;
    const int y = 170;
    u8g2_DrawFrame(g, 80, y - 40, 240, 70);
    u8g2_DrawBox(g, 110, y - 10, 32, 20); u8g2_DrawBox(g, 230, y - 10, 40, 20);
    u8g2_DrawLine(g, 236, y + 18, 264, y + 18);
    u8g2_DrawLine(g, 250, y + 12, 250, y + 24);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "R0 %.1f  Rx %.1f  R %.1fΩ", rh_R0, rh_Rx, R); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "U %.1fV  I %.2fA", rh_V, I); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(29); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:RX U/D:R0 B:RESET SW:BACK");
}


static void ampereReset() { am_B = 0.5f; am_I = 2.0f; am_L = 1.0f; am_rev = false; }
static void ampereInput(float dt) {
    if (in.x < 0) am_B = fmaxf(0.1f, am_B - dt * 0.2f);
    if (in.x > 0) am_B = fminf(1.5f, am_B + dt * 0.2f);
    if (in.down) am_I = fmaxf(0.5f, am_I - dt * 1.0f);
    if (in.up) am_I = fminf(5.0f, am_I + dt * 1.0f);
    if (in.k3 && !prevIn.k3) am_rev = !am_rev;
    if (in.k4 && !prevIn.k4) ampereReset();
    prevIn = in;
}
static void drawAmpere() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "安培力");
    const int y = 170;
    u8g2_DrawFrame(g, 50, 95, 70, 90); u8g2_DrawFrame(g, 280, 95, 70, 90);
    u8g2_DrawStr(g, 80, 90, "N"); u8g2_DrawStr(g, 310, 90, "S");
    for (int i = 0; i < 5; ++i) u8g2_DrawLine(g, 124 + i * 34, 120, 124 + i * 34, 200);
    u8g2_DrawLine(g, 198, 80, 198, 220);
    int iy = 150;
    int iw = (int)(am_I * 10);
    if (am_rev) u8g2_DrawLine(g, 198, iy, 198, iy + iw); else u8g2_DrawLine(g, 198, iy, 198, iy - iw);
    u8g2_DrawStr(g, 204, iy, am_rev ? "I down" : "I up");
    float F = am_B * am_I * am_L;
    int fx = 198 + (int)(F * 60); if (fx > 370) fx = 370;
    u8g2_DrawLine(g, 198, y, fx, y);
    u8g2_DrawLine(g, fx, y, fx - 8, y - 6); u8g2_DrawLine(g, fx, y, fx - 8, y + 6);
    u8g2_DrawStr(g, fx - 20, y - 10, "F");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "B %.2fT  I %.1fA  L %.1fm", am_B, am_I, am_L); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "F = BIL = %.2fN", F); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(30); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:B U/D:I A:REVERSE B:RESET");
}

static void faradayReset() { fd_speed = 1; fd_t = 0; fd_turns = 5; fd_run = true; }
static void faradayInput(float dt) {
    if (in.x < 0) fd_speed = fmaxf(0.2f, fd_speed - dt * 1.0f);
    if (in.x > 0) fd_speed = fminf(3.0f, fd_speed + dt * 1.0f);
    if (in.down) fd_turns = (fd_turns > 1) ? fd_turns - 1 : 1;
    if (in.up) fd_turns = (fd_turns < 20) ? fd_turns + 1 : 20;
    if (in.k3 && !prevIn.k3) fd_run = !fd_run;
    if (in.k4 && !prevIn.k4) faradayReset();
    prevIn = in;
}
static void faradayUpdate(float dt) { if (fd_run) fd_t += dt * fd_speed; }
static void drawFaraday() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "法拉第电磁感应");
    const int cx = 250, cy = 130;
    for (int i = 0; i < 8; ++i) u8g2_DrawCircle(g, cx, cy, 32 + i * 2, U8G2_DRAW_ALL);
    int mx = 90 + (int)(40 * sinf(fd_t));
    u8g2_DrawFrame(g, mx - 22, cy - 12, 44, 24);
    u8g2_DrawStr(g, mx - 4, cy + 4, "N");
    u8g2_DrawLine(g, mx, cy, cx - 35, cy);
    float emf = fd_turns * fd_speed * 0.3f * cosf(fd_t);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "N %d  速度 %.1f  emf %+.2fV", fd_turns, fd_speed, emf); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "faster flux change -> larger emf"); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(31); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:SPEED U/D:TURNS A:PAUSE B:RESET");
}

static void lenzReset() { lz_approach = true; }
static void lenzInput(float dt) {
    if (in.k3 && !prevIn.k3) lz_approach = !lz_approach;
    if (in.k4 && !prevIn.k4) lenzReset();
    prevIn = in;
}
static void drawLenz() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "楞次定律");
    const int cx = 250, cy = 130;
    for (int i = 0; i < 8; ++i) u8g2_DrawCircle(g, cx, cy, 32 + i * 2, U8G2_DRAW_ALL);
    int mx = lz_approach ? 130 : 100;
    u8g2_DrawFrame(g, mx - 22, cy - 12, 44, 24);
    u8g2_DrawStr(g, mx - 4, cy + 4, "N");
    u8g2_DrawLine(g, mx, cy, cx - 35, cy);
    u8g2_DrawLine(g, cx, cy + 50, cx + 30, cy + 50);
    u8g2_DrawLine(g, cx, cy + 50, cx + 10, cy + 44);
    u8g2_DrawLine(g, cx, cy + 50, cx + 10, cy + 56);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "magnet %s  induced current: %s", lz_approach ? "approach" : "leave", lz_approach ? "repel" : "attract"); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "induced field opposes flux change");
    drawPhysicsHint(32); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "A:NEAR/FAR B:RESET SW:BACK");
}

static void waveReset() { mw_A = 22; mw_f = 1; mw_t = 0; mw_run = true; }
static void waveInput(float dt) {
    if (in.x < 0) mw_A = fmaxf(5.0f, mw_A - dt * 12.0f);
    if (in.x > 0) mw_A = fminf(35.0f, mw_A + dt * 12.0f);
    if (in.down) mw_f = fmaxf(0.3f, mw_f - dt * 0.5f);
    if (in.up) mw_f = fminf(2.5f, mw_f + dt * 0.5f);
    if (in.k3 && !prevIn.k3) mw_run = !mw_run;
    if (in.k4 && !prevIn.k4) waveReset();
    prevIn = in;
}
static void waveUpdate(float dt) { if (mw_run) mw_t += dt; }
static void drawWave() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "机械波");
    const int y = 145, left = 40, right = 370;
    u8g2_DrawLine(g, left, y, right, y);
    float lambda = 1.6f / mw_f;
    for (int x = left; x <= right; x += 2) {
        float phase = (x - left) / (right - left) * 3.0f * PI_F - mw_t * 3.0f * mw_f;
        int yy = y + (int)(mw_A * sinf(phase));
        u8g2_DrawPixel(g, x, yy);
        if (x % 20 == 0) u8g2_DrawLine(g, x, yy, x, yy + 8);
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "A %.0f  f %.1fHz  v 2.0m/s", mw_A, mw_f); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "lambda = v/f = %.2fm  t %.2fs", 2.0f / mw_f, mw_t); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(33); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:A U/D:F A:PAUSE B:RESET");
}


static void interfereReset() { iw_sep = 50; iw_lam = 24; }
static void interfereInput(float dt) {
    if (in.x < 0) iw_sep = fmaxf(20.0f, iw_sep - dt * 30.0f);
    if (in.x > 0) iw_sep = fminf(100.0f, iw_sep + dt * 30.0f);
    if (in.down) iw_lam = fmaxf(10.0f, iw_lam - dt * 15.0f);
    if (in.up) iw_lam = fminf(45.0f, iw_lam + dt * 15.0f);
    if (in.k4 && !prevIn.k4) interfereReset();
    prevIn = in;
}
static void drawInterfere() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "波的干涉");
    const int y = 120, cx = 200;
    int s1 = cx - (int)iw_sep / 2, s2 = cx + (int)iw_sep / 2;
    u8g2_DrawDisc(g, s1, y, 4, U8G2_DRAW_ALL); u8g2_DrawDisc(g, s2, y, 4, U8G2_DRAW_ALL);
    u8g2_DrawLine(g, 40, 200, 370, 200);
    for (int x = 45; x < 365; x += 4) {
        float th = atan2f((float)(x - cx), 180.0f);
        float d = iw_sep * sinf(th);
        float inten = cosf(PI_F * d / iw_lam);
        int h = (int)(24 * inten * inten);
        if (h > 2) u8g2_DrawLine(g, x, 200 - h, x, 200 + h);
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "d %.0f  lambda %.0f  (相对单位)", iw_sep, iw_lam); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "亮纹：相位差 = 0, 2pi, ...");
    drawPhysicsHint(34); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:D U/D:LAMBDA B:RESET");
}

static void refractReset() { rf_ang = 40; rf_n1 = 1; rf_n2 = 1.5f; }
static void refractInput(float dt) {
    if (in.x < 0) rf_ang = fmaxf(5.0f, rf_ang - dt * 20.0f);
    if (in.x > 0) rf_ang = fminf(80.0f, rf_ang + dt * 20.0f);
    if (in.down) rf_n2 = fmaxf(1.0f, rf_n2 - dt * 0.3f);
    if (in.up) rf_n2 = fminf(2.5f, rf_n2 + dt * 0.3f);
    if (in.k4 && !prevIn.k4) refractReset();
    prevIn = in;
}
static void drawRefract() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "反射与折射");
    const int ox = 200, oy = 150;
    u8g2_DrawLine(g, 30, oy, 370, oy);
    u8g2_DrawLine(g, ox, 70, ox, 230);
    float a = rf_ang * PI_F / 180.0f;
    int ix = ox - (int)(100 * sinf(a)), iy = oy - (int)(100 * cosf(a));
    u8g2_DrawLine(g, ix, iy, ox, oy);
    u8g2_DrawLine(g, ox, oy, ox + (int)(80 * sinf(a)), oy - (int)(80 * cosf(a)));
    float sa = rf_n1 * sinf(a) / rf_n2;
    if (sa > 1) sa = 1;
    float b = asinf(sa);
    u8g2_DrawLine(g, ox, oy, ox + (int)(80 * sinf(b)), oy + (int)(80 * cosf(b)));
    u8g2_DrawStr(g, ix, iy - 4, "in"); u8g2_DrawStr(g, ox + 50, oy - 60, "ref"); u8g2_DrawStr(g, ox + 50, oy + 30, "refr");
    char b2[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b2, sizeof(b2), "入射 %.0fdeg  n1 %.2f  n2 %.2f", rf_ang, rf_n1, rf_n2); u8g2_DrawStr(g, 8, 230, b2);
    snprintf(b2, sizeof(b2), "折射角 %.1fdeg  n1 sin i = n2 sin r", b * 180.0f / PI_F); u8g2_DrawStr(g, 8, 250, b2);
    drawPhysicsHint(35); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:ANGLE U/D:N2 B:RESET");
}

static void tirReset() { tr_ang = 45; tr_n1 = 1.5f; tr_n2 = 1.0f; }
static void tirInput(float dt) {
    if (in.x < 0) tr_ang = fmaxf(5.0f, tr_ang - dt * 20.0f);
    if (in.x > 0) tr_ang = fminf(85.0f, tr_ang + dt * 20.0f);
    if (in.down) tr_n1 = fmaxf(1.0f, tr_n1 - dt * 0.2f);
    if (in.up) tr_n1 = fminf(2.5f, tr_n1 + dt * 0.2f);
    if (in.k4 && !prevIn.k4) tirReset();
    prevIn = in;
}
static void drawTir() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "全反射");
    const int ox = 200, oy = 150;
    u8g2_DrawLine(g, 30, oy, 370, oy); u8g2_DrawLine(g, ox, 60, ox, 240);
    float a = tr_ang * PI_F / 180.0f;
    int ix = ox - (int)(100 * sinf(a)), iy = oy - (int)(100 * cosf(a));
    u8g2_DrawLine(g, ix, iy, ox, oy);
    bool can_tir = (tr_n1 > tr_n2);
    float crit = 90.0f;
    if (can_tir) crit = asinf(tr_n2 / tr_n1) * 180.0f / PI_F;
    bool tir = can_tir && (tr_ang >= crit);
    u8g2_DrawLine(g, ox, oy, ox + (int)(80 * sinf(a)), oy + (int)(80 * cosf(a)));
    if (!tir) {
        float sa = tr_n1 * sinf(a) / tr_n2;
        if (sa > 1) sa = 1;
        float b = asinf(sa);
        u8g2_DrawLine(g, ox, oy, ox + (int)(80 * sinf(b)), oy + (int)(80 * cosf(b)));
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    const char *state = !can_tir ? "no TIR" : (tir ? "TIR" : "refract");
    if (can_tir) snprintf(b, sizeof(b), "crit %.1fdeg  in %.0fdeg  %s", crit, tr_ang, state);
    else snprintf(b, sizeof(b), "n1 <= n2  in %.0fdeg  %s", tr_ang, state);
    u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "dense -> rare only");
    drawPhysicsHint(36); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:ANGLE U/D:N1 B:RESET");
}

static void lensReset() { ln_u = 30; ln_f = 10; }
static void lensInput(float dt) {
    if (in.x < 0) ln_u = fmaxf(12.0f, ln_u - dt * 8.0f);
    if (in.x > 0) ln_u = fminf(60.0f, ln_u + dt * 8.0f);
    if (in.down) ln_f = fmaxf(5.0f, ln_f - dt * 2.0f);
    if (in.up) ln_f = fminf(25.0f, ln_f + dt * 2.0f);
    if (in.k4 && !prevIn.k4) lensReset();
    prevIn = in;
}
static void drawLens() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "薄透镜成像");
    const int ox = 200, oy = 160;
    u8g2_DrawLine(g, 30, oy, 370, oy);
    u8g2_DrawLine(g, ox, 80, ox, 230);
    u8g2_DrawLine(g, ox, 95, ox, 225);
    u8g2_DrawLine(g, ox - 10, 105, ox + 10, 105); u8g2_DrawLine(g, ox - 10, 215, ox + 10, 215);
    float v = 1.0f / (1.0f / ln_f - 1.0f / ln_u);
    int ix = ox + (int)(v * 3);
    u8g2_DrawLine(g, ox - (int)(ln_u * 3), oy, ox - (int)(ln_u * 3), oy - 22);
    u8g2_DrawLine(g, ox - (int)(ln_u * 3), oy - 22, ox - (int)(ln_u * 3) + 5, oy - 16);
    u8g2_DrawLine(g, ox - (int)(ln_u * 3), oy - 22, ox - (int)(ln_u * 3) - 5, oy - 16);
    if (ix < 370) {
        u8g2_DrawLine(g, ix, oy, ix, oy + 22);
        u8g2_DrawLine(g, ix, oy + 22, ix + 5, oy + 16);
        u8g2_DrawLine(g, ix, oy + 22, ix - 5, oy + 16);
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "u %.0f  f %.0f  v %.1f", ln_u, ln_f, v); u8g2_DrawStr(g, 8, 230, b);
    snprintf(b, sizeof(b), "1/f = 1/u + 1/v  mag %.2f", fabsf(v / ln_u)); u8g2_DrawStr(g, 8, 250, b);
    drawPhysicsHint(37); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:U U/D:F B:RESET");
}

static void doubleSlitReset() { ds_lam = 30; ds_d = 3; }
static void doubleSlitInput(float dt) {
    if (in.x < 0) ds_d = fmaxf(1.0f, ds_d - dt * 1.0f);
    if (in.x > 0) ds_d = fminf(8.0f, ds_d + dt * 1.0f);
    if (in.down) ds_lam = fmaxf(10.0f, ds_lam - dt * 10.0f);
    if (in.up) ds_lam = fminf(45.0f, ds_lam + dt * 10.0f);
    if (in.k4 && !prevIn.k4) doubleSlitReset();
    prevIn = in;
}
static void drawDoubleSlit() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "双缝干涉");
    const int cx = 200, top = 80;
    u8g2_DrawLine(g, 40, top, 360, top);
    u8g2_DrawLine(g, 190, top, 190, top + 20); u8g2_DrawLine(g, 210, top, 210, top + 20);
    for (int x = 45; x < 360; x += 5) {
        float th = atan2f((float)(x - cx), 150.0f);
        float inten = cosf(PI_F * ds_d * sinf(th) / ds_lam);
        int h = (int)(30 * inten * inten);
        if (h > 2) u8g2_DrawLine(g, x, 180 - h, x, 180 + h);
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "d %.1f  lambda %.0f  (相对)", ds_d, ds_lam); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "fringe spacing ~ lambda/d");
    drawPhysicsHint(38); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:D U/D:LAMBDA B:RESET");
}

static void prismReset() { pd_ang = 40; }
static void prismInput(float dt) {
    if (in.x < 0) pd_ang = fmaxf(20.0f, pd_ang - dt * 15.0f);
    if (in.x > 0) pd_ang = fminf(70.0f, pd_ang + dt * 15.0f);
    if (in.k4 && !prevIn.k4) prismReset();
    prevIn = in;
}
static void drawPrism() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "棱镜色散");
    const int cx = 210, cy = 160;
    u8g2_DrawTriangle(g, cx, cy - 55, cx - 55, cy + 40, cx + 55, cy + 40);
    float a = pd_ang * PI_F / 180.0f;
    u8g2_DrawLine(g, 40, cy - 30, cx - 20, cy);
    u8g2_DrawLine(g, cx + 18, cy + 10, 370, cy + 10);
    u8g2_DrawLine(g, cx + 18, cy + 10, 370, cy + 22);
    u8g2_DrawLine(g, cx + 18, cy + 10, 370, cy + 34);
    u8g2_DrawStr(g, 300, cy + 8, "R");
    u8g2_DrawStr(g, 300, cy + 20, "G");
    u8g2_DrawStr(g, 300, cy + 32, "V");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "in %.0fdeg  prism dispersion", pd_ang); u8g2_DrawStr(g, 8, 238, b);
    u8g2_DrawStr(g, 8, 256, "n differs by color");
    drawPhysicsHint(39); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:ANGLE B:RESET SW:BACK");
}


static void idealGasReset() { ig_T = 300; ig_V = 1; }
static void idealGasInput(float dt) {
    if (in.x < 0) ig_T = fmaxf(100.0f, ig_T - dt * 80.0f);
    if (in.x > 0) ig_T = fminf(600.0f, ig_T + dt * 80.0f);
    if (in.down) ig_V = fmaxf(0.4f, ig_V - dt * 0.3f);
    if (in.up) ig_V = fminf(2.0f, ig_V + dt * 0.3f);
    if (in.k4 && !prevIn.k4) idealGasReset();
    prevIn = in;
}
static void drawIdealGas() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "理想气体状态");
    float P = 8.31f * ig_T / ig_V;
    u8g2_DrawFrame(g, 70, 90, 260, 110);
    int px = 70 + (int)(110 * ig_V / 2.0f);
    u8g2_DrawLine(g, px, 90, px, 200);
    u8g2_DrawLine(g, px - 8, 90, px + 8, 90);
    for (int i = 0; i < 24; ++i) {
        int x = 80 + (i * 37) % 150, yy = 110 + (i * 23) % 70;
        if (x < px - 5) u8g2_DrawPixel(g, x, yy);
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "T %.0fK  V %.2f  P %.0fPa", ig_T, ig_V, P); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "PV = nRT   (n=1 mol)");
    drawPhysicsHint(40); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:T U/D:V B:RESET SW:BACK");
}

static void isothermalReset() { it_T = 300; it_V = 1; }
static void isothermalInput(float dt) {
    if (in.x < 0) it_T = fmaxf(100.0f, it_T - dt * 80.0f);
    if (in.x > 0) it_T = fminf(600.0f, it_T + dt * 80.0f);
    if (in.down) it_V = fmaxf(0.4f, it_V - dt * 0.3f);
    if (in.up) it_V = fminf(2.0f, it_V + dt * 0.3f);
    if (in.k4 && !prevIn.k4) isothermalReset();
    prevIn = in;
}
static void drawIsothermal() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "气体等温变化");
    const int ox = 55, oy = 205;
    u8g2_DrawLine(g, ox, 60, ox, oy); u8g2_DrawLine(g, ox, oy, 370, oy);
    float P0 = 8.31f * it_T;
    for (int i = 1; i < 120; ++i) {
        float V = 0.35f + i * 0.014f;
        float P = P0 / V;
        int x = ox + (int)(V / 2.0f * 300);
        int y = oy - (int)(P / 2500.0f * 140);
        if (y > 60) u8g2_DrawPixel(g, x, y);
    }
    int vx = ox + (int)(it_V / 2.0f * 300), vy = oy - (int)(P0 / it_V / 2500.0f * 140);
    u8g2_DrawDisc(g, vx, vy, 4, U8G2_DRAW_ALL);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "T %.0fK  V %.2f  P %.0fPa", it_T, it_V, P0 / it_V); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "PV = 常数  (T 不变)");
    drawPhysicsHint(41); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:T U/D:V B:RESET SW:BACK");
}

static void thermoReset() { th_T1 = 80; th_T2 = 20; th_run = false; }
static void thermoInput(float dt) {
    if (!th_run) {
        if (in.x < 0) th_T1 = fmaxf(10.0f, th_T1 - dt * 10.0f);
        if (in.x > 0) th_T1 = fminf(100.0f, th_T1 + dt * 10.0f);
        if (in.down) th_T2 = fmaxf(0.0f, th_T2 - dt * 10.0f);
        if (in.up) th_T2 = fminf(80.0f, th_T2 + dt * 10.0f);
    }
    if (in.k3 && !prevIn.k3) th_run = !th_run;
    if (in.k4 && !prevIn.k4) thermoReset();
    prevIn = in;
}
static void thermoUpdate(float dt) {
    if (!th_run) return;
    float avg = (th_T1 + th_T2) * 0.5f;
    th_T1 += (avg - th_T1) * dt * 0.8f;
    th_T2 += (avg - th_T2) * dt * 0.8f;
}
static void drawThermo() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "温度计与热传递");
    const int y1 = 200, y2 = 200;
    u8g2_DrawFrame(g, 90, y1 - 80, 50, 90); u8g2_DrawFrame(g, 260, y2 - 80, 50, 90);
    int h1 = (int)(th_T1), h2 = (int)(th_T2);
    if (h1 > 78) h1 = 78;
    if (h2 > 78) h2 = 78;
    u8g2_DrawBox(g, 92, y1 - h1, 46, h1);
    u8g2_DrawBox(g, 262, y2 - h2, 46, h2);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "T1 %.0fC   T2 %.0fC", th_T1, th_T2); u8g2_DrawStr(g, 8, 238, b);
    u8g2_DrawStr(g, 8, 256, "heat flows hot -> cold");
    drawPhysicsHint(42); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:T1 U/D:T2 A:HEAT FLOW B:RESET");
}

static void photoelectricReset() { ph_f = 5; ph_W = 3; }
static void photoelectricInput(float dt) {
    if (in.x < 0) ph_f = fmaxf(1.0f, ph_f - dt * 2.0f);
    if (in.x > 0) ph_f = fminf(10.0f, ph_f + dt * 2.0f);
    if (in.down) ph_W = fmaxf(1.0f, ph_W - dt * 1.0f);
    if (in.up) ph_W = fminf(8.0f, ph_W + dt * 1.0f);
    if (in.k4 && !prevIn.k4) photoelectricReset();
    prevIn = in;
}
static void drawPhotoelectric() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "光电效应");
    const int ox = 55, oy = 205;
    u8g2_DrawLine(g, ox, 60, ox, oy); u8g2_DrawLine(g, ox, oy, 370, oy);
    float Ek = ph_f - ph_W;
    for (int i = 0; i < 120; ++i) {
        float f = 1.0f + i * 0.08f;
        float e = f - ph_W;
        int x = ox + (int)(f * 30), y = oy - (int)(fmaxf(0.0f, e) * 35);
        if (x < 370 && y > 60) u8g2_DrawPixel(g, x, y);
    }
    int x = ox + (int)(ph_f * 30), y = oy - (int)(fmaxf(0.0f, Ek) * 35);
    u8g2_DrawDisc(g, x, y, 4, U8G2_DRAW_ALL);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "f %.1f  W %.1f  Ek %.1f", ph_f, ph_W, fmaxf(0.0f, Ek)); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "Ek=hf-W; no e- below W");
    drawPhysicsHint(43); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F U/D:W B:RESET SW:BACK");
}

static void blackbodyReset() { bb_T = 3000; }
static void blackbodyInput(float dt) {
    if (in.x < 0) bb_T = fmaxf(1000.0f, bb_T - dt * 500.0f);
    if (in.x > 0) bb_T = fminf(7000.0f, bb_T + dt * 500.0f);
    if (in.k4 && !prevIn.k4) blackbodyReset();
    prevIn = in;
}
static void drawBlackbody() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "黑体辐射");
    const int ox = 55, oy = 205;
    u8g2_DrawLine(g, ox, 60, ox, oy); u8g2_DrawLine(g, ox, oy, 370, oy);
    for (int i = 1; i < 120; ++i) {
        float lam = 0.2f + i * 0.015f;
        float B = 1.0f / (powf(lam, 5) * (expf(14388.0f / (lam * bb_T)) - 1.0f));
        int x = ox + (int)(lam * 150);
        int y = oy - (int)(B * 1e4f);
        if (x < 370 && y > 60 && y < oy) u8g2_DrawPixel(g, x, y);
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "T %.0fK  peak shifts left", bb_T); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "T up -> peak wavelength down");
    drawPhysicsHint(44); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:T B:RESET SW:BACK");
}

static void decayReset() { rd_half = 5; rd_t = 0; rd_run = false; }
static void decayInput(float dt) {
    if (!rd_run) {
        if (in.x < 0) rd_half = fmaxf(1.0f, rd_half - dt * 1.0f);
        if (in.x > 0) rd_half = fminf(20.0f, rd_half + dt * 1.0f);
    }
    if (in.k3 && !prevIn.k3) { if (rd_run) rd_run = false; else rd_run = true; }
    if (in.k4 && !prevIn.k4) decayReset();
    prevIn = in;
}
static void decayUpdate(float dt) { if (rd_run) rd_t += dt; }
static void drawDecay() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "放射性衰变");
    const int ox = 55, oy = 205;
    u8g2_DrawLine(g, ox, 60, ox, oy); u8g2_DrawLine(g, ox, oy, 370, oy);
    for (int i = 0; i <= 120; ++i) {
        float t = i * 0.15f;
        float N = powf(0.5f, t / rd_half);
        int x = ox + (int)(t * 50), y = oy - (int)(N * 140);
        if (x < 370) u8g2_DrawPixel(g, x, y);
    }
    float N = powf(0.5f, rd_t / rd_half);
    int x = ox + (int)(rd_t * 50), y = oy - (int)(N * 140);
    if (x < 370) u8g2_DrawDisc(g, x, y, 4, U8G2_DRAW_ALL);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "N/N0 %.2f  t %.1fs  T1/2 %.1fs", N, rd_t, rd_half); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "N=N0*(1/2)^(t/T1/2)");
    drawPhysicsHint(45); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:T1/2 A:RUN/PAUSE B:RESET");
}


static void balanceReset() { jb_f1 = 10; jb_f2 = 10; }
static void balanceInput(float dt) {
    if (in.x < 0) jb_f1 = fmaxf(0.0f, jb_f1 - dt * 8.0f);
    if (in.x > 0) jb_f1 = fminf(20.0f, jb_f1 + dt * 8.0f);
    if (in.down) jb_f2 = fmaxf(0.0f, jb_f2 - dt * 8.0f);
    if (in.up) jb_f2 = fminf(20.0f, jb_f2 + dt * 8.0f);
    if (in.k4 && !prevIn.k4) balanceReset();
    prevIn = in;
}
static void drawBalance() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "二力平衡");
    const int cx = 200, cy = 180;
    u8g2_DrawBox(g, cx - 26, cy - 24, 52, 40);
    int a = (int)(jb_f1 * 4), b = (int)(jb_f2 * 4);
    u8g2_DrawLine(g, cx - 28, cy - 4, cx - 28 - a, cy - 4);
    u8g2_DrawLine(g, cx - 28 - a, cy - 4, cx - 20 - a, cy - 10);
    u8g2_DrawLine(g, cx - 28 - a, cy - 4, cx - 20 - a, cy + 2);
    u8g2_DrawLine(g, cx + 28, cy - 4, cx + 28 + b, cy - 4);
    u8g2_DrawLine(g, cx + 28 + b, cy - 4, cx + 20 + b, cy - 10);
    u8g2_DrawLine(g, cx + 28 + b, cy - 4, cx + 20 + b, cy + 2);
    u8g2_DrawStr(g, cx - 36 - a, cy - 12, "F1");
    u8g2_DrawStr(g, cx + 32 + b, cy - 12, "F2");
    char t[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(t, sizeof(t), "F1 %.0fN  F2 %.0fN  net %+.0fN", jb_f1, jb_f2, jb_f1 - jb_f2); u8g2_DrawStr(g, 8, 230, t);
    u8g2_DrawStr(g, 8, 250, "same body, equal, opposite, collinear");
    drawPhysicsHint(47); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F1 U/D:F2 B:RESET SW:BACK");
}

static void juniorWeightReset() { jw_m = 1; jw_k = 20; }
static void juniorWeightInput(float dt) {
    if (in.x < 0) jw_m = fmaxf(0.2f, jw_m - dt * 0.8f);
    if (in.x > 0) jw_m = fminf(3.0f, jw_m + dt * 0.8f);
    if (in.down) jw_k = fmaxf(5.0f, jw_k - dt * 8.0f);
    if (in.up) jw_k = fminf(40.0f, jw_k + dt * 8.0f);
    if (in.k4 && !prevIn.k4) juniorWeightReset();
    prevIn = in;
}
static void drawJuniorWeight() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "弹力与重力");
    const int cx = 200, top = 58;
    float F = jw_m * 9.8f, x = F / jw_k;
    int L = 45 + (int)(x * 60); if (L > 150) L = 150;
    u8g2_DrawLine(g, cx - 35, top, cx + 35, top);
    u8g2_DrawLine(g, cx, top, cx, top + L);
    for (int i = 0; i < 9; ++i) {
        int y0 = top + L * i / 9, y1 = top + L * (i + 1) / 9;
        int x0 = cx + ((i % 2) ? 11 : -11), x1 = cx + (((i + 1) % 2) ? 11 : -11);
        u8g2_DrawLine(g, x0, y0, x1, y1);
    }
    u8g2_DrawBox(g, cx - 20, top + L, 40, 26);
    u8g2_DrawLine(g, cx, top + L + 26, cx, top + L + 44);
    u8g2_DrawLine(g, cx - 12, top + L + 44, cx + 12, top + L + 44);
    char t[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(t, sizeof(t), "m %.1fkg  G %.1fN  k %.0fN/m", jw_m, F, jw_k); u8g2_DrawStr(g, 8, 230, t);
    snprintf(t, sizeof(t), "x = G/k = %.2fm", x); u8g2_DrawStr(g, 8, 250, t);
    drawPhysicsHint(46); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:M U/D:K B:RESET SW:BACK");
}

static void collinearReset() { jc_f1 = 10; jc_f2 = 6; jc_same = true; }
static void collinearInput(float dt) {
    if (in.x < 0) jc_f1 = fmaxf(0.0f, jc_f1 - dt * 8.0f);
    if (in.x > 0) jc_f1 = fminf(20.0f, jc_f1 + dt * 8.0f);
    if (in.down) jc_f2 = fmaxf(0.0f, jc_f2 - dt * 8.0f);
    if (in.up) jc_f2 = fminf(20.0f, jc_f2 + dt * 8.0f);
    if (in.k3 && !prevIn.k3) jc_same = !jc_same;
    if (in.k4 && !prevIn.k4) collinearReset();
    prevIn = in;
}
static void drawCollinear() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "同一直线二力");
    const int ox = 180, y = 175;
    int a = (int)(jc_f1 * 7), b = (int)(jc_f2 * 7);
    if (jc_same) {
        u8g2_DrawLine(g, ox, y, ox + a, y);
        u8g2_DrawLine(g, ox + a, y, ox + a - 8, y - 6);
        u8g2_DrawLine(g, ox + a, y, ox + a - 8, y + 6);
        u8g2_DrawLine(g, ox + a, y, ox + a + b, y);
        u8g2_DrawLine(g, ox + a + b, y, ox + a + b - 8, y - 6);
        u8g2_DrawLine(g, ox + a + b, y, ox + a + b - 8, y + 6);
    } else {
        u8g2_DrawLine(g, ox, y, ox + a, y);
        u8g2_DrawLine(g, ox + a, y, ox + a - 8, y - 6);
        u8g2_DrawLine(g, ox + a, y, ox + a - 8, y + 6);
        u8g2_DrawLine(g, ox, y, ox - b, y);
        u8g2_DrawLine(g, ox - b, y, ox - b + 8, y - 6);
        u8g2_DrawLine(g, ox - b, y, ox - b + 8, y + 6);
    }
    float R = jc_same ? jc_f1 + jc_f2 : jc_f1 - jc_f2;
    if (R < 0) R = -R;
    char t[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(t, sizeof(t), "F1 %.0fN  F2 %.0fN  %s", jc_f1, jc_f2, jc_same ? "same" : "opposite"); u8g2_DrawStr(g, 8, 230, t);
    snprintf(t, sizeof(t), "resultant %.0fN", R); u8g2_DrawStr(g, 8, 250, t);
    drawPhysicsHint(48); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F1 U/D:F2 A:DIR B:RESET SW:BACK");
}


static void soundReset() { js_f = 440; js_t = 0; js_run = true; }
static void soundInput(float dt) {
    if (in.x < 0) js_f = fmaxf(100.0f, js_f - dt * 80.0f);
    if (in.x > 0) js_f = fminf(1000.0f, js_f + dt * 80.0f);
    if (in.k3 && !prevIn.k3) js_run = !js_run;
    if (in.k4 && !prevIn.k4) soundReset();
    prevIn = in;
}
static void soundUpdate(float dt) { if (js_run) js_t += dt; }
static void drawSound() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "声音的产生与传播");
    const int cx = 200, cy = 112;
    u8g2_DrawFrame(g, cx - 18, cy - 24, 36, 48);
    u8g2_DrawLine(g, cx - 30, cy, cx - 80, cy);
    for (int i = 1; i <= 4; ++i) {
        int r = 12 + i * 12;
        u8g2_DrawCircle(g, cx, cy, r, U8G2_DRAW_ALL);
    }
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "f %.0fHz  T %.4fs", js_f, 1.0f / js_f); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "v = 340 m/s in air");
    drawPhysicsHint(49); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:F A:PAUSE B:RESET SW:BACK");
}

static void mirrorReset() { jm_ang = 35; }
static void mirrorInput(float dt) {
    if (in.x < 0) jm_ang = fmaxf(5.0f, jm_ang - dt * 20.0f);
    if (in.x > 0) jm_ang = fminf(75.0f, jm_ang + dt * 20.0f);
    if (in.k4 && !prevIn.k4) mirrorReset();
    prevIn = in;
}
static void drawMirror() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "平面镜成像");
    const int mx = 240, top = 70, bot = 230;
    const int objX = 145, imgX = 335, baseY = 170, tipY = 100, mirrorY = 150;
    u8g2_DrawLine(g, mx, top, mx, bot);
    for (int y = top + 6; y < bot; y += 12) u8g2_DrawLine(g, mx, y, mx + 5, y + 6);
    // object arrow
    u8g2_DrawLine(g, objX, baseY, objX, tipY);
    u8g2_DrawLine(g, objX, tipY, objX - 5, tipY + 8);
    u8g2_DrawLine(g, objX, tipY, objX + 5, tipY + 8);
    // image arrow (dashed)
    for (int y = tipY; y < baseY; y += 8) u8g2_DrawLine(g, imgX, y, imgX, y + 4);
    u8g2_DrawLine(g, imgX, tipY, imgX - 5, tipY + 8);
    u8g2_DrawLine(g, imgX, tipY, imgX + 5, tipY + 8);
    // incident and reflected rays
    u8g2_DrawLine(g, objX, tipY, mx, mirrorY);
    u8g2_DrawLine(g, mx, mirrorY, imgX, tipY);
    // normal
    for (int x = mx - 30; x < mx + 30; x += 8) u8g2_DrawPixel(g, x, mirrorY);
    u8g2_DrawStr(g, objX - 18, baseY + 14, "obj");
    u8g2_DrawStr(g, imgX - 18, baseY + 14, "image");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "object distance = image distance"); u8g2_DrawStr(g, 8, 238, b);
    u8g2_DrawStr(g, 8, 256, "image is virtual, same size");
    drawPhysicsHint(50); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "B:RESET SW:BACK");
}

static void buoyancyReset() { by_V = 1; by_m = 2; }
static void buoyancyInput(float dt) {
    if (in.x < 0) by_V = fmaxf(0.2f, by_V - dt * 0.3f);
    if (in.x > 0) by_V = fminf(2.0f, by_V + dt * 0.3f);
    if (in.down) by_m = fmaxf(0.5f, by_m - dt * 0.8f);
    if (in.up) by_m = fminf(4.0f, by_m + dt * 0.8f);
    if (in.k4 && !prevIn.k4) buoyancyReset();
    prevIn = in;
}
static void drawBuoyancy() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "浮力与阿基米德原理");
    const int x = 90, top = 90, bot = 230;
    u8g2_DrawFrame(g, x, top, 220, bot - top);
    u8g2_DrawLine(g, x, 150, x + 220, 150);
    int bw = 40, bh = 35;
    int bx = x + 80, by = 150 - (int)(by_V * 20);
    u8g2_DrawBox(g, bx, by, bw, bh);
    u8g2_DrawLine(g, bx + bw/2, by, bx + bw/2, by - 20);
    float F = 1000.0f * 9.8f * by_V;
    char t[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(t, sizeof(t), "V %.1fL  m %.1fkg  F %.0fN", by_V, by_m, F); u8g2_DrawStr(g, 8, 238, t);
    u8g2_DrawStr(g, 8, 256, "F = rho g V");
    drawPhysicsHint(51); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:V U/D:M B:RESET SW:BACK");
}

static void leverReset() { lv_L1 = 1; lv_F1 = 10; lv_L2 = 2; }
static void leverInput(float dt) {
    if (in.x < 0) lv_L1 = fmaxf(0.5f, lv_L1 - dt * 0.5f);
    if (in.x > 0) lv_L1 = fminf(3.0f, lv_L1 + dt * 0.5f);
    if (in.down) lv_L2 = fmaxf(0.5f, lv_L2 - dt * 0.5f);
    if (in.up) lv_L2 = fminf(3.0f, lv_L2 + dt * 0.5f);
    if (in.k3 && !prevIn.k3) lv_F1 = (lv_F1 > 15) ? 5 : (lv_F1 > 8 ? 20 : 10);
    if (in.k4 && !prevIn.k4) leverReset();
    prevIn = in;
}
static void drawLever() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "杠杆平衡");
    const int px = 200, py = 190;
    u8g2_DrawTriangle(g, px, py, px - 22, py + 30, px + 22, py + 30);
    u8g2_DrawLine(g, px - 140, py, px + 140, py);
    float F2 = lv_F1 * lv_L1 / lv_L2;
    int x1 = px - (int)(lv_L1 * 45), x2 = px + (int)(lv_L2 * 45);
    u8g2_DrawBox(g, x1 - 12, py - 24, 24, 24);
    u8g2_DrawBox(g, x2 - 12, py - 24, 24, 24);
    u8g2_DrawLine(g, x1, py - 24, x1, py - 48);
    u8g2_DrawLine(g, x2, py - 24, x2, py - 48);
    char t[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(t, sizeof(t), "F1 %.0fN L1 %.1f  L2 %.1f", lv_F1, lv_L1, lv_L2); u8g2_DrawStr(g, 8, 238, t);
    snprintf(t, sizeof(t), "F1 L1 = F2 L2 -> F2 %.1fN", F2); u8g2_DrawStr(g, 8, 256, t);
    drawPhysicsHint(52); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:L1 U/D:L2 A:F1 B:RESET");
}


static void avgSpeedReset() { av_s = 120; av_t = 4; }
static void avgSpeedInput(float dt) {
    if (in.x < 0) av_s = fmaxf(20.0f, av_s - dt * 30.0f);
    if (in.x > 0) av_s = fminf(200.0f, av_s + dt * 30.0f);
    if (in.down) av_t = fmaxf(0.5f, av_t - dt * 0.5f);
    if (in.up) av_t = fminf(10.0f, av_t + dt * 0.5f);
    if (in.k4 && !prevIn.k4) avgSpeedReset();
    prevIn = in;
}
static void drawAvgSpeed() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "测量平均速度");
    float v = av_s / av_t;
    const int x0 = 60, y = 190;
    u8g2_DrawLine(g, x0, y, 370, y);
    u8g2_DrawLine(g, x0, y, 360, y - 80);
    u8g2_DrawBox(g, 180, y - 26, 42, 22);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "s %.0fcm  t %.1fs  v %.1fcm/s", av_s, av_t, v); u8g2_DrawStr(g, 8, 238, b);
    u8g2_DrawStr(g, 8, 256, "v = s / t");
    drawPhysicsHint(53); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:S U/D:T B:RESET SW:BACK");
}

static void densityReset() { de_m = 54; de_V = 20; }
static void densityInput(float dt) {
    if (in.x < 0) de_m = fmaxf(10.0f, de_m - dt * 10.0f);
    if (in.x > 0) de_m = fminf(100.0f, de_m + dt * 10.0f);
    if (in.down) de_V = fmaxf(5.0f, de_V - dt * 5.0f);
    if (in.up) de_V = fminf(50.0f, de_V + dt * 5.0f);
    if (in.k4 && !prevIn.k4) densityReset();
    prevIn = in;
}
static void drawDensity() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "密度");
    float rho = de_m / de_V;
    int bw = 30 + (int)(de_V * 1.2f); if (bw > 100) bw = 100;
    int bx = 200 - bw / 2;
    u8g2_DrawFrame(g, bx, 90, bw, 60);
    u8g2_DrawBox(g, bx, 120, bw, 30);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "m %.0fg  V %.0fcm3  rho %.2f g/cm3", de_m, de_V, rho); u8g2_DrawStr(g, 8, 230, b);
    u8g2_DrawStr(g, 8, 250, "rho = m / V");
    drawPhysicsHint(54); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:M U/D:V B:RESET SW:BACK");
}

static void liquidReset() { lp_h = 0.5f; }
static void liquidInput(float dt) {
    if (in.x < 0) lp_h = fmaxf(0.1f, lp_h - dt * 0.2f);
    if (in.x > 0) lp_h = fminf(1.0f, lp_h + dt * 0.2f);
    if (in.k4 && !prevIn.k4) liquidReset();
    prevIn = in;
}
static void drawLiquid() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "液体压强");
    const int x = 80, top = 80, bot = 220;
    u8g2_DrawFrame(g, x, top, 220, bot - top);
    u8g2_DrawLine(g, x, top + 20, x + 220, top + 20);
    int hh = (int)(lp_h * 100);
    u8g2_DrawBox(g, x, bot - hh, 220, hh);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    float p = 1000.0f * 9.8f * lp_h;
    snprintf(b, sizeof(b), "h %.1fm  p %.0fPa", lp_h, p); u8g2_DrawStr(g, 8, 238, b);
    u8g2_DrawStr(g, 8, 256, "p = rho g h");
    drawPhysicsHint(55); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:H B:RESET SW:BACK");
}

static void ePowerReset() { ep_U = 6; ep_I = 2; }
static void ePowerInput(float dt) {
    if (in.x < 0) ep_U = fmaxf(1.0f, ep_U - dt * 1.5f);
    if (in.x > 0) ep_U = fminf(12.0f, ep_U + dt * 1.5f);
    if (in.down) ep_I = fmaxf(0.2f, ep_I - dt * 0.8f);
    if (in.up) ep_I = fminf(5.0f, ep_I + dt * 0.8f);
    if (in.k4 && !prevIn.k4) ePowerReset();
    prevIn = in;
}
static void drawEPower() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "电功率");
    float P = ep_U * ep_I;
    const int y = 170;
    u8g2_DrawFrame(g, 80, y - 40, 240, 70);
    u8g2_DrawBox(g, 150, y - 10, 40, 20);
    u8g2_DrawStr(g, 160, y - 18, "R");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "U %.1fV  I %.1fA  P %.1fW", ep_U, ep_I, P); u8g2_DrawStr(g, 8, 238, b);
    u8g2_DrawStr(g, 8, 256, "P = U I");
    drawPhysicsHint(56); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:U U/D:I B:RESET SW:BACK");
}


static void chargeReset() { qc_q1 = 2; qc_q2 = -2; qc_r = 3; }
static void chargeInput(float dt) {
    if (in.x < 0) qc_r = fmaxf(1.0f, qc_r - dt * 1.5f);
    if (in.x > 0) qc_r = fminf(6.0f, qc_r + dt * 1.5f);
    if (in.k3 && !prevIn.k3) qc_q2 = -qc_q2;
    if (in.k4 && !prevIn.k4) chargeReset();
    prevIn = in;
}
static void drawCharge() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 148, 38, "电荷间的相互作用");
    const int cx = 200, cy = 150;
    int sep = (int)(qc_r * 22);
    int x1 = cx - sep / 2, x2 = cx + sep / 2;
    u8g2_DrawDisc(g, x1, cy, 12, U8G2_DRAW_ALL);
    u8g2_DrawDisc(g, x2, cy, 12, U8G2_DRAW_ALL);
    u8g2_DrawStr(g, x1 - 4, cy + 4, qc_q1 > 0 ? "+" : "-");
    u8g2_DrawStr(g, x2 - 4, cy + 4, qc_q2 > 0 ? "+" : "-");
    bool repel = (qc_q1 * qc_q2 > 0);
    int dir = repel ? -1 : 1;
    u8g2_DrawLine(g, x1 - 16, cy - 28, x1 - 16 + dir * ((int)qc_r * 8), cy - 28);
    u8g2_DrawLine(g, x2 + 16, cy - 28, x2 + 16 + dir * ((int)qc_r * 8), cy - 28);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "q1 %+.0f  q2 %+.0f  r %.1f", qc_q1, qc_q2, qc_r); u8g2_DrawStr(g, 8, 238, b);
    snprintf(b, sizeof(b), "%s", repel ? "same charge: repel" : "opposite: attract"); u8g2_DrawStr(g, 8, 256, b);
    drawPhysicsHint(57); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:R A:Q2 SIGN B:RESET");
}

static void ammeterReset() { ja_R = 2; ja_V = 3; }
static void ammeterInput(float dt) {
    if (in.x < 0) ja_R = fmaxf(0.5f, ja_R - dt * 1.0f);
    if (in.x > 0) ja_R = fminf(8.0f, ja_R + dt * 1.0f);
    if (in.down) ja_V = fmaxf(1.0f, ja_V - dt * 1.0f);
    if (in.up) ja_V = fminf(6.0f, ja_V + dt * 1.0f);
    if (in.k4 && !prevIn.k4) ammeterReset();
    prevIn = in;
}
static void drawAmmeter() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 154, 38, "电流表模型");
    float I = ja_V / ja_R;
    const int y = 170;
    u8g2_DrawFrame(g, 80, y - 40, 240, 70);
    u8g2_DrawCircle(g, 200, y - 5, 18, U8G2_DRAW_ALL);
    u8g2_DrawStr(g, 196, y, "A");
    u8g2_DrawBox(g, 120, y - 10, 32, 20);
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "U %.1fV  R %.1fohm  I %.2fA", ja_V, ja_R, I); u8g2_DrawStr(g, 8, 238, b);
    u8g2_DrawStr(g, 8, 256, "Ammeter in series, I=U/R");
    drawPhysicsHint(58); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:R U/D:U B:RESET SW:BACK");
}

static void jouleReset() { jl_I = 2; jl_R = 5; jl_t = 10; }
static void jouleInput(float dt) {
    if (in.x < 0) jl_I = fmaxf(0.2f, jl_I - dt * 0.8f);
    if (in.x > 0) jl_I = fminf(5.0f, jl_I + dt * 0.8f);
    if (in.down) jl_R = fmaxf(1.0f, jl_R - dt * 2.0f);
    if (in.up) jl_R = fminf(20.0f, jl_R + dt * 2.0f);
    if (in.k3 && !prevIn.k3) jl_t = (jl_t > 15) ? 5 : (jl_t > 8 ? 20 : 10);
    if (in.k4 && !prevIn.k4) jouleReset();
    prevIn = in;
}
static void drawJoule() {
    drawStatus(); u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312); u8g2_DrawUTF8(g, 166, 38, "焦耳定律");
    float Q = jl_I * jl_I * jl_R * jl_t;
    const int y = 170;
    u8g2_DrawFrame(g, 80, y - 40, 240, 70);
    u8g2_DrawBox(g, 180, y - 10, 40, 20);
    u8g2_DrawStr(g, 186, y - 18, "R");
    char b[96]; u8g2_SetFont(g, u8g2_font_7x13_tf);
    snprintf(b, sizeof(b), "I %.1fA  R %.1fohm  t %.0fs", jl_I, jl_R, jl_t); u8g2_DrawStr(g, 8, 238, b);
    snprintf(b, sizeof(b), "Q = I2 R t = %.0fJ", Q); u8g2_DrawStr(g, 8, 256, b);
    drawPhysicsHint(59); u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, 8, 292, "L/R:I U/D:R A:T B:RESET");
}

static void drawPhysicsMenu() {
    drawStatus();
    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    const char *title = (g_phys_cat == 1) ? "初中物理" : (g_phys_cat == 2 ? "高中物理" : "物理实验");
    u8g2_DrawUTF8(g, 164, 38, title);

    int count = phys_list_count();
    if (g_phys_sel < g_phys_off) g_phys_off = g_phys_sel;
    if (g_phys_sel >= g_phys_off + 5) g_phys_off = g_phys_sel - 4;
    if (g_phys_off < 0) g_phys_off = 0;

    int rows = count < 5 ? count : 5;
    for (int row = 0; row < rows; ++row) {
        int i = g_phys_off + row;
        int y = 74 + row * 34;
        char num[12]; snprintf(num, sizeof(num), "%d", i + 1);
        u8g2_SetFont(g, u8g2_font_9x15_tf);
        u8g2_DrawStr(g, 52, y, num);
        u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(g, 136, y, phys_list_name(i));
        if (i == g_phys_sel) {
            u8g2_DrawFrame(g, 108, y - 22, 220, 30);
            u8g2_SetFont(g, u8g2_font_9x15_tf);
            u8g2_DrawStr(g, 90, y, ">");
        }
    }
    u8g2_SetFont(g, u8g2_font_7x13_tf);
}

static void handlePhysicsMenuInput() {
    int count = phys_list_count();
    if (in.down && !prevIn.down) { g_phys_sel = (g_phys_sel + 1) % count; if (g_phys_sel == 0) g_phys_off = 0; }
    if (in.up && !prevIn.up) { g_phys_sel = (g_phys_sel + count - 1) % count; if (g_phys_sel == 0) g_phys_off = 0; }

    if (in.k3 && !prevIn.k3) {
        if (g_phys_cat == 0) {
            if (g_phys_sel == 0) { g_phys_cat = 1; g_phys_sel = 0; g_phys_off = 0; }
            else if (g_phys_sel == 1) { g_phys_cat = 2; g_phys_sel = 0; g_phys_off = 0; }
            else { g_mode = MODE_MENU; g_phys_cat = 0; }
        } else {
            int view = (g_phys_cat == 1) ? PHYS_MIDDLE_VIEW[g_phys_sel] : PHYS_HIGH_VIEW[g_phys_sel];
            if (view == 0) { g_phys_cat = 0; g_phys_sel = 0; g_phys_off = 0; }
            else {
                if (view == 1) { g_wuli_page = 0; g_wuli_sel = 0; physicsReset(); }
                else if (view == 2) { g_wuli_page = 0; g_wuli_sel = 0; springReset(); }
                else if (view == 3) freefallReset();
                else if (view == 4) uniformReset();
                else if (view == 5) accelReset();
                else if (view == 6) pendulumReset();
                else if (view == 7) forceReset();
                else if (view == 8) motionCompReset();
                else if (view == 9) newtonReset();
                else if (view == 10) frictionReset();
                else if (view == 11) hookeReset();
                else if (view == 12) pulleyReset();
                else if (view == 13) circularReset();
                else if (view == 14) centripetalReset();
                else if (view == 15) workReset();
                else if (view == 16) kineticReset();
                else if (view == 17) potentialReset();
                else if (view == 18) energyReset();
                else if (view == 19) powerReset();
                else if (view == 20) impulseReset();
                else if (view == 21) momentumReset();
                else if (view == 22) elasticReset();
                else if (view == 23) inelasticReset();
                else if (view == 24) recoilReset();
                else if (view == 25) ohmReset();
                else if (view == 26) seriesReset();
                else if (view == 27) parallelReset();
                else if (view == 28) closedReset();
                else if (view == 29) rheostatReset();
                else if (view == 30) ampereReset();
                else if (view == 31) faradayReset();
                else if (view == 32) lenzReset();
                else if (view == 33) waveReset();
                else if (view == 34) interfereReset();
                else if (view == 35) refractReset();
                else if (view == 36) tirReset();
                else if (view == 37) lensReset();
                else if (view == 38) doubleSlitReset();
                else if (view == 39) prismReset();
                else if (view == 46) balanceReset();
                else if (view == 47) juniorWeightReset();
                else if (view == 48) collinearReset();
                else if (view == 49) soundReset();
                else if (view == 50) mirrorReset();
                else if (view == 51) buoyancyReset();
                else if (view == 52) leverReset();
                else if (view == 53) avgSpeedReset();
                else if (view == 54) densityReset();
                else if (view == 55) liquidReset();
                else if (view == 56) ePowerReset();
                else if (view == 57) chargeReset();
                else if (view == 58) ammeterReset();
                else if (view == 59) jouleReset();
                else if (view == 40) idealGasReset();
                else if (view == 41) isothermalReset();
                else if (view == 42) thermoReset();
                else if (view == 43) photoelectricReset();
                else if (view == 44) blackbodyReset();
                else decayReset();
                g_phys_view = view;
            }
        }
    }
    prevIn = in;
}

static void goBack() {
    if (g_mode == MODE_MENU) return;
    if (g_mode == MODE_PHYSICS) {
        if (g_phys_view != 0) g_phys_view = 0;
        else if (g_phys_cat != 0) { g_phys_cat = 0; g_phys_sel = 0; g_phys_off = 0; }
        else g_mode = MODE_MENU;
        return;
    }
    if (g_mode == MODE_WIFI) { g_mode = MODE_SETTINGS; return; }
    if (g_mode == MODE_GAME) { g_mode = MODE_MENU; return; }
    if (g_mode == MODE_SURVEY_REPORT) { g_mode = MODE_SURVEY_MENU; return; }
    if (g_mode == MODE_SURVEY) { g_mode = MODE_SURVEY_MENU; return; }
    if (g_mode == MODE_SURVEY_MENU) { g_mode = MODE_MENU; return; }
    if (g_mode == MODE_WORD) { g_mode = MODE_WORD_MENU; return; }
    if (g_mode == MODE_WORD_MENU) { g_mode = MODE_MENU; return; }
    if (g_mode == MODE_SETTINGS_CTRL) { g_mode = MODE_SETTINGS; return; }
    if (g_mode == MODE_SETTINGS) { g_mode = MODE_MENU; return; }
    if (g_mode == MODE_INFO || g_mode == MODE_HELP || g_mode == MODE_NOTES || g_mode == MODE_FEEDBACK) {
        g_mode = MODE_SETTINGS; return;
    }
    if (g_mode == MODE_VIDEO) { video_close(); g_mode = MODE_FILES; g_file_view = 0; return; }
    if (g_mode == MODE_MUSIC) { music_stop(); g_mode = MODE_MENU; return; }
    if (g_mode == MODE_IMAGE) {
        if (g_img_gray) { heap_caps_free(g_img_gray); g_img_gray = nullptr; }
        g_mode = MODE_FILES;
        g_file_view = 1;
        return;
    }
    if (g_mode == MODE_FILES) {
        wav_stop_playback();
        if (g_mp3_playing) g_mp3_stop = true;
        if (g_file_view != 0) { g_file_view = 0; files_free_text(); g_file_is_txt = g_file_is_wav = false; }
        else { files_free_text(); g_mode = MODE_MENU; }
        return;
    }
    g_mode = MODE_MENU;
}

static void gameTask(void *arg) {
    (void)arg;
    u8g2_st7305_config_t cfg = u8g2_st7305_default_config();
    cfg.mosi_io = RLCD_MOSI_PIN;
    cfg.sclk_io = RLCD_SCK_PIN;
    cfg.dc_io = RLCD_DC_PIN;
    cfg.cs_io = RLCD_CS_PIN;
    cfg.reset_io = RLCD_RST_PIN;
    cfg.rotation = U8G2_R1;              // landscape 400x300
    cfg.tile_buf_height = U8G2_ST7305_TILE_BUF_FULL;
    ESP_ERROR_CHECK(u8g2_st7305_init(&g_lcd, &cfg));
    g = u8g2_st7305_get_u8g2(&g_lcd);

    settings_load();
    settings_apply();
    init_input();
    buzzer_init();
    resetGame();

    int64_t last = esp_timer_get_time();
    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        int64_t now = esp_timer_get_time();
        float realDt = clampf((now - last) / 1000000.0f, 0.001f, 0.06f);
        last = now;
        g_time += realDt;
        updateStatus(realDt);

        if (g_screen_pause) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        float dt = realDt;
        if (g_hitStop > 0.0f) {
            g_hitStop -= realDt;
            dt = 0.0f;
        }

        in = readInput();
        static int64_t k4_down_us = 0;
        static bool k4_back_fired = false;
        if (!in.k4) {
            k4_down_us = 0;
            k4_back_fired = false;
        } else {
            if (k4_down_us == 0) k4_down_us = now;
            if (!k4_back_fired && g_mode != MODE_MENU && now - k4_down_us > 700000LL) {
                k4_back_fired = true;
                goBack();
                prevIn = in;
                continue;
            }
        }
        bool cd = in.k1 && in.k2;
        static bool cd_prev = false;
        if (cd && !cd_prev && !g_cmd_busy) cmd_start();
        if (!cd && cd_prev && g_cmd_recording) g_cmd_stop = true;
        cd_prev = cd;
        if (in.sw && !prevIn.sw && g_mode != MODE_MUSIC) {
            goBack();
            prevIn = in;
            continue;
        }
        buzzer_update();

        // Hidden game launch: down -> right -> B within 1.2 s, no visible entry.
        if (g_mode == MODE_MENU) {
            int64_t now_us = esp_timer_get_time();
            if (now_us - g_secret_t > 1500000LL) g_secret_step = 0;
            if (in.down && !prevIn.down) { g_secret_step = 1; g_secret_t = now_us; }
            else if (g_secret_step == 1 && in.x > 0 && prevIn.x <= 0) { g_secret_step = 2; g_secret_t = now_us; }
            else if (g_secret_step == 2 && in.k4 && !prevIn.k4) {
                g_secret_step = 0;
                resetGame();
                g_hitStop = 0; g_shake = 0; g_shakeX = g_shakeY = 0;
                playSfx(2);
                g_mode = MODE_GAME;
                prevIn = in;
            }
        }
        if (g_mode == MODE_MENU) {
            g_back_hold = 0.0f;
            handleMenuInput();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawMenu();
            presentFrame();
        } else if (g_mode == MODE_SURVEY_MENU) {
            handleSurveyMenu();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawSurveyMenu();
            presentFrame();
        } else if (g_mode == MODE_SURVEY_REPORT) {
            handleSurveyReport();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawSurveyReport();
            presentFrame();
        } else if (g_mode == MODE_SURVEY) {
            handleSurvey();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawSurvey();
            presentFrame();
        } else if (g_mode == MODE_WORD_MENU) {
            handleWordMenu();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawWordMenu();
            g_no_invert = false;
            g_dark_ui = false;
            presentFrame();
        } else if (g_mode == MODE_INFO || g_mode == MODE_HELP) {
            if (g_mode == MODE_INFO && in.up && !prevIn.up) g_info_page = (g_info_page + 3) % 4;
            if (g_mode == MODE_INFO && in.down && !prevIn.down) g_info_page = (g_info_page + 1) % 4;
            if (g_mode == MODE_INFO && in.k4 && !prevIn.k4) { g_mode = MODE_SETTINGS; prevIn = in; continue; }
            if (updateBackChord(realDt)) {
                g_mode = MODE_MENU;
                prevIn = in;
                continue;
            }
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            if (g_mode == MODE_INFO) drawInfo(); else drawHelp();
            presentFrame();
            prevIn = in;
        } else if (g_mode == MODE_WIFI) {
            handleWifiInput();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawWifi();
            presentFrame();
        } else if (g_mode == MODE_SETTINGS) {
            handleSettingsMenu();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawSettingsMenu();
            presentFrame();
        } else if (g_mode == MODE_SETTINGS_CTRL) {
            handleSettingsCtrl();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawSettingsCtrl();
            presentFrame();
    } else if (g_mode == MODE_WORD) {
            handleWordInput();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawWord();
            g_no_invert = false;
            g_dark_ui = false;
            presentFrame();
        } else if (g_mode == MODE_VOICE) {
            if (updateBackChord(realDt)) {
                g_mode = MODE_MENU;
                prevIn = in;
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                drawMenu();
                presentFrame();
            } else {
                handleVoiceInput();
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                drawVoice();
                presentFrame();
            }
        } else if (g_mode == MODE_NOTES) {
            if (updateBackChord(realDt)) {
                g_mode = MODE_MENU;
                prevIn = in;
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                drawMenu();
                presentFrame();
            } else {
                handleNotesInput();
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                drawNotes();
                presentFrame();
            }
        } else if (g_mode == MODE_PHYSICS) {
            if (updateBackChord(realDt)) {
                if (g_phys_view != 0) {
                    g_phys_view = 0;
                } else if (g_phys_cat != 0) {
                    g_phys_cat = 0;
                    g_phys_sel = 0;
                    g_phys_off = 0;
                } else {
                    g_mode = MODE_MENU;
                }
                prevIn = in;
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                if (g_mode == MODE_MENU) drawMenu(); else drawPhysicsMenu();
                presentFrame();
            } else {
                if (g_phys_view == 0) {
                    handlePhysicsMenuInput();
                } else if (g_phys_view == 1) {
                    projectileInput(realDt); projectileUpdate(realDt);
                } else if (g_phys_view == 2) {
                    springInput(realDt); springUpdate(realDt);
                } else if (g_phys_view == 3) {
                    freefallInput(realDt); freefallUpdate(realDt);
                } else if (g_phys_view == 4) {
                    uniformInput(realDt); uniformUpdate(realDt);
                } else if (g_phys_view == 5) {
                    accelInput(realDt); accelUpdate(realDt);
                } else if (g_phys_view == 6) {
                    pendulumInput(realDt); pendulumUpdate(realDt);
                } else if (g_phys_view == 7) {
                    forceInput(realDt);
                } else if (g_phys_view == 8) {
                    motionCompInput(realDt); motionCompUpdate(realDt);
                } else if (g_phys_view == 9) {
                    newtonInput(realDt); newtonUpdate(realDt);
                } else if (g_phys_view == 10) {
                    frictionInput(realDt); frictionUpdate(realDt);
                } else if (g_phys_view == 11) {
                    hookeInput(realDt);
                } else if (g_phys_view == 12) {
                    pulleyInput(realDt); pulleyUpdate(realDt);
                } else if (g_phys_view == 13) {
                    circularInput(realDt); circularUpdate(realDt);
                } else if (g_phys_view == 14) {
                    centripetalInput(realDt);
                } else if (g_phys_view == 15) {
                    workInput(realDt);
                } else if (g_phys_view == 16) {
                    kineticInput(realDt);
                } else if (g_phys_view == 17) {
                    potentialInput(realDt);
                } else if (g_phys_view == 18) {
                    energyInput(realDt); energyUpdate(realDt);
                } else if (g_phys_view == 19) {
                    powerInput(realDt);
                } else if (g_phys_view == 20) {
                    impulseInput(realDt);
                } else if (g_phys_view == 21) {
                    momentumInput(realDt);
                } else if (g_phys_view == 22) {
                    elasticInput(realDt);
                } else if (g_phys_view == 23) {
                    inelasticInput(realDt);
                } else if (g_phys_view == 24) {
                    recoilInput(realDt);
                } else if (g_phys_view == 25) {
                    ohmInput(realDt);
                } else if (g_phys_view == 26) {
                    seriesInput(realDt);
                } else if (g_phys_view == 27) {
                    parallelInput(realDt);
                } else if (g_phys_view == 28) {
                    closedInput(realDt);
                } else if (g_phys_view == 29) {
                    rheostatInput(realDt);
                } else if (g_phys_view == 30) {
                    ampereInput(realDt);
                } else if (g_phys_view == 31) {
                    faradayInput(realDt); faradayUpdate(realDt);
                } else if (g_phys_view == 32) {
                    lenzInput(realDt);
                } else if (g_phys_view == 33) {
                    waveInput(realDt); waveUpdate(realDt);
                } else if (g_phys_view == 34) {
                    interfereInput(realDt);
                } else if (g_phys_view == 35) {
                    refractInput(realDt);
                } else if (g_phys_view == 36) {
                    tirInput(realDt);
                } else if (g_phys_view == 37) {
                    lensInput(realDt);
                } else if (g_phys_view == 38) {
                    doubleSlitInput(realDt);
                } else if (g_phys_view == 39) {
                    prismInput(realDt);
                } else if (g_phys_view == 46) {
                    balanceInput(realDt);
                } else if (g_phys_view == 47) {
                    juniorWeightInput(realDt);
                } else if (g_phys_view == 48) {
                    collinearInput(realDt);
                } else if (g_phys_view == 49) {
                    soundInput(realDt); soundUpdate(realDt);
                } else if (g_phys_view == 50) {
                    mirrorInput(realDt);
                } else if (g_phys_view == 51) {
                    buoyancyInput(realDt);
                } else if (g_phys_view == 52) {
                    leverInput(realDt);
                } else if (g_phys_view == 53) {
                    avgSpeedInput(realDt);
                } else if (g_phys_view == 54) {
                    densityInput(realDt);
                } else if (g_phys_view == 55) {
                    liquidInput(realDt);
                } else if (g_phys_view == 56) {
                    ePowerInput(realDt);
                } else if (g_phys_view == 57) {
                    chargeInput(realDt);
                } else if (g_phys_view == 58) {
                    ammeterInput(realDt);
                } else if (g_phys_view == 59) {
                    jouleInput(realDt);
                } else if (g_phys_view == 40) {
                    idealGasInput(realDt);
                } else if (g_phys_view == 41) {
                    isothermalInput(realDt);
                } else if (g_phys_view == 42) {
                    thermoInput(realDt); thermoUpdate(realDt);
                } else if (g_phys_view == 43) {
                    photoelectricInput(realDt);
                } else if (g_phys_view == 44) {
                    blackbodyInput(realDt);
                } else {
                    decayInput(realDt); decayUpdate(realDt);
                }
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                if (g_phys_view == 0) drawPhysicsMenu();
                else if (g_phys_view == 1) drawProjectile();
                else if (g_phys_view == 2) drawSpring();
                else if (g_phys_view == 3) drawFreefall();
                else if (g_phys_view == 4) drawUniform();
                else if (g_phys_view == 5) drawAccel();
                else if (g_phys_view == 6) drawPendulum();
                else if (g_phys_view == 7) drawForce();
                else if (g_phys_view == 8) drawMotionComp();
                else if (g_phys_view == 9) drawNewton();
                else if (g_phys_view == 10) drawFriction();
                else if (g_phys_view == 11) drawHooke();
                else if (g_phys_view == 12) drawPulley();
                else if (g_phys_view == 13) drawCircular();
                else if (g_phys_view == 14) drawCentripetal();
                else if (g_phys_view == 15) drawWork();
                else if (g_phys_view == 16) drawKinetic();
                else if (g_phys_view == 17) drawPotential();
                else if (g_phys_view == 18) drawEnergy();
                else if (g_phys_view == 19) drawPower();
                else if (g_phys_view == 20) drawImpulse();
                else if (g_phys_view == 21) drawMomentum();
                else if (g_phys_view == 22) drawElastic();
                else if (g_phys_view == 23) drawInelastic();
                else if (g_phys_view == 24) drawRecoil();
                else if (g_phys_view == 25) drawOhm();
                else if (g_phys_view == 26) drawSeries();
                else if (g_phys_view == 27) drawParallel();
                else if (g_phys_view == 28) drawClosed();
                else if (g_phys_view == 29) drawRheostat();
                else if (g_phys_view == 30) drawAmpere();
                else if (g_phys_view == 31) drawFaraday();
                else if (g_phys_view == 32) drawLenz();
                else if (g_phys_view == 33) drawWave();
                else if (g_phys_view == 34) drawInterfere();
                else if (g_phys_view == 35) drawRefract();
                else if (g_phys_view == 36) drawTir();
                else if (g_phys_view == 37) drawLens();
                else if (g_phys_view == 38) drawDoubleSlit();
                else if (g_phys_view == 39) drawPrism();
                else if (g_phys_view == 46) drawBalance();
                else if (g_phys_view == 47) drawJuniorWeight();
                else if (g_phys_view == 48) drawCollinear();
                else if (g_phys_view == 49) drawSound();
                else if (g_phys_view == 50) drawMirror();
                else if (g_phys_view == 51) drawBuoyancy();
                else if (g_phys_view == 52) drawLever();
                else if (g_phys_view == 53) drawAvgSpeed();
                else if (g_phys_view == 54) drawDensity();
                else if (g_phys_view == 55) drawLiquid();
                else if (g_phys_view == 56) drawEPower();
                else if (g_phys_view == 57) drawCharge();
                else if (g_phys_view == 58) drawAmmeter();
                else if (g_phys_view == 59) drawJoule();
                else if (g_phys_view == 40) drawIdealGas();
                else if (g_phys_view == 41) drawIsothermal();
                else if (g_phys_view == 42) drawThermo();
                else if (g_phys_view == 43) drawPhotoelectric();
                else if (g_phys_view == 44) drawBlackbody();
                else drawDecay();
                presentFrame();
            }
        } else if (g_mode == MODE_FEEDBACK) {
            if (updateBackChord(realDt)) {
                g_mode = MODE_MENU;
                prevIn = in;
            }
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawFeedback();
            presentFrame();
        } else if (g_mode == MODE_VIDEO) {
            video_input(realDt);
            video_update();
            presentFrame();
        } else if (g_mode == MODE_MUSIC) {
            handleMusicInput();
            uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
            if (g_music_dirty || (int32_t)(nowMs - g_music_next_draw) >= 0) {
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                drawMusic();
                presentFrame();
                g_music_dirty = false;
                g_music_next_draw = nowMs + 125; // 8 fps UI, audio unaffected
            }
        } else if (g_mode == MODE_IMAGE) {
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawImage();
            presentFrame();
        } else if (g_mode == MODE_FILES) {
            handleFilesInput();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawFiles();
            presentFrame();
        } else if (g_mode == MODE_INFO || g_mode == MODE_HELP) {
            if (g_mode == MODE_INFO && in.up && !prevIn.up) g_info_page = (g_info_page + 3) % 4;
            if (g_mode == MODE_INFO && in.down && !prevIn.down) g_info_page = (g_info_page + 1) % 4;
            if (g_mode == MODE_INFO && in.k4 && !prevIn.k4) { g_mode = MODE_SETTINGS; prevIn = in; }
            if (updateBackChord(realDt)) {
                g_mode = MODE_MENU;
                prevIn = in;
            }
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            if (g_mode == MODE_INFO) drawInfo(); else drawHelp();
            presentFrame();
            prevIn = in;
        } else if (g_mode == MODE_WIFI) {
            handleWifiInput();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawWifi();
            presentFrame();
        } else if (g_mode == MODE_SETTINGS) {
            handleSettingsMenu();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawSettingsMenu();
            presentFrame();
        } else if (g_mode == MODE_SETTINGS_CTRL) {
            handleSettingsCtrl();
            u8g2_ClearBuffer(g);
            u8g2_SetDrawColor(g, 1);
            drawSettingsCtrl();
            presentFrame();
        } else {
            if (updateBackChord(realDt)) {
                g_mode = MODE_MENU;
                g_hitStop = 0;
                g_shake = 0;
                g_shakeX = g_shakeY = 0;
                prevIn = in;
                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                drawMenu();
                presentFrame();
            } else {
                if (!g_debug_hold) {
                    handleInput(dt);
                    updateAI(dt);
                    updateFighter(p1, dt, false);
                    updateFighter(dummy, dt, true);
                    tryHit(p1, dummy);
                    tryHit(dummy, p1);
                    updateHitFx(realDt);
                }

                if (g_shake > 0.0f) {
                    g_shake -= realDt;
                    g_shakeX = (int)(esp_random() % 5) - 2;
                    g_shakeY = (int)(esp_random() % 3) - 1;
                } else {
                    g_shakeX = 0;
                    g_shakeY = 0;
                }

                u8g2_ClearBuffer(g);
                u8g2_SetDrawColor(g, 1);
                drawArena();
                if (p1.state == 6) drawInkSprite(p1);
                Pose pp = poseFor(p1);
                drawFighter(p1, pp, false);
                if (dummy.state == 6) drawInkSprite(dummy);
                Pose pd = poseFor(dummy);
                drawFighter(dummy, pd, true);
                drawSpecialFx(p1);
                drawSpecialFx(dummy);

                drawHitFx();
                drawImpactText();

                if (ko) {
                    u8g2_SetBitmapMode(g, 1);
                    if (winner == 1) {
                        u8g2_DrawBitmap(g, (400 - 104) / 2, 64, 13,
                                        WIN_GLYPH_H, WIN_GLYPH);
                    } else {
                        u8g2_DrawBitmap(g, (400 - 104) / 2, 64, 13,
                                        WIN_GLYPH_H, LOSE_GLYPH);
                    }
                    u8g2_SetBitmapMode(g, 0);
                }

                drawHud();
                presentFrame();
            }
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(40)); // ~25 fps target
    }
}


#if AUDIO_PROBE2
static void audio_probe_run() {
    if (!g_codec || !g_audio_buf) return;
    uint8_t *wav = g_audio_buf;
    memset(wav, 0, 44 + 192000);
    memcpy(wav, "RIFF", 4);
    wav_u32(wav + 4, 36 + 192000);
    memcpy(wav + 8, "WAVEfmt ", 8);
    wav_u32(wav + 16, 16);
    wav_u16(wav + 20, 1);
    wav_u16(wav + 22, 2);
    wav_u32(wav + 24, 16000);
    wav_u32(wav + 28, 16000 * 4);
    wav_u16(wav + 32, 4);
    wav_u16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    wav_u32(wav + 40, 192000);

    g_voice_state = 1;
    g_read_ret = g_codec->CodecPort_EchoRead(wav + 44, 192000);
    g_read_calls = 1;
    g_voice_bytes = (g_read_ret == 0) ? 192000 : 0;
    int16_t *s = (int16_t *)(wav + 44);
    int peak = 0;
    for (int i = 0; i < 192000 / 2; ++i) {
        int v = s[i] < 0 ? -s[i] : s[i];
        if (v > peak) peak = v;
    }
    g_voice_peak = peak;
    snprintf(g_voice_msg, sizeof(g_voice_msg), "PROBE %d PEAK %d", g_read_ret, peak);
    ESP_LOGI(TAG, "probe read=%d peak=%d", g_read_ret, peak);

    g_ota_started = true;
    ota_start();
    vTaskDelay(pdMS_TO_TICKS(8000));

    esp_http_client_config_t cfg = {};
    cfg.url = "http://192.168.3.39:8765/voice";
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 180000;
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return;
    esp_http_client_set_header(cli, "Content-Type", "audio/wav");
    esp_http_client_set_header(cli, "X-Device", "rlcd-stickman-probe");
    size_t total = 44 + 192000;
    if (esp_http_client_open(cli, total) != ESP_OK) {
        esp_http_client_cleanup(cli);
        return;
    }
    size_t sent = 0;
    while (sent < total) {
        int n = esp_http_client_write(cli, (char *)wav + sent, total - sent);
        if (n <= 0) break;
        sent += n;
    }
    int code = 0;
    if (esp_http_client_fetch_headers(cli) >= 0) {
        code = esp_http_client_get_status_code(cli);
    }
    char resp[128] = {0};
    esp_http_client_read(cli, resp, sizeof(resp) - 1);
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    g_voice_state = 4;
    snprintf(g_voice_msg, sizeof(g_voice_msg), "PROBE %d PEAK %d HTTP %d", g_read_ret, peak, code);
    ESP_LOGI(TAG, "probe http=%d sent=%u", code, (unsigned)sent);
}

static void audio_probe_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    audio_probe_run();
    vTaskDelay(portMAX_DELAY);
}
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "stickman RLCD game proto");
#if AUDIO_PROBE2
    audio_init();
    audio_open_late();
    xTaskCreatePinnedToCore(audio_probe_task, "audio_probe", 8192, NULL, 4, NULL, 1);
    vTaskDelay(portMAX_DELAY);
#else
    audio_init();
    audio_open_late();
    // NVS and WiFi must be initialized before the UI task calls settings_load().
    g_ota_started = true;
    ota_start();
    BaseType_t ok = xTaskCreatePinnedToCore(gameTask, "game", 16384, NULL, 4, NULL, 1);
    configASSERT(ok == pdPASS);
    sd_ensure();
    if (g_sd_ok) cleanup_temp_files();
    g_cap_q = xQueueCreate(16, CAP_PATH_LEN);
    if (g_cap_q) {
        // Background VAD capture removed: only C+D manual recordings are uploaded.
        xTaskCreatePinnedToCore(capture_net_task, "capture_net", 4096, NULL, 2, NULL, 0);
    }
#endif
}
