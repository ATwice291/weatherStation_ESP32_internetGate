#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_sntp.h"
#include <esp_netif_sntp.h>
#include "esp_http_client.h"
#include "cJSON.h"

#define BLINK_GPIO 2

static const char *TAG = "wifi_scan";
char weatherDescription[64];
uint8_t iconIndex;
int8_t temperature;
uint8_t humidity;
uint16_t pressure;
uint8_t windDegrees;
uint16_t windSpeed;

typedef struct {
    uint8_t hours;
    int8_t temperature;
    uint8_t iconIndex;
    uint16_t precipitation;
}Forecast;

Forecast forecast[4];
uint8_t forecastElements;

#define CONFIG_SNTP_TIME_SERVER  "pool.ntp.org"

const char *api_key = "put your key here";
const char *city = "your city"; 
const char *lat = "lat %.3f"; 
const char *lon = "lon %.3f"; 

extern esp_err_t example_wifi_connect(void);
extern void example_wifi_shutdown(void);

static void obtain_time(void);

void wifi_scan_task(void *pvParameter) {
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while (1) {
        gpio_set_level(BLINK_GPIO, 0);
        obtain_time();  
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

void print_task(void *pvParameter) {
    struct tm timeinfo = { 0 };
    time_t now = 0;
    char strftime_buf[64];

    while (1) {
        time(&now);
        setenv("TZ", "GMT-4", 1);
        tzset();
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI("TIME", " %s", strftime_buf);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void can_init(void) {
    gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_21, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GPIO_NUM_21, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NO_ACK);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI("CAN", "CAN started");
    twai_status_info_t status;
    twai_get_status_info(&status);
    ESP_LOGI("CAN", "TWAI state = %d", status.state);
        vTaskDelay(pdMS_TO_TICKS(2000));
}

void can_task(void *pvParameter) {
    twai_message_t message;
    struct tm timeInfo = { 0 };
    time_t now = 0;
    esp_err_t canError = ESP_OK;
    twai_status_info_t status;

    while (true) {
        if (twai_receive(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
            //ESP_LOGI("CAN", "---RECEIVED----");
            if (message.identifier == 0x010) {
                time(&now);
                setenv("TZ", "GMT-4", 1);
                tzset();
                localtime_r(&now, &timeInfo);
                twai_message_t reply = {
                    .identifier = 0x011,
                    .data_length_code = 7,
                    .data = {timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec, timeInfo.tm_mday, timeInfo.tm_mon, timeInfo.tm_year, timeInfo.tm_wday}
                };
                canError = twai_transmit(&reply, pdMS_TO_TICKS(10));
                //ESP_LOGI("CAN", "---ANSWER---- error =  %i", canError);
            }
            if (message.identifier == 0x012) {
                twai_message_t reply = {
                    .identifier = 0x013
                };
                uint8_t msgCounter = 0;
                uint8_t strIndex = 0;
                bool finished = false;
                //ESP_LOGI("CAN", "--------------");
                while(!finished) {
                    reply.data[0] = msgCounter;
                    uint8_t i = 1;
                    for(i = 1; i < 8; ++i) {
                        reply.data[i] = (uint8_t)weatherDescription[strIndex++];
                        if(reply.data[i] == 0 || strIndex == 63) {
                            finished = true;
                            ++i;
                            break;
                        }
                    }
                    reply.data_length_code = i;
                    canError = twai_transmit(&reply, pdMS_TO_TICKS(10));
                    msgCounter++;
                }
                //ESP_LOGI("CAN", "---ANSWER---- error =  %i", canError);
            }
            if (message.identifier == 0x014) {
                twai_message_t reply = {
                    .identifier = 0x015,
                    .data_length_code = 8,
                    .data = {iconIndex, temperature, humidity, (pressure>>8), (pressure&0xFF), windDegrees, (windSpeed>>8), (windSpeed&0xFF)}
                };
                canError = twai_transmit(&reply, pdMS_TO_TICKS(10));
            }
            if (message.identifier == 0x016) {
                twai_message_t reply = {
                    .identifier = 0x017
                };
                for(uint8_t i = 0; i < forecastElements; ++i) {
                    reply.data[0] = i;
                    reply.data[1] = forecast[i].hours;
                    reply.data[2] = forecast[i].iconIndex;
                    reply.data[3] = forecast[i].temperature;
                    reply.data[4] = forecast[i].precipitation>>8;
                    reply.data[5] = forecast[i].precipitation&0xFF;
                    reply.data_length_code = 6;
                    canError = twai_transmit(&reply, pdMS_TO_TICKS(10));
                }
            }
        }
        if(canError != 0) {
            ESP_LOGI("CAN", "RESTART");
            twai_stop();
            ESP_ERROR_CHECK(twai_start());
        }
        twai_get_status_info(&status);
        if(status.state != 1) {
            ESP_LOGI("CAN", "RESTART, state = %d", status.state);
            twai_stop();
            ESP_ERROR_CHECK(twai_start());
        }
        
    }
}

void app_main(void) {
    can_init();
    xTaskCreatePinnedToCore(can_task,       "CAN Task",   4096, NULL, 3, NULL, 0);
    //xTaskCreatePinnedToCore(print_task,     "Print Task",   2048, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan_task", 8192, NULL, 4, NULL, 1);
    
}


#define HTTP_RESPONSE_BUFFER_SIZE 1024

char *response_data = NULL;
size_t response_len = 0;
bool all_chunks_received = false;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Resize the buffer to fit the new chunk of data
            response_data = realloc(response_data, response_len + evt->data_len);
            memcpy(response_data + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            break;
        case HTTP_EVENT_ON_FINISH:
            all_chunks_received = true;
            //ESP_LOGI("OpenWeatherAPI", "Received data: %s", response_data);
            cJSON *json = cJSON_Parse(response_data);
            if (json) {
                cJSON *weather_array  = cJSON_GetObjectItem(json, "weather");
                if (weather_array && cJSON_IsArray(weather_array)) {
                    ESP_LOGI("API", "weather_array ARRAY");
                    cJSON *first_item = cJSON_GetArrayItem(weather_array, 0);
                    if(first_item) {
                        cJSON *description = cJSON_GetObjectItem(first_item, "description");
                        ESP_LOGI("W", "%s", description->valuestring);
                        memcpy(weatherDescription, description->valuestring, 64);
                        weatherDescription[63] = 0;
                        cJSON *icon = cJSON_GetObjectItem(first_item, "icon");
                        ESP_LOGI("W", "icon - %s", icon->valuestring);
                        iconIndex = (icon->valuestring[0]-'0')*10 + (icon->valuestring[1]-'0') + (icon->valuestring[2]=='n'?128:0);
                    }
                    cJSON *main = cJSON_GetObjectItem(json, "main");
                    if (main) {
                        double temp = cJSON_GetObjectItem(main, "temp")->valuedouble;
                        temperature = (temp + 0.5);
                        ESP_LOGI("W", "Temperature: %.1f°C", temp);
                        temp = cJSON_GetObjectItem(main, "pressure")->valuedouble;
                        pressure = temp;
                        ESP_LOGI("W", "Pressure: %fhPa", temp);
                        temp = cJSON_GetObjectItem(main, "humidity")->valuedouble;
                        humidity = temp;
                        ESP_LOGI("W", "Humidity: %f%%", temp);
                    }
                    cJSON *wind = cJSON_GetObjectItem(json, "wind");
                    if(wind) {
                        double temp = cJSON_GetObjectItem(wind, "speed")->valuedouble;
                        windSpeed = (temp*10.0);
                        ESP_LOGI("W", "Wind speed: %.2fm/s", temp);
                        temp = cJSON_GetObjectItem(wind, "deg")->valuedouble;
                        windDegrees = (temp / 10);
                        ESP_LOGI("W", "Wind degrees: %f", temp); //230 - SW, 240 - WSW
                    }
                } else {
                    cJSON *cnt  = cJSON_GetObjectItem(json, "cnt");
                    if(cnt) {
                        ESP_LOGI("API", "FORECAST");
                        forecastElements = cnt->valueint;
                        if(forecastElements>4) forecastElements = 4;
                        cJSON *list  = cJSON_GetObjectItem(json, "list");
                        if (cJSON_IsArray(list)) {
                            for(uint8_t i = 0; i < forecastElements; ++i) {
                                cJSON *item = cJSON_GetArrayItem(list, i);
                                if(item) {
                                    int tempI = cJSON_GetObjectItem(item, "dt")->valueint;
                                    
                                    struct tm timeinfo = { 0 };
                                    time_t hours = tempI;
                                    setenv("TZ", "GMT-4", 1);
                                    tzset();
                                    localtime_r(&hours, &timeinfo);

                                    forecast[i].hours = timeinfo.tm_hour;
                                    cJSON *itemMain = cJSON_GetObjectItem(item, "main");
                                    double tempD = cJSON_GetObjectItem(itemMain, "temp")->valuedouble;
                                    forecast[i].temperature = (tempD + 0.5);
                                    cJSON *itemWeather = cJSON_GetObjectItem(item, "weather");
                                    cJSON *itemWeatherFirst = cJSON_GetArrayItem(itemWeather, 0);
                                    cJSON *icon = cJSON_GetObjectItem(itemWeatherFirst, "icon");
                                    forecast[i].iconIndex = (icon->valuestring[0]-'0')*10 + (icon->valuestring[1]-'0') + (icon->valuestring[2]=='n'?128:0);
                                    tempI = 0;
                                    cJSON *itemRain = cJSON_GetObjectItem(item, "rain");
                                    if(itemRain) {
                                        tempI += cJSON_GetObjectItem(itemRain, "3h")->valuedouble * 100;
                                    }
                                    forecast[i].precipitation = tempI;

                                    ESP_LOGI("F", "%02d.00 : temp %d, rain %d.%02d", forecast[i].hours, forecast[i].temperature, forecast[i].precipitation/100, forecast[i].precipitation%100);
                                }
                            }
                        }
                    }
                }
                
                cJSON_Delete(json);
            }
            free(response_data);
            response_data = NULL;
            response_len = 0;

            break;
        default:
            break;
    }
    return ESP_OK;
}

void fetch_weather() {
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=metric",
             city, api_key);
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .method = HTTP_METHOD_GET
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
    } else {
        ESP_LOGE("weather", "HTTP request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void fetch_forecast() {
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/forecast?q=%s&appid=%s&cnt=4&units=metric",
             city, api_key);
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .method = HTTP_METHOD_GET
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
    } else {
        ESP_LOGE("weather", "HTTP request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}


static void obtain_time(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    ESP_ERROR_CHECK(example_wifi_connect());

    vTaskDelay(pdMS_TO_TICKS(2000));
    
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    config.smooth_sync = true;

    esp_netif_sntp_init(&config);

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(1000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        //ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    
    fetch_weather();
    fetch_forecast();

    example_wifi_shutdown();
    esp_netif_sntp_deinit();
    esp_event_loop_delete_default();
    esp_netif_deinit();
    nvs_flash_deinit();
}