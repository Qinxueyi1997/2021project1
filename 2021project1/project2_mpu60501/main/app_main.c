/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "driver/gpio.h"
#include "driver/adc.h"

#include "driver/i2c.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "protocol_examples_common.h"
#include "esp_smartconfig.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include"cJSON.h"
/***********算法相关***********/
#include "ahu_utils.h"
#include "ahu_respirate.h"
#include "ahu_statistics.h"
#include "ahu_wavelet.h"
#include "ahu_dsp.h"
/********低功耗相关头文件******/
#include "esp32/ulp.h"
#include "driver/touch_pad.h"
#include "driver/rtc_io.h"
#include "soc/sens_periph.h"
#include "soc/rtc.h"
#include "sdkconfig.h"
#include "esp_http_client.h"

#include "AppConfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/task.h"
#include "i2c_application.h"
#include "i2c_driver.h"
#include "nvs_flash.h"
#include "mpu6050_application.h"

#define STACK_SIZE_2048 2048

/* Structure that will hold the TCB of the task being created. */
StaticTask_t xI2CWriteTaskBuffer;
StaticTask_t xI2CReadTaskBuffer;
StaticTask_t xMPU6050TaskBuffer;
/* Buffer that the task being created will use as its stack.  Note this is
    an array of StackType_t variables.  The size of StackType_t is dependent on
    the RTOS port. */
StackType_t xStack_I2C_Write[ STACK_SIZE_2048 ];
StackType_t xStack_I2C_Read[ STACK_SIZE_2048 ];
StackType_t xStack_MPU6050Task[ STACK_SIZE_2048 ];

//I2C 
#define I2C_SCL_IO          33                  //SCL->IO33
#define I2C_SDA_IO          32                  //SDA->IO32
#define I2C_MASTER_NUM      I2C_NUM_1           //I2C_1
#define WRITE_BIT           I2C_MASTER_WRITE    //写:0
#define READ_BIT            I2C_MASTER_READ     //读:1
#define ACK_CHECK_EN        0x1                 //主机检查从机的ACK
#define ACK_CHECK_DIS       0x0                 //主机不检查从机的ACK
#define ACK_VAL             0x0                 //应答
#define NACK_VAL            0x1                 //不应答
#define  LED_IO        21
//SHT30
#define SHT30_WRITE_ADDR    0x44                //地址 
#define CMD_FETCH_DATA_H    0x22                //循环采样，参考sht30 datasheet
#define CMD_FETCH_DATA_L    0x36
#define CID_ESP 0x02E5
//uart  gpio引脚
#define TXD_PIN (GPIO_NUM_4)
#define RXD_PIN (GPIO_NUM_5)

unsigned char sht30_buf[6]={0}; //温湿度变量
float g_temp=0.0, g_rh=0.0,pm=0.0;
uint8_t  pm_rateH,pm_rateL;
static const int RX_BUF_SIZE = 1024;//定义接收缓冲区大小

float environment[3]={0};


static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
#define  SAVE_POWER_WAIT_TIME    300//5分钟
#define EXAMPLE_ESP_MAXIMUM_RETRY  5
#define false		0
#define true		1
#define MAX_HTTP_RECV_BUFFER 512
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define start_time 20
#define stop_time 8
#define LEFT_TIME_OF_OUT_BED 30
static RTC_DATA_ATTR struct timeval sleep_enter_time;
static SemaphoreHandle_t adc_semaphore,original_semaphore,led_semaphore,mqtt_semaphore;
//wifi函数
static int s_retry_num = 0;

#define USER_ID      "HF102002-A"
#define DEVICE_ID          "SWA001"

