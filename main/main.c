#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2s_std.h"
#include "esp_http_server.h"

// =================CONFIG=================
// Wi-Fi Configuration
#define EXAMPLE_ESP_WIFI_SSID      "UP70_2.4G"
#define EXAMPLE_ESP_WIFI_PASS      "autoup70666"
#define MAX_RETRY                  10

// Servo Configuration (GPIO 7)
#define SUB_PIN_SERVO              7
#define SERVO_MIN_PULSEWIDTH       500  // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH       2500 // Maximum pulse width in microsecond
#define SERVO_MAX_DEGREE           180  // Maximum angle in degree we can control
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (SUB_PIN_SERVO) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_FREQUENCY          (50) // Frequency in Hertz. Set frequency at 50 Hz

// I2S Configuration (INMP441 Mic & MAX98357A Speaker)
#define I2S_BCK_IO              4
#define I2S_WS_IO               5
#define I2S_DO_IO               6       // To Speaker (DIN pin on MAX98357A)
#define I2S_DI_IO               1       // From Mic (SD pin on INMP441)
#define I2S_SAMPLE_RATE         16000
#define WAVE_FREQ_HZ            440     // Beep frequency
#define VOLUME_THRESHOLD        50000000 // Threshold for "clap" detection (Adjust based on mic sensitivity/bit depth)

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

// Forward Declaration
static void play_tone(int freq, int duration_ms);

static const char *TAG = "SMART_LIGHT";
static int s_retry_num = 0;

// =================SERVO=================
static void servo_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 50 Hz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

// angle: 0-180
static void servo_set_angle(int angle)
{
    if(angle > SERVO_MAX_DEGREE) angle = SERVO_MAX_DEGREE;
    if(angle < 0) angle = 0;

    // Calculate duty cycle: 
    // Pulse width: 500us to 2500us
    // Period: 20000us (50Hz)
    // Resolution: 13 bit (8192)
    
    uint32_t pulse_width = SERVO_MIN_PULSEWIDTH + (((SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) * angle) / SERVO_MAX_DEGREE);
    
    // Duty = (pulse_width / 20000) * 8192
    uint32_t duty = (pulse_width * 8192) / 20000;

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
    ESP_LOGI(TAG, "Servo set to %d degrees (duty: %lu)", angle, duty);
}

// Action Helpers
void light_on() {
    ESP_LOGI(TAG, "Turning Light ON (Servo 90)");
    servo_set_angle(90); // Adjust angle as needed for your switch
    // Play Success Sound (Rising)
    play_tone(440, 100);
    play_tone(880, 200);
}

void light_off() {
    ESP_LOGI(TAG, "Turning Light OFF (Servo 0)");
    servo_set_angle(0); // Adjust angle as needed
    // Play Off Sound (Falling)
    play_tone(880, 100);
    play_tone(440, 200);
}

// =================I2S Audio=================
static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    // INMP441 uses "I2S Philips" standard (left channel low, right channel high, 1-bit delay)
    // MAX98357A also uses "I2S Philips" standard.
    // The default I2S_STD_SLOT_DEFAULT_CONFIG is I2S Philips.

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S Initialized.");
}

static void play_tone(int freq, int duration_ms)
{
    // Generate sine wave
    size_t bytes_written;
    int samples_count = (I2S_SAMPLE_RATE * duration_ms) / 1000;
    int32_t *samples = (int32_t *)malloc(samples_count * 2 * sizeof(int32_t)); // Stereo (2 channels)
    
    // Scale amplitude (avoid clipping, MAX98357A is powerful)
    int32_t amplitude = 20000000; // ~10% of 31-bit max, adjust volume here

    for (int i = 0; i < samples_count; i++) {
        double t = (double)i / I2S_SAMPLE_RATE;
        int32_t sample_val = (int32_t)(amplitude * sin(2 * M_PI * freq * t));
        
        // Stereo - Duplicate for Left/Right
        samples[i * 2] = sample_val;     // Left
        samples[i * 2 + 1] = sample_val; // Right
    }

    i2s_channel_write(tx_handle, samples, samples_count * 2 * sizeof(int32_t), &bytes_written, portMAX_DELAY);
    free(samples);
}