static const char *DATA1 = "nvs_blob",*NVS_CUSTOMER = "customer data",*TAGTIME = "TIME",*TAG3 = "HTTP_CLIENT",*TAG1 = "smartconfig_example",*TAG2 = "wifi station",*TAG4 = "HTTP_CLIENT";
char *cname,*month,*waring_string="bed_off",*waring_type="bed_off",post_data1[1048],post_data2[1048]={0},wifi_flag=0,http_send_fail=0;
short sntp_init_flag=0,http_data_type_flag=0;
int  HTTP_Body_Data[6]={0,0,3,0,0,1},MQTT_Body_Data[6]={0,0,3,0,0,1};
int out_bed_flag=0,lengthtime=0,waring_flag=0,min_of_out_bed=0,current_hour=0,current_min=0;
//http_data_type_flag=1  报警信息
//http_data_type_flag=2  表示呼吸心率信息
//const char *Device_ID="SWA002";
const char *address,*webaddress[3]={"http://up.winsleep.club/abnormalAndService.action","http://up.winsleep.club/monitor.action","http://up.winsleep.club/monitorSleep.action"};
TaskHandle_t led_test_task_Handler,http_test_task_Handler,low_power_Handler,adc1_get_data_task_Handler;
nvs_handle handle;
static void low_vol_protect_task(void *arg);
static void wifi_led_test_task(void *pvParameters);//wifi断开网络状态指示灯
static void led_test_task(void *pvParameters);
static void smartconfig_example_task(void *parm);
static void peripheral_init(void);
static void peripheral_get_task(void);
static void adc_init(void);
//static EventGroupHandle_t mqtt_event_group;
//const static int CONNECTED_BIT = BIT0;
static void Get_Current_Body_Data(char *dat);
static void Get_Current_Original_Data(char *dat);
static void Get_Current_monitorSleep_Data(char *dat);
static esp_mqtt_client_handle_t mqtt_client = NULL;
static const char *TAG_MQTT = "MQTT_EXAMPLE";

extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[]   asm("_binary_howsmyssl_com_root_cert_pem_end");

/*IIC初始化*/
void i2c_init(void)
{
	//i2c配置结构体
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;                    //I2C模式
    conf.sda_io_num = I2C_SDA_IO;                   //SDA IO映射
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;        //SDA IO模式
    conf.scl_io_num = I2C_SCL_IO;                   //SCL IO映射
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;        //SCL IO模式
    conf.master.clk_speed = 100000;                 //I2C CLK频率
    i2c_param_config(I2C_MASTER_NUM, &conf);        //设置I2C
    //注册I2C服务即使能
    i2c_driver_install(I2C_MASTER_NUM, conf.mode,0,0,0);
}
/*
* sht30初始化
* @param[in]   void  		        :无
* @retval      int                  :0成功，其他失败
*/
int sht30_init(void)
{
    int ret;
    //配置SHT30的寄存器
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();                                   //新建操作I2C句柄
    i2c_master_start(cmd);                                                          //启动I2C
    i2c_master_write_byte(cmd, SHT30_WRITE_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);    //发地址+写+检查ack
    i2c_master_write_byte(cmd, CMD_FETCH_DATA_H, ACK_CHECK_EN);                     //发数据高8位+检查ack
    i2c_master_write_byte(cmd, CMD_FETCH_DATA_L, ACK_CHECK_EN);                     //发数据低8位+检查ack
    i2c_master_stop(cmd);                                                           //停止I2C
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_RATE_MS);        //I2C发送
    i2c_cmd_link_delete(cmd);                                                       //删除I2C句柄
    return ret;
}

/*
* sht30校验算法
*/
unsigned char SHT3X_CalcCrc(unsigned char *data, unsigned char nbrOfBytes)
{
	unsigned char bit;        // bit mask
    unsigned char crc = 0xFF; // calculated checksum
    unsigned char byteCtr;    // byte counter
    unsigned int POLYNOMIAL =  0x131;           // P(x) = x^8 + x^5 + x^4 + 1 = 100110001

    // calculates 8-Bit checksum with given polynomial
    for(byteCtr = 0; byteCtr < nbrOfBytes; byteCtr++) {
        crc ^= (data[byteCtr]);
        for(bit = 8; bit > 0; --bit) {
            if(crc & 0x80) {
                crc = (crc << 1) ^ POLYNOMIAL;
            }  else {
                crc = (crc << 1);
            }
        }
    }
	return crc;
}
/*
* sht30数据校验
*/
unsigned char SHT3X_CheckCrc(unsigned char *pdata, unsigned char nbrOfBytes, unsigned char checksum)
{
    unsigned char crc;
	crc = SHT3X_CalcCrc(pdata, nbrOfBytes);// calculates 8-Bit checksum
    if(crc != checksum) 
    {   
        return 1;           
    }
    return 0;              
}

int sht30_get_value(void)
{
    int ret;
    //配置SHT30的寄存器
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();                                   //新建操作I2C句柄
    i2c_master_start(cmd);                                                          //启动I2C
    i2c_master_write_byte(cmd, SHT30_WRITE_ADDR << 1 | READ_BIT, ACK_CHECK_EN);     //发地址+读+检查ack
    i2c_master_read_byte(cmd, &sht30_buf[0], ACK_VAL);                               //读取数据+回复ack
    i2c_master_read_byte(cmd, &sht30_buf[1], ACK_VAL);                               //读取数据+回复ack
    i2c_master_read_byte(cmd, &sht30_buf[2], ACK_VAL);                               //读取数据+回复ack
    i2c_master_read_byte(cmd, &sht30_buf[3], ACK_VAL);                               //读取数据+回复ack
    i2c_master_read_byte(cmd, &sht30_buf[4], ACK_VAL);                               //读取数据+回复ack
    i2c_master_read_byte(cmd, &sht30_buf[5], NACK_VAL);                              //读取数据+不回复ack
    i2c_master_stop(cmd);                                                            //停止I2C
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_RATE_MS);         //I2C发送
    if(ret!=ESP_OK)
    {
        return ret;
    }
    i2c_cmd_link_delete(cmd);                                                       //删除I2C句柄
    //校验读出来的数据，算法参考sht30 datasheet
    if( (!SHT3X_CheckCrc(sht30_buf,2,sht30_buf[2])) && (!SHT3X_CheckCrc(sht30_buf+3,2,sht30_buf[5])) )
    {
        ret = ESP_OK;//成功
    }
    else
    {
        ret = 1;
    }
    return ret;
}
void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
/*创建一个接收数据函数*/
static void rx_task(void *arg)//创建接收数据函数
{
    uint8_t senddata[9] = {0xff,0x01,0x86,0x00,0x00,0x00,0x00,0x00,0x79};//发送请求命令
    uart_write_bytes(UART_NUM_1, (char *)senddata, 9);//进行数据发送
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 1000 / portTICK_RATE_MS);//读取串口数据
        if (rxBytes > 0) {//判断数据是否接收成功
            data[rxBytes] = 0;//在接收数据末尾加上停止校验位
            pm_rateH=data[2];
            pm_rateL=data[3];
            pm=pm_rateH<<8|pm_rateL;
           vTaskDelay(100/portTICK_RATE_MS);
          }
    free(data);
    vTaskDelete(NULL);
}
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG4, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG4, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG4, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG4, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
           ESP_LOGD(TAG4, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
             //ESP_LOGI(TAG, "Receivedata=  %s            \n", (char*)evt->data);
             printf("%.*s   \n", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG4, "HTTP_EVENsT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG4, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG4, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG4, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static void http_rest_with_url(void)
{
    esp_http_client_config_t config = {
        .url = address,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char post_data1[1024]={0};
    // POST
    memcpy(post_data1, post_data2,strlen(post_data2));
    esp_http_client_set_url(client,address);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data1, strlen(post_data1));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG4, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
                  http_send_fail=0;
    } else {
        ESP_LOGE(TAG4, "HTTP POST request failed: %s", esp_err_to_name(err));
         xTaskCreate(&wifi_led_test_task, "led_test_task", 2048, NULL, 11, NULL);//数据发送不成功 ，灯快速闪动
        http_send_fail++;
        if(http_send_fail==20)
        {
            http_send_fail=0;
            esp_restart();
        }
    }

    esp_http_client_cleanup(client);
}



static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_publish(client, "/products/sht30", "HELLO,FRIEND", 0, 0, 0);
            ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
    };
        ESP_LOGI(TAG_MQTT, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
        esp_mqtt_client_start(mqtt_client);
}

static void mqtt_task(void *Pvparamters)
{
while(1){

        peripheral_get_task();
        vTaskDelay(5000/portTICK_RATE_MS);
                        
        sprintf(post_data2,"TEMP:%4.2f  *C HUM: %4.2f  *RH",environment[0],environment[1]);
        printf("post_data2:%s\n",post_data2);
        int  msg_id = esp_mqtt_client_publish(mqtt_client, "/products/sht30",post_data2, 0, 0, 0);
        ESP_LOGI(TAG_MQTT, "[%d] Publishing...", msg_id);   
        memset(post_data2,0,strlen(post_data2));  
                  
                     
                 
    
      
    }
    vTaskDelete(NULL);
            
}