// Task to monitor audio levels
void audio_monitor_task(void *arg)
{
    // Read buffer
    size_t bytes_read;
    int32_t *read_buf = (int32_t *)malloc(1024 * sizeof(int32_t)); // 256 stereo samples ? No, 1024 bytes -> 256 samples (if 4 bytes)
    // Actually we allocate space for say 256 stereo samples (256 * 2 * 4 bytes = 2048 bytes)
    size_t buf_len_bytes = 1024;
    size_t samples_to_read = buf_len_bytes / 4; // Total samples (L+R)

    int light_state = 0;
    ESP_LOGI(TAG, "Starting Audio Monitor...");

    while (1) {
        if (i2s_channel_read(rx_handle, read_buf, buf_len_bytes, &bytes_read, 1000 / portTICK_PERIOD_MS) == ESP_OK) {
            
            int64_t sum_sq = 0;
            int sample_count = bytes_read / 4; // 32-bit samples

            for (int i = 0; i < sample_count; i++) {
                // INMP441 is 24-bit data in 32-bit slot.
                int32_t sample = read_buf[i];
                // Check if we need to shift (sometimes data is in upper 24 bits)
                // Assuming signed 32-bit integer, standard I2S reads correctly into MSB usually if configured right.
                // Just prevent overflow in square
                // We can use a rough energy estimate
                if (sample < 0) sample = -sample;
                sum_sq += (int64_t)sample; // Just Sum of Absolute values (Approx for speed/simplicity) or Square
            }
            
            // Average amplitude
            int32_t avg_amp = sum_sq / sample_count;

            // Simple "Clap" detection (Threshold needs tuning!)
            if (avg_amp > VOLUME_THRESHOLD) {
                ESP_LOGI(TAG, "Loud Noise Detected! Amp: %ld", avg_amp);
                
                // Toggle Light
                light_state = !light_state;
                if (light_state) {
                    light_on();
                } else {
                    light_off();
                }

                // Debounce
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }
    free(read_buf);
}


// =================HTTP SERVER=================
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* resp_str = 
        "<!DOCTYPE html>"
        "<html>"
        "<head><style>"
        "body { font-family: sans-serif; text-align: center; margin-top: 50px; }"
        "button { padding: 20px 40px; font-size: 24px; margin: 20px; cursor: pointer; }"
        ".on { background-color: #4CAF50; color: white; border: none; }"
        ".off { background-color: #f44336; color: white; border: none; }"
        "</style></head>"
        "<body>"
        "<h1>Dormitory Light Control</h1>"
        "<button class='on' onclick=\"location.href='/on'\">Turn ON</button>"
        "<button class='off' onclick=\"location.href='/off'\">Turn OFF</button>"
        "</body></html>";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t on_get_handler(httpd_req_t *req)
{
    light_on();
    // Redirect back to root
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<script>window.location.href='/';</script>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t off_get_handler(httpd_req_t *req)
{
    light_off();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<script>window.location.href='/';</script>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t on = { .uri = "/on", .method = HTTP_GET, .handler = on_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &on);

        httpd_uri_t off = { .uri = "/off", .method = HTTP_GET, .handler = off_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &off);
    }
    return server;
}


// =================WIFI=================
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            ESP_LOGI(TAG, "connect to the AP fail");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        // Start Server once connected
        start_webserver();
    }
}

void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Hardware
    servo_init();
    
    // Initialize I2S
    i2s_init();
    
    // Play startup sound
    play_tone(440, 200);
    play_tone(880, 200);

    // Start Audio Monitor Task
    xTaskCreate(audio_monitor_task, "audio_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}