static void event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data)
{  nvs_handle handle;
    esp_err_t err;
    static const char *NVS_CUSTOMER = "customer data";
    wifi_config_t wifi_config_stored;
    memset(&wifi_config_stored, 0x0, sizeof(wifi_config_stored));
    uint32_t len = sizeof(wifi_config_stored);
    	// Open
    err =nvs_open(NVS_CUSTOMER, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {//第一步执行
         err = nvs_get_blob(handle, DATA1, &wifi_config_stored, &len);
	    if (err == ESP_ERR_NVS_NOT_FOUND) 
		{wifi_flag=0;
		 xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
		}
		else if(err == ESP_OK)
		{wifi_flag=1;
          nvs_close(handle);
          esp_wifi_connect();
		}
        //创建smartconfig任务
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if(wifi_flag==1)
        {
               if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG1, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); 
              /****************清除nvs_flash*************/
             ESP_ERROR_CHECK( nvs_open( NVS_CUSTOMER, NVS_READWRITE, &handle) );
             ESP_ERROR_CHECK(  nvs_erase_key(handle,DATA1));
             vTaskDelay(1000 / portTICK_PERIOD_MS);
             ESP_ERROR_CHECK( nvs_erase_all(handle));
             vTaskDelay(1000 / portTICK_PERIOD_MS);
             ESP_ERROR_CHECK( nvs_commit(handle) );
             vTaskDelay(1000 / portTICK_PERIOD_MS);
             nvs_close(handle); 
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
             }
            ESP_LOGI(TAG1,"connect to the AP fail");
            }
        else{//断线重连  密码错误尝试重新链接
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);//清除标志位
         ESP_LOGI(TAG1, "断线重连.........");
         esp_restart();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {//获取到wifi信息后，执行， 第七步
             if(wifi_flag==1)
                      {
                           ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                           ESP_LOGI(TAG2, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
                           s_retry_num = 0;
                           xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                      }else{
                           xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);}
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {//等待配网 第四步，等待手机配网
        ESP_LOGI(TAG1, "Scan done");
        xTaskCreate(&led_test_task, "led_test_task", 2048, NULL, 10, &led_test_task_Handler); 
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {//扫描信道 第五步，找到udp广播
        ESP_LOGI(TAG1, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) { //获取到ssid和密码   第六步，获取 wifi信息
        ESP_LOGI(TAG1, "Got SSID and password");
        vTaskSuspend(led_test_task_Handler);
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }
        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        //打印账号密码
        ESP_LOGI(TAG1, "SSID:%s", ssid);
        ESP_LOGI(TAG1, "PASSWORD:%s", password);
        //断开默认的
        ESP_ERROR_CHECK( esp_wifi_disconnect() );
         //设置获取的ap和密码到寄存器
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        //将获取的密码保存起来 nvs_flash
        ESP_ERROR_CHECK( nvs_open( NVS_CUSTOMER, NVS_READWRITE, &handle) );
        ESP_ERROR_CHECK( nvs_set_blob( handle, DATA1, &wifi_config, sizeof(wifi_config)) );
        ESP_ERROR_CHECK( nvs_commit(handle) );
        nvs_close(handle);
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}
/*初始化 wifi*/
static void initialise_wifi(void)
{   
    wifi_event_group = xEventGroupCreate();//创建一个事件组
     s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    
    ESP_ERROR_CHECK(esp_event_loop_create_default());//wifi事件
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );  //获取链接信息
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );//获取链接信息
    if(wifi_flag!=1)ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );     //获取链接信息
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );//设置模式
    if(wifi_flag==1){
     nvs_handle handle;
    static const char *NVS_CUSTOMER = "customer data";
    static const char *DATA1 = "nvs_blob";
    wifi_config_t wifi_config_stored;
    memset(&wifi_config_stored, 0x0, sizeof(wifi_config_stored));
    uint32_t len = sizeof(wifi_config_stored);
   ESP_ERROR_CHECK( nvs_open(NVS_CUSTOMER, NVS_READWRITE, &handle) );
   ESP_ERROR_CHECK ( nvs_get_blob(handle, DATA1, &wifi_config_stored, &len) );
   nvs_close(handle);
   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config_stored) );
   wifi_flag=1;}
    ESP_ERROR_CHECK( esp_wifi_start() );//启动wifi
            ESP_LOGI(TAG1, "wifi_init_sta finished.");
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
            if (bits & WIFI_CONNECTED_BIT) {} 
            else if (bits & WIFI_FAIL_BIT) {} 
    else ESP_LOGE(TAG1, "UNEXPECTED EVENT");
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);
}
   void smartconfig_example_task(void *parm)
{
    EventBits_t uxBits;
    //使用ESP-TOUCH配置
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));//第二步执行
    //开始sc
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();//建立长链接，获取wifi信息
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1)
    { //死等事件组：CONNECTED_BIT | ESPTOUCH_DONE_BIT，第三步执行，死等，等待事件组
        uxBits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
            //连上ap
        if (uxBits & CONNECTED_BIT) //第八步
        { 
            ESP_LOGI(TAG1, "WiFi Connected to ap");
        }
        //sc结束
        if (uxBits & ESPTOUCH_DONE_BIT)//第九步
        {      ESP_LOGI(TAG1, "smartconfig over");
               esp_smartconfig_stop();
               ESP_LOGI(TAG1, "smartconfig 里面的任务");
               vTaskDelete(led_test_task_Handler);
                mqtt_app_start();
           //     xTaskCreate(&mqtt_task,"mqtt_task",8192,NULL,6,NULL);
                vTaskDelete(NULL);     
        }
          vTaskDelay(1 / portTICK_PERIOD_MS);///freertos里面的延时函数
    }
}                     

void app_main(void)
{
    ESP_LOGI(TAG_MQTT, "[APP] Startup..");
    ESP_LOGI(TAG_MQTT, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG_MQTT, "[APP] IDF version: %s", esp_get_idf_version());
    gpio_pad_select_gpio(LED_IO);
    gpio_set_direction(LED_IO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_IO, 0);
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);
      adc_semaphore = xSemaphoreCreateBinary();
    //要使用信号量达到两个任务先后执行，比如任务A执行初始化以后，给出信号量消息，然后任务B才运行
    if (!adc_semaphore) {
        ESP_LOGE(TAG1, "%s, init fail, the adc semaphore create fail.", __func__);
        return;
    }    
     led_semaphore = xSemaphoreCreateBinary();
    //要使用信号量达到两个任务先后执行，比如任务A执行初始化以后，给出信号量消息，然后任务B才运行
    if (!led_semaphore) {
        ESP_LOGE(TAG1, "%s, init fail, the adc semaphore create fail.", __func__);
        return;
    }

     original_semaphore = xSemaphoreCreateBinary();
    //要使用信号量达到两个任务先后执行，比如任务A执行初始化以后，给出信号量消息，然后任务B才运行
    if (!original_semaphore) {
        ESP_LOGE(TAG1, "%s, init fail, the adc semaphore create fail.", __func__);
        return;
     }
          mqtt_semaphore = xSemaphoreCreateBinary();
    //要使用信号量达到两个任务先后执行，比如任务A执行初始化以后，给出信号量消息，然后任务B才运行
    if (!mqtt_semaphore) {
        ESP_LOGE(TAG1, "%s, init fail, the adc semaphore create fail.", __func__);
        return;
     }
   // ESP_ERROR_CHECK(nvs_flash_init());
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    initialise_wifi();
    // Initializations

	vI2CInit();

    xTaskCreateStaticPinnedToCore(vI2CWrite,            /* Function that implements the task. */
                                  "vI2CWrite",          /* Text name for the task. */
                                  STACK_SIZE_2048,      /* Number of indexes in the xStack array. */
                                  NULL,                 /* Parameter passed into the task. */
                                  osPriorityHigh,       /* Priority at which the task is created. */
                                  xStack_I2C_Write,     /* Array to use as the task's stack. */
                                  &xI2CWriteTaskBuffer, /* Variable to hold the task's data structure. */
                                  0                     /*  0 for PRO_CPU, 1 for APP_CPU, or tskNO_AFFINITY which allows the task to run on both */
                                  );

    xTaskCreateStaticPinnedToCore(vI2CRead,            /* Function that implements the task. */
                                  "vI2CRead",          /* Text name for the task. */
                                  STACK_SIZE_2048,     /* Number of indexes in the xStack array. */
                                  NULL,                /* Parameter passed into the task. */
                                  osPriorityHigh,      /* Priority at which the task is created. */
                                  xStack_I2C_Read,     /* Array to use as the task's stack. */
                                  &xI2CReadTaskBuffer, /* Variable to hold the task's data structure. */
                                  0                    /*  0 for PRO_CPU, 1 for APP_CPU, or tskNO_AFFINITY which allows the task to run on both */
                                  );

    xTaskCreateStaticPinnedToCore(vMPU6050Task,        /* Function that implements the task. */
                                  "vMPU6050Task",      /* Text name for the task. */
                                  STACK_SIZE_2048,     /* Number of indexes in the xStack array. */
                                  NULL,                /* Parameter passed into the task. */
                                  osPriorityNormal,    /* Priority at which the task is created. */
                                  xStack_MPU6050Task,  /* Array to use as the task's stack. */
                                  &xMPU6050TaskBuffer, /* Variable to hold the task's data structure. */
                                  0                    /*  0 for PRO_CPU, 1 for APP_CPU, or tskNO_AFFINITY which allows the task to run on both */
                                  );
                                  
    mqtt_app_start();
    //xTaskCreate(&mqtt_task,"mqtt_task",8192,NULL,6,NULL);
}
static void peripheral_get_task(void)
{      
    
       if(sht30_get_value()==ESP_OK)   //获取温湿度
        {
            //算法参考sht30 datasheet
            g_temp    =( ( (  (sht30_buf[0]*256) +sht30_buf[1]) *175   )/65535.0  -45  );
            g_rh  =  ( ( (sht30_buf[3]*256) + (sht30_buf[4]) )*100/65535.0) ;
            ESP_LOGI("SHT30", "temp:%4.2f C \r\n", g_temp); //℃打印出来是乱码,所以用C
            ESP_LOGI("SHT30", "hum:%4.2f %%RH \r\n", g_rh);
        } else printf("sht30 get value failed!\r\n");
         
         vTaskDelay(100 / portTICK_PERIOD_MS);
         environment[0]=(int)g_temp;
         environment[1]=(int)g_rh;
         environment[2]=pm;
}
static void peripheral_init(void)
{  
    uart_init(); 
    i2c_init();                         //I2C初始化
    sht30_init();                       //sht30初始化
    vTaskDelay(100/portTICK_RATE_MS);   //延时100ms 
    gpio_pad_select_gpio(LED_IO); 
    gpio_set_direction(LED_IO, GPIO_MODE_OUTPUT);
    printf("peripheral init sucess\r\n");
   
}
void Get_Current_Original_Data(char *dat)
{
    cJSON *root=NULL;//定义一个指针变量
    root =cJSON_CreateObject();
    cJSON_AddStringToObject(root,"deviceId",DEVICE_ID);// 成人
    cJSON_AddNumberToObject(root,"breathFlag",HTTP_Body_Data[0]);//状态标志位 1正常 2 呼吸暂停 3低通气 4 呼吸过快 5 不通气6 在床 7 离床 8 体动
    cJSON_AddNumberToObject(root,"breathRate",HTTP_Body_Data[1]);//呼吸频率
    cJSON_AddNumberToObject(root,"breathAvg",HTTP_Body_Data[2]);//呼吸平均值
    cJSON_AddNumberToObject(root,"heartRate",HTTP_Body_Data[3]);//心率
    cJSON_AddNumberToObject(root,"heartAvg",HTTP_Body_Data[4]);//心率平均值
    cJSON_AddNumberToObject(root,"bodyTP",HTTP_Body_Data[5]);//体温
    cJSON_AddStringToObject(root,"pushTime",cname);
    char *out=cJSON_PrintUnformatted(root);
    memcpy(dat,out,strlen(out));//将整合的数据copy给dat
    cJSON_Delete(root);
    free(out);
}

void Get_Current_Body_Data(char *dat)
{
    cJSON *root=NULL;//定义一个指针变量
    cJSON *array=NULL;
    root =cJSON_CreateObject();
    cJSON_AddStringToObject(root,"product_model","MS001");// 成人
    cJSON_AddStringToObject(root,"user-id",USER_ID);// 成人
    cJSON_AddNumberToObject(root,"breath",MQTT_Body_Data[0]);//呼吸
    cJSON_AddNumberToObject(root,"heart",MQTT_Body_Data[1]);//心率
    cJSON_AddNumberToObject(root,"sleepFlag",MQTT_Body_Data[2]);//睡眠标志位
    cJSON_AddItemToObject(root,"waring",array=cJSON_CreateArray());  
    cJSON *ArrayItem0=cJSON_CreateObject();
     cJSON_AddStringToObject(ArrayItem0,"t",waring_string);
     cJSON_AddNumberToObject(ArrayItem0,"w",waring_flag);
    cJSON_AddItemToArray(array,ArrayItem0);

    cJSON_AddStringToObject(root,"time",cname);
    char *out=cJSON_PrintUnformatted(root);
    memcpy(dat,out,strlen(out));//将整合的数据copy给dat
    cJSON_Delete(root);
    free(out);
}

void Get_Current_monitorSleep_Data(char *dat)
{
    cJSON *root=NULL;//定义一个指针变量
    cJSON *array=NULL;
    cJSON *array1=NULL;
    cJSON *array2=NULL;
    root =cJSON_CreateObject();
    array =cJSON_CreateObject();
    array1 =cJSON_CreateObject();
    array2 =cJSON_CreateObject();
    cJSON_AddItemToObject(root,"user-id",cJSON_CreateString("HF102001-A")); 
    cJSON_AddItemToObject(root,"abnormal",array1);
    cJSON_AddStringToObject(array1,"type",waring_string);
    cJSON_AddStringToObject(array1,"time",cname);
    //cJSON_AddItemToObject(root,"service",array2);
    //cJSON_AddStringToObject(array2,"type",waring_type);
    //cJSON_AddStringToObject(array2,"time",cname);
    cJSON_AddStringToObject(root,"time",month);
    char *out=cJSON_PrintUnformatted(root);
    memcpy(dat,out,strlen(out));//将整合的数据copy给dat
    cJSON_Delete(root);
    free(out);
}

//ADC采集任务
static void adc_init(void)
{
    esp_err_t r;
    gpio_num_t adc_gpio_num1,adc_gpio_num2, adc_gpio_num3;
    r = adc1_pad_get_io_num( ADC1_CHANNEL_5, &adc_gpio_num1 );
    assert( r == ESP_OK );
     r = adc1_pad_get_io_num( ADC1_CHANNEL_6, &adc_gpio_num2 );
    assert( r == ESP_OK );
     r = adc1_pad_get_io_num( ADC1_CHANNEL_7, &adc_gpio_num3 );
    assert( r == ESP_OK );
    //be sure to do the init before using adc1. 
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten( ADC1_CHANNEL_5, ADC_ATTEN_11db);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten( ADC1_CHANNEL_6, ADC_ATTEN_11db);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten( ADC1_CHANNEL_7, ADC_ATTEN_11db);
    vTaskDelay(2 * portTICK_PERIOD_MS);
   }

   static void led_test_task(void *pvParameters)
{
    while(1){
            gpio_set_level(LED_IO, 1);
            vTaskDelay(1000 / portTICK_PERIOD_MS);///freertos里面的延时函数  
            gpio_set_level(LED_IO, 0);  
            vTaskDelay(1000 / portTICK_PERIOD_MS);///freertos里面的延时函数
    }
    vTaskDelete(NULL);  
}

static void wifi_led_test_task(void *pvParameters)//wifi断开
{
        for(int i=0;i<5;i++){
            gpio_set_level(LED_IO, 1);
            vTaskDelay(100 / portTICK_PERIOD_MS);///freertos里面的延时函数  
            gpio_set_level(LED_IO, 0);  
            vTaskDelay(100 / portTICK_PERIOD_MS);///freertos里面的延时函数
            }
             vTaskDelete(NULL);
}
