/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "hx711.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "stm32f4xx_hal.h"
#define RTC_BKP_VALID_FLAG 0x320F
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_IDLE,
    STATE_DISPENSING,
    STATE_WAITING_FOR_PICKUP,
    STATE_MENU,
    STATE_SNOOZE
} SystemState;

typedef enum {
    P_STATE_IDLE,
    P_STATE_QUEUED,
    P_STATE_DISPENSING,
    P_STATE_WAIT_PICKUP,
	P_STATE_VERIFYING_DROP,
    P_STATE_SNOOZED
} PillState;

typedef struct {
	char user_id[32];
    uint8_t hour;
    uint8_t minute;
    uint8_t dispenser_id;
    char pill_name[32];
    bool is_active;
    PillState state;
    uint32_t timer_start;
    uint8_t last_triggered_date;
} Alarm;

typedef enum {
    D_STATE_IDLE,
    D_STATE_DISPENSING,
    D_STATE_WAIT_PICKUP,
    D_STATE_SNOOZED
} DispenserState;

typedef struct {
    DispenserState state;
    uint32_t timer_start;
    int steps_left;
    uint8_t current_alarm_index;
} DispenserStatus;

#define MAX_TOTAL_ALARMS 12
Alarm alarms[MAX_TOTAL_ALARMS];

typedef struct {
    bool is_busy;
    int steps_left;
} MotorStatus;

MotorStatus motor_status[4];

typedef struct {
  uint8_t hour;
  uint8_t minute;
  uint8_t pill_id;
  char taken_or_missed;
}LogEntry;

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} PinConfig;

typedef struct {
    PinConfig in[4];
} MotorConfig;


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// --- Stepper Motor Configuration ---
const MotorConfig MOTORS[4] = {
    { // Motor 1: PC9, PC8, PC7, PC6
        .in = {{GPIOC, GPIO_PIN_9}, {GPIOC, GPIO_PIN_8}, {GPIOC, GPIO_PIN_7}, {GPIOC, GPIO_PIN_6}}
    },
    { // Motor 2: PC4, PC5, PB0, PB1
        .in = {{GPIOC, GPIO_PIN_4}, {GPIOC, GPIO_PIN_5}, {GPIOB, GPIO_PIN_0}, {GPIOB, GPIO_PIN_1}}
    },
    { // Motor 3: PB12, PB13, PB14, PB15
        .in = {{GPIOB, GPIO_PIN_12}, {GPIOB, GPIO_PIN_13}, {GPIOB, GPIO_PIN_14}, {GPIOB, GPIO_PIN_15}}
    },
    { // Motor 4: PA4, PA5, PA6, PA7
        .in = {{GPIOA, GPIO_PIN_4}, {GPIOA, GPIO_PIN_5}, {GPIOA, GPIO_PIN_6}, {GPIOA, GPIO_PIN_7}}
    }
};
//3 = 4 on this code proto
// which dispenser is currently active
uint8_t active_dispenser_index = 0;

#define MAX_ALARM_MSG_LENGTH 100

#define STEPS_PER_90_DEGREES 1024
#define STEP_DELAY 6

#define PICKUP_WAIT_DURATION_MS 50000 //50 secs
#define SIMULATE_MOTORS 0
/* USER CODE BEGIN PD */

const int half_step_seq[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}
};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
SystemState current_state = STATE_IDLE;

uint8_t rx_buffer_line[MAX_ALARM_MSG_LENGTH];
uint8_t processing_buffer[MAX_ALARM_MSG_LENGTH];
uint8_t rx_index = 0;

// NON-BLOCKING MOTOR/TIMER VARIABLES
uint32_t last_action_time = 0;
int motor_steps_remaining = 0;
uint32_t last_step_time = 0;
int current_step_sequence_index = 0;

volatile bool message_ready_to_parse = false;
volatile uint32_t rx_byte_counter = 0;
uint8_t rx_byte_it;
RTC_TimeTypeDef gTime = {0};
RTC_DateTypeDef gDate = {0};
static uint32_t debug_print_timer = 0;

char pending_msg[64];
bool msg_is_pending = false;

HX711 myLoadCell;
DispenserStatus disp_status[4];
//static float current_weight = 0;
//static uint8_t event = 0;
static uint8_t change_happened = 0;
static uint8_t last_sync_minute = 60;

//SD CARD
FATFS fs;
FIL file;
UINT bw;

char current_logged_in_user[32] = {0};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RTC_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch)
{
    //Since huart2 is for debug only we use a timeout.
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 0xFFFF);
    return ch;
}

void Send_Status_To_ESP32(const char* status, uint8_t alarm_idx) {
//    RTC_TimeTypeDef sTime = {0};
//    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
//    RTC_DateTypeDef sDate = {0};
//    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

	uint8_t d_id = alarms[alarm_idx].dispenser_id;
	const char* pill_name = alarms[alarm_idx].pill_name;
	uint8_t hr = alarms[alarm_idx].hour;
	uint8_t min = alarms[alarm_idx].minute;

	// Format:STATUS:DISPENSER:HOUR:MINUTE
	snprintf(pending_msg, sizeof(pending_msg), "%s:%s:%d:%02d:%02d\n",
	         status, pill_name, d_id, hr, min);

	msg_is_pending = true;

//	printf("UART TX Ready: %s", pending_msg);
}

void Send_UART_ACK(const char* message) {
    uint16_t len = strlen(message);
    if (HAL_UART_Transmit(&huart1, (uint8_t*)message, len, 100) == HAL_OK) {
//        printf("ACK Sent to ESP32: %s\r\n", message);
    } else {
//        printf("ERROR: Failed to send ACK to ESP32.\r\n");
    }
}

void Motor_SetStep(uint8_t disp_idx, int s1, int s2, int s3, int s4) {
	if (disp_idx > 3) return;

#if SIMULATE_MOTORS == 0
	// Use the struct array to find the specific port and pin for each "IN"
	HAL_GPIO_WritePin(MOTORS[disp_idx].in[0].port, MOTORS[disp_idx].in[0].pin, s1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
	HAL_GPIO_WritePin(MOTORS[disp_idx].in[1].port, MOTORS[disp_idx].in[1].pin, s2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
	HAL_GPIO_WritePin(MOTORS[disp_idx].in[2].port, MOTORS[disp_idx].in[2].pin, s3 ? GPIO_PIN_SET : GPIO_PIN_RESET);
	HAL_GPIO_WritePin(MOTORS[disp_idx].in[3].port, MOTORS[disp_idx].in[3].pin, s4 ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else

#endif
}

/**
  * @brief Performs one motor step if steps remain.
  * This is called repeatedly in the main loop and handles the state transition.
  * @retval None
  */
void Motor_PerformStep_NonBlocking(uint8_t motor_idx) {

    if (motor_status[motor_idx].steps_left <= 0) {
            Motor_SetStep(motor_idx, 0, 0, 0, 0);
            motor_status[motor_idx].is_busy = false;
            return;
        }

        static int seq_idx[4] = {0, 0, 0, 0};
        if (motor_idx == 1 || motor_idx == 3) {
                // Clockwise
                seq_idx[motor_idx] = (seq_idx[motor_idx] + 1) % 8;
            }
            else if (motor_idx == 0 || motor_idx == 2) {
                // Counterclockwise
                seq_idx[motor_idx] = (seq_idx[motor_idx] - 1 + 8) % 8;
            }

        Motor_SetStep(motor_idx,
            half_step_seq[seq_idx[motor_idx]][0],
            half_step_seq[seq_idx[motor_idx]][1],
            half_step_seq[seq_idx[motor_idx]][2],
            half_step_seq[seq_idx[motor_idx]][3]);

        motor_status[motor_idx].steps_left--;

}


void System_Time_Set(uint8_t month, uint8_t date, uint8_t year,uint8_t hour, uint8_t min, uint8_t sec) {
    RTC_TimeTypeDef sTime = {0};
    sTime.Hours = hour;
    sTime.Minutes = min;
    sTime.Seconds = sec;

    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK)
    {
        RTC_DateTypeDef sDate = {0};
        sDate.WeekDay = RTC_WEEKDAY_MONDAY;
        sDate.Month = month;
        sDate.Date = date;
        sDate.Year = year;
        HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
        printf("RTC Set: %02d/%02d/%02d %02d:%02d:%02d\r\n", month, date, year, hour, min, sec);
    } else {
//        printf("ERROR: Failed to set RTC time.\r\n");
    }
}

void Schedule_Manager_Check(void) {
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
            if (alarms[i].is_active && alarms[i].state == P_STATE_IDLE &&
                sTime.Hours == alarms[i].hour && sTime.Minutes == alarms[i].minute && alarms[i].last_triggered_date != sDate.Date)
            {
                alarms[i].state = P_STATE_QUEUED;
                alarms[i].last_triggered_date= sDate.Date;
//                printf("Pill %d (Disp %d) is QUEUED to drop.\r\n", i, alarms[i].dispenser_id);
            }
        }
}

bool User_Exists(const char* user_id) {
    char filename[50];
    snprintf(filename, sizeof(filename), "%s_sch.csv", user_id);

    if (f_open(&file, filename, FA_READ) == FR_OK) {
        f_close(&file);
        return true;
    }
    return false;
}


void Send_User_Schedules_To_ESP32(const char* user_id) {
    char msg[128];

    Send_UART_ACK("SCHED_START\n");
    HAL_Delay(50);

    for (int disp = 0; disp < 4; disp++) {
        char t1[6] = "none";
        char t2[6] = "none";
        char t3[6] = "none";
        char pillName[32] = "none"; //ONLY NEED ONE PILL NAME PER DISPENSER!
        int foundCount = 0;

        for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
            if (alarms[i].is_active &&
                strcmp(alarms[i].user_id, user_id) == 0 &&
                alarms[i].dispenser_id == disp) {

                char timeBuf[8];
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", alarms[i].hour, alarms[i].minute);

                if (foundCount == 0) {
                    strncpy(t1, timeBuf, sizeof(t1));
                    strncpy(pillName, alarms[i].pill_name, sizeof(pillName) - 1);//grabbing name on first alarm
                }
                else if (foundCount == 1) { strncpy(t2, timeBuf, sizeof(t2));}
                else if (foundCount == 2) { strncpy(t3, timeBuf, sizeof(t3));}

                foundCount++;
            }
        }

        //LOAD_SLOT:Dispenser,PillName,Time1,Time2,Time3
        snprintf(msg, sizeof(msg), "LOAD_SLOT:%d,%s,%s,%s,%s\n", disp, pillName, t1, t2, t3);
        HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), 100);
        HAL_Delay(30);
    }

    Send_UART_ACK("SCHED_END\n");
}


void Send_User_Log_To_ESP32(const char* user_id) {
    char filename[50];
    char line_buffer[100];

    snprintf(filename, sizeof(filename), "%s_log.csv", user_id);

    if (f_open(&file, filename, FA_READ) == FR_OK) {
//        printf("Sending %s over UART...\r\n", filename);

        Send_UART_ACK("LOG_START\n");
        HAL_Delay(50);

        while (f_gets(line_buffer, sizeof(line_buffer), &file) != NULL) {
            HAL_UART_Transmit(&huart1, (uint8_t*)line_buffer, strlen(line_buffer), 100);
            HAL_Delay(15);
        }

        f_close(&file);
        Send_UART_ACK("LOG_END\n");
    } else {
//        printf("Note: No log file found for %s yet.\r\n", user_id);
        Send_UART_ACK("NACK:FILE_NOT_FOUND\n");
    }
}

bool SD_Log_User_Event(const char* user_id, const char* pill_name, uint8_t dispenser_id, uint8_t sched_hr, uint8_t sched_min,const char* status) {
    FRESULT fr;
    char filename[50];

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    snprintf(filename, sizeof(filename), "%s_log.csv", user_id);

    fr = f_open(&file, filename, FA_OPEN_APPEND | FA_WRITE);

    if (fr != FR_OK) {
//        printf("ERROR: Could not open %s. Code: %d\r\n", filename, fr);
        return false;
    }

    f_printf(&file, "%02d:%02d,%d,%s,%02d:%02d,%s\n", sTime.Hours, sTime.Minutes, dispenser_id, pill_name,sched_hr, sched_min, status);

    f_sync(&file);
    f_close(&file);

//    printf("--> LOGGED TO [%s]: Disp %d | %s | %02d:%02d | %s\r\n", filename, dispenser_id, pill_name, sched_hr, sched_min, status);
    return true;
}

void Sync_Schedules_To_SD(const char* user_id) {
    FRESULT fr;
    char filename[50];

    snprintf(filename, sizeof(filename), "%s_sch.csv", user_id);

    //FA_CREATE_ALWAYS instantly deletes the old file and starts fresh
    fr = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
//        printf("ERROR: Could not sync schedules to %s\r\n", filename);
        return;
    }
    //loop through RAM save only active alarms
    int saved_count = 0;
    for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
        if (alarms[i].is_active && strcmp(alarms[i].user_id, user_id) == 0) {
            f_printf(&file, "%d,%02d,%02d,%s\n", alarms[i].dispenser_id, alarms[i].hour, alarms[i].minute, alarms[i].pill_name);
            saved_count++;
        }
    }

    f_sync(&file);
    f_close(&file);
//    printf("--> SYNC COMPLETE: Saved %d active alarms to %s\r\n", saved_count, filename);
}

bool Load_User_Schedules(const char* user_id) {
    FRESULT fr;
    char filename[50];
    char line_buffer[64];

    snprintf(filename, sizeof(filename), "%s_sch.csv", user_id);
    fr = f_open(&file, filename, FA_READ);
    if (fr != FR_OK) {
//        printf("No existing schedules found for %s. Starting fresh.\r\n", user_id);
        return false;
    }

//    printf("Loading schedules for %s from SD...\r\n", user_id);

    while (f_gets(line_buffer, sizeof(line_buffer), &file) != NULL) {
        uint16_t disp, hr, min;
        char p_name[32] = {0};

        if (sscanf(line_buffer, "%hu,%hu,%hu,%31[^\r\n]", &disp, &hr, &min, p_name) >= 3) {
        	if (sscanf(line_buffer, "%hu,%hu,%hu,%31[^\r\n]", &disp, &hr, &min, p_name) == 3) strcpy(p_name, "Unknown"); // Fallback for old files without a name

            for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
                if (!alarms[i].is_active) {
                    alarms[i].dispenser_id = (uint8_t)disp;
                    alarms[i].hour = (uint8_t)hr;
                    alarms[i].minute = (uint8_t)min;
                    alarms[i].is_active = true;
                    alarms[i].state = P_STATE_IDLE;
                    alarms[i].last_triggered_date = 0;

                    strncpy(alarms[i].user_id, user_id, sizeof(alarms[i].user_id) - 1);
                    strncpy(alarms[i].pill_name, p_name, sizeof(alarms[i].pill_name) - 1);
//                    printf("  -> Loaded: Disp %d at %02d:%02d\r\n", disp, hr, min);
                    break;
                }
            }
        }
    }

    f_close(&file);
    return true;
}

void Restore_Tomorrow_Shields(const char* user_id) {
    char filename[50];
    char line_buffer[100];
    snprintf(filename, sizeof(filename), "%s_log.csv", user_id);

    RTC_DateTypeDef sDate = {0};
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    if (f_open(&file, filename, FA_READ) == FR_OK) {
        while (f_gets(line_buffer, sizeof(line_buffer), &file) != NULL) {
            uint16_t actual_hr, actual_min, disp, sched_hr, sched_min;
            char pill[32] = {0}, status[16] = {0};

            if (sscanf(line_buffer, "%hu:%hu,%hu,%31[^,],%hu:%hu,%15[^\r\n]",
                       &actual_hr, &actual_min, &disp, pill, &sched_hr, &sched_min, status) == 7) {

                if (strcmp(status, "DISPENSED") == 0 || strcmp(status, "TAKEN") == 0) {
                    //find the matching alarm in RAM to DISPENSED or TAKEN
                    for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
                        if (alarms[i].is_active && alarms[i].dispenser_id == disp &&
                            alarms[i].hour == sched_hr && alarms[i].minute == sched_min) {

                            //lock it for the day
                            alarms[i].last_triggered_date = sDate.Date;
                        }
                    }
                }
            }
        }
        f_close(&file);
    }
}


void Handle_Incoming_UART_Message(void) {
//    printf("DBG: Parsing received line: %s\r\n", (char*)processing_buffer);

    uint16_t disp_num, hr, min, repeat_flag;
    uint16_t s_disp, s_hr, s_min;
    int parsed_count;

    //SCHED messages - creating the schedules
    parsed_count = sscanf((char*)processing_buffer, "SCHED:%hu:%hu:%hu:%hu",
                              &disp_num, &hr, &min, &repeat_flag);

    if (parsed_count == 4 && disp_num >= 0 && disp_num <= 3) {
    	int target_index = -1;
//        printf("DBG: Successfully parsed SCHED message.\r\n");

        for(int i = 0; i < MAX_TOTAL_ALARMS; i++) {
                    if (alarms[i].is_active && alarms[i].dispenser_id == disp_num &&
                        alarms[i].hour == hr && alarms[i].minute == min) {
                        target_index = i;
                        break;
                    }
                }

        		//find first empty slot if not found
                if (target_index == -1) {
                    for(int i = 0; i < MAX_TOTAL_ALARMS; i++) {
                        if (!alarms[i].is_active) {
                            target_index = i;
                            break;
                        }
                    }
                }

                if (target_index != -1) {
                    alarms[target_index].dispenser_id = (uint8_t)disp_num;
                    alarms[target_index].hour = (uint8_t)hr;
                    alarms[target_index].minute = (uint8_t)min;
                    alarms[target_index].is_active = (repeat_flag == 1);
                    alarms[target_index].last_triggered_date = 0;

                    strncpy(alarms[target_index].user_id, current_logged_in_user, sizeof(alarms[target_index].user_id) - 1);
//                    printf("Alarm SLOT %d: Disp %d at %02d:%02d\r\n", target_index, disp_num, hr, min);
                    Send_UART_ACK("ACK:SCHED_OK\n");
                } else {
                    Send_UART_ACK("NACK:MEM_FULL\n");
                }

   } else if (strncmp((char*)processing_buffer, "DELETE:", 7) == 0) {
        char req_user[32] = {0};

        if (sscanf((char*)processing_buffer, "DELETE:%31[^:]:%hu", req_user, &disp_num) == 2) {
//            printf("DBG: Deleting ALL schedules for Dispenser %d\r\n", disp_num);

            bool found = false;
            for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
                if (alarms[i].dispenser_id == (uint8_t)disp_num) {
                    alarms[i].is_active = false;
                    found = true;
                }
            }

            if (found) {
//                printf("Success: All schedules for Disp %d cleared.\r\n", disp_num);
                Sync_Schedules_To_SD(req_user);
                Send_UART_ACK("ACK:DELETE_OK\n");
            } else {
//                printf("Note: No active schedules found for Disp %d to delete.\r\n", disp_num);
                Send_UART_ACK("ACK:DELETE_NONE\n");
            }
        } else {
            Send_UART_ACK("NACK:INVALID_FORMAT\n");
        }
    } 	else if (sscanf((char*)processing_buffer, "SNOOZE:%hu:%hu:%hu", &s_disp, &s_hr, &s_min) == 3) {

    	        //SNOOZE ALL pills currently in the waiting state
    	            int total_snoozed = 0;
    	            for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
    	                if (alarms[i].is_active && alarms[i].state == P_STATE_WAIT_PICKUP) {
    	                    alarms[i].state = P_STATE_SNOOZED;
    	                    alarms[i].timer_start = HAL_GetTick();
    	                    total_snoozed++;
    	                }
    	            }
//    	            printf("SNOOZE TRIGGERED: %d pills moved to snooze state.\r\n", total_snoozed);
    	            Send_UART_ACK("ACK:SNOOZE_OK\n");


    	        memset(processing_buffer, 0, MAX_ALARM_MSG_LENGTH);

		}

    else if (strncmp((char*)processing_buffer, "SIGNUP:", 7) == 0) {
            char req_user[32] = {0};
            char req_role[32] = {0};
            char req_phone[32] = {0};

            if (sscanf((char*)processing_buffer, "SIGNUP:%31[^:]:%31[^:]:%31[^\r\n]", req_user, req_role, req_phone) >= 2) {
            	if (sscanf((char*)processing_buffer, "SIGNUP:%31[^:]:%31[^:]:%31[^\r\n]", req_user, req_role, req_phone) == 2) strcpy(req_phone, "");
//            	printf("DBG: User Signup Detected: %s as %s with phone number: %s\r\n", req_user, req_role, req_phone);


                if (User_Exists(req_user)) {
                    Send_UART_ACK("SIGNUP_ACK:FAIL\n");
                } else {
                    //create account
                    char filename[50];
                    snprintf(filename, sizeof(filename), "%s_sch.csv", req_user);

                    FRESULT fr = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
                    if (fr == FR_OK) {
                        f_close(&file);

                        snprintf(filename, sizeof(filename), "%s_role.txt", req_user);
                        fr = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
                        if (fr == FR_OK) {
                            f_printf(&file, "%s", req_role);
                            f_close(&file);
                        }

                        snprintf(filename, sizeof(filename), "%s_phone.txt", req_user);
                        fr = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
                        if (fr == FR_OK) {
                        	f_printf(&file, "%s", req_phone);
                             f_close(&file);
                        }

                        for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
                             alarms[i].is_active = false;
                             memset(alarms[i].user_id, 0, sizeof(alarms[i].user_id));
                        }
                        strncpy(current_logged_in_user, req_user, sizeof(current_logged_in_user) - 1);
                        char ack_msg[100];
                        snprintf(ack_msg, sizeof(ack_msg), "SIGNUP_ACK:SUCCESS:%s:%s\n", req_role, req_phone);
                        Send_UART_ACK(ack_msg);
                    } else {
//                    	printf("DBG: SD ERROR creating file! FatFS Code: %d\r\n", fr);
                        Send_UART_ACK("NACK:SD_ERROR\n");
                    }
                }
            } else {
                Send_UART_ACK("NACK:INVALID_FORMAT\n");
            }
        }

    else if (strncmp((char*)processing_buffer, "LOGIN:", 6) == 0) {
    		    char req_user[32] = {0};

    		    if (sscanf((char*)processing_buffer, "LOGIN:%31[^\r\n]", req_user) == 1) {
    		        printf("DBG: User Login Detected: %s\r\n", req_user);

    		        if (!User_Exists(req_user)) {

    		        	Send_UART_ACK("LOGIN_ACK:FAIL\n");
    		        } else {
    		        	char loaded_role[16] = "Regular"; // Default fallback
    		        	char loaded_phone[32] = "";
    		        	char filename[50];

    		        	snprintf(filename, sizeof(filename), "%s_role.txt", req_user);
    		        	if (f_open(&file, filename, FA_READ) == FR_OK) {
    		        	    f_gets(loaded_role, sizeof(loaded_role), &file);
    		        	    f_close(&file);
    		        	    loaded_role[strcspn(loaded_role, "\r\n")] = 0;
    		        	}

    		        	snprintf(filename, sizeof(filename), "%s_phone.txt", req_user);
    		        	if (f_open(&file, filename, FA_READ) == FR_OK) {
    		        		f_gets(loaded_phone, sizeof(loaded_phone), &file);
    		        		f_close(&file);
    		        	    loaded_phone[strcspn(loaded_phone, "\r\n")] = 0;
    		        	}

						//redundant clear but necessary!
						for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
							alarms[i].is_active = false;
							memset(alarms[i].user_id, 0, sizeof(alarms[i].user_id));
						}
						//new active schedules into RAM
						Load_User_Schedules(req_user);
						Restore_Tomorrow_Shields(req_user);
						strncpy(current_logged_in_user, req_user, sizeof(current_logged_in_user) - 1);

						char ack_msg[100];
						snprintf(ack_msg, sizeof(ack_msg), "LOGIN_ACK:SUCCESS:%s:%s\n", loaded_role, loaded_phone);
						printf("DEBUG - Sending to ESP32: %s", ack_msg);
						Send_UART_ACK(ack_msg);
						HAL_Delay(50);

						Send_User_Schedules_To_ESP32(req_user);
						HAL_Delay(50);

						Send_User_Log_To_ESP32(req_user);

    		        }

    		    } else {
    		    Send_UART_ACK("NACK:INVALID_FORMAT\n");
    		 }

    		}

    else if (strncmp((char*)processing_buffer, "CHECK_PATIENT:", 14) == 0) {
    	char req_user[32] = {0};
    	char found_role[16] = {0};
    	char role_filename[50];

    	if (sscanf((char*)processing_buffer, "CHECK_PATIENT:%31[^\r\n]", req_user) == 1) {
//    	            printf("DBG: Checking existence of patient: %s\r\n", req_user);

    	            snprintf(role_filename, sizeof(role_filename), "%s_role.txt", req_user);

    	            bool is_valid_patient = false;

    	                    if (f_open(&file, role_filename, FA_READ) == FR_OK) {
    	                        if (f_gets(found_role, sizeof(found_role), &file) != NULL) {
    	                            found_role[strcspn(found_role, "\r\n")] = 0;

    	                            if (strcasecmp(found_role, "Patient") == 0) {
    	                                is_valid_patient = true;
    	                            }
    	                        }
    	                        f_close(&file);
    	                    }
    	            //check if user exists and IS a patient

    	            if (is_valid_patient) {
    	                char ack_msg[64];
    	                snprintf(ack_msg, sizeof(ack_msg), "PATIENT_ACK:SUCCESS:%s\n", req_user);
    	                Send_UART_ACK(ack_msg);
    	                HAL_Delay(50);

    	                for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
    	                    alarms[i].is_active = false;
    	                }
    	                Load_User_Schedules(req_user);

    	                Send_User_Schedules_To_ESP32(req_user);
    	                HAL_Delay(50);

    	                Send_User_Log_To_ESP32(req_user);

    	            } else {
    	                //Patient not found on SD card
    	                char fail_msg[64];
    	                snprintf(fail_msg, sizeof(fail_msg), "PATIENT_ACK:FAIL:%s\n", req_user);
    	                Send_UART_ACK(fail_msg);
    	            }
    	        } else {
    	            Send_UART_ACK("NACK:INVALID_PATIENT_FORMAT\n");
    	        }

    }

    else if (strncmp((char*)processing_buffer, "SAVESD:", 7) == 0) {
    		    char parsed_user[32] = {0};
    		    char parsed_pill[32] = {0};
    		    char parsed_status[32] = {0};
    		    uint16_t disp, hr, min;

    		    if (sscanf((char*)processing_buffer, "SAVESD:%31[^:]:%31[^:]:%hu:%hu:%hu:%31[^\r\n]",
    		                        parsed_user, parsed_pill, &disp, &hr, &min, parsed_status) == 6) {

//    		        printf("DBG: SAVESD -> User: %s | Pill Name: %s | Disp: %d | Time: %02d:%02d | Status: %s\r\n", parsed_user, parsed_pill, disp, hr, min, parsed_status);

    		        if (strcmp(parsed_status, "SCHEDULED") == 0) {
    		            //find an empty slot or update existing
    		            for(int i = 0; i < MAX_TOTAL_ALARMS; i++) {
    		                if (!alarms[i].is_active || (alarms[i].dispenser_id == disp && alarms[i].hour == hr && alarms[i].minute == min)) {
    		                    strncpy(alarms[i].user_id, parsed_user, sizeof(alarms[i].user_id) - 1);
//    		                    strncpy(alarms[i].pill_name, parsed_pill, sizeof(alarms[i].pill_name) - 1);
    		                    alarms[i].dispenser_id = (uint8_t)disp;
    		                    alarms[i].hour = (uint8_t)hr;
    		                    alarms[i].minute = (uint8_t)min;
    		                    alarms[i].is_active = true;
    		                    alarms[i].state = P_STATE_IDLE;
    		                    break;
    		                }
    		            }

    		            for(int i = 0; i < MAX_TOTAL_ALARMS; i++) {
    		                if (alarms[i].is_active && alarms[i].dispenser_id == disp) {
    		                strncpy(alarms[i].pill_name, parsed_pill, sizeof(alarms[i].pill_name) - 1);
    		                }
    		            }

    		            Sync_Schedules_To_SD(parsed_user); //Save RAM to _sch.csv
    		        }
    		        Send_UART_ACK("ACK:SAVESD_OK\n");
    		    } else {
    		        Send_UART_ACK("NACK:INVALID_FORMAT\n");
    		    }
    		}


         else if (strncmp((char*)processing_buffer, "TIME:", 5) == 0) {
        	 int mo, da, yr, hr, mn, sc;
        	 if (sscanf((char*)processing_buffer, "TIME:%d:%d:%d:%d:%d:%d", &mo, &da, &yr, &hr, &mn, &sc) == 6) {
        	             printf("DBG: Successfully parsed Live Time Sync.\r\n");
        	             System_Time_Set((uint8_t)mo, (uint8_t)da, (uint8_t)yr, (uint8_t)hr, (uint8_t)mn, (uint8_t)sc);
        	             Send_UART_ACK("ACK:TIME_OK\n");
        	         } else {
        	        	 printf("DBG: Failed to parse TIME format. ESP32 actually sent: [%s]\r\n", processing_buffer);
        	             Send_UART_ACK("NACK:INVALID_FORMAT\n");
        	         }
         }
    }

//request time at boot
//void RTC_Set_From_UART_Sync(void) {
//    uint8_t time_data[10] = {0}; // [Hour, Minute, Second]
//    __HAL_UART_FLUSH_DRREGISTER(&huart1);
//
////unused char rx_buf[64];
//        int retries = 10;
//        bool sync_success = false;
//
//        printf("Requesting time sync from ESP32...\r\n");
//
//        while (retries > 0 && !sync_success) {
//            HAL_UART_Transmit(&huart1, (uint8_t*)"GET_TIME\n", 9, 100);
//
//            if (HAL_UART_Receive(&huart1, time_data, 6, 2000) == HAL_OK) {
//               System_Time_Set(time_data[0] + 1, time_data[1], time_data[2], time_data[3], time_data[4], time_data[5]);
//               printf("DBG: Sync SUCCESS on try %d\r\n", 6 - retries);
//               sync_success = true;
//               } else {
//            	   printf("No response, retrying... (%d left)\r\n", retries - 1);
//                retries--;
//                HAL_Delay(500);
//            }
//        }
//
//        if (!sync_success) {
//            printf("Sync FAILED. Defaulting to 12:00:00.\r\n");
//            System_Time_Set( 1, 1, 25, 12, 0, 0);
//        }
//}
	void RTC_Set_From_UART_Sync(void) {
		printf("Requesting time sync from ESP32 at boot...\r\n");
		__HAL_UART_FLUSH_DRREGISTER(&huart1);

		// Clear out any old garbage
		memset(processing_buffer, 0, MAX_ALARM_MSG_LENGTH);
		message_ready_to_parse = false;

		HAL_UART_Transmit(&huart1, (uint8_t*)"GET_TIME\n", 9, 100);

		uint32_t start_tick = HAL_GetTick();

		// Wait up to 5 seconds for a response
		while (HAL_GetTick() - start_tick < 15000) {
			if (message_ready_to_parse) {
				message_ready_to_parse = false;
				Handle_Incoming_UART_Message(); // This will hit the new TIME: block!

				memset(processing_buffer, 0, MAX_ALARM_MSG_LENGTH);
				return; // Exit function, sync was successful!
			}
		}

		printf("Sync FAILED. Defaulting to 12:00:00.\r\n");
		System_Time_Set(1, 1, 25, 12, 0, 0);
	}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {

    	rx_byte_counter++;
        if (rx_byte_it == '\n') {
            if (rx_index > 0) {
                rx_buffer_line[rx_index] = '\0';
                memcpy(processing_buffer, rx_buffer_line, rx_index + 1);
                message_ready_to_parse = true; //THIS MUST BE REACHED
            }
            rx_index = 0;
        } else if (rx_byte_it != '\r') {
            if (rx_index < MAX_ALARM_MSG_LENGTH - 1) {
                rx_buffer_line[rx_index++] = rx_byte_it;
            } else {
                rx_index = 0;
            }
        }
        HAL_UART_Receive_IT(&huart1, &rx_byte_it, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        // 1. Clear the error flags by reading the Status and Data registers
        uint32_t isrflags   = READ_REG(huart->Instance->SR);
        uint32_t cr1its     = READ_REG(huart->Instance->CR1);
        uint32_t errorflags = (isrflags & (uint32_t)(USART_SR_PE | USART_SR_FE | USART_SR_ORE | USART_SR_NE));

        // Dummy read of the data register to flush the bad byte
        uint32_t tmpreg = READ_REG(huart->Instance->DR);
        (void)tmpreg; // Prevent compiler warning

        printf("DBG: UART Error Caught and Cleared! Restarting RX...\r\n");

        // 2. Force the UART to start listening again
        HAL_UART_Receive_IT(&huart1, &rx_byte_it, 1);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_RTC_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  MX_SPI2_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */

  MX_FATFS_Init();
  // Mount the drive with delayed mounting (0 instead of 1)
    FRESULT res = f_mount(&fs, "", 0);
    if(res == FR_OK) {
        printf("SD Card Workspace Registered Successfully.\r\n");

    } else {
        printf("ERROR: SD Card failed to mount.FatFS Error Code: %d\r\n", res);
    }

  HX711_Init(&myLoadCell, GPIOB, GPIO_PIN_3, GPIOB, GPIO_PIN_4);
  myLoadCell.coefficient = 1.2f;//


  //Tare the scale (reset to 0) on startup
  HAL_Delay(500);

  HX711_Tare(&myLoadCell, 30); //average 30 samples currently
//  printf("Tare complete. Offset: %ld\r\n", myLoadCell.offset);
  HX711_SetupPageHinkley(&myLoadCell, 0.5f, 150.0f);

  Motor_SetStep(0, 0, 0, 0, 0);
  HAL_UART_Receive_IT(&huart1, &rx_byte_it, 1);
  RTC_Set_From_UART_Sync();

    //set dispenser IDs as inactive
    for(int i = 0; i < 4; i++) {
        alarms[i].is_active = false;
        alarms[i].dispenser_id = i;
    }

    volatile uint32_t rx_byte_counter = 0;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
       while (1) //voltage reference will be at 3.3V
       	  {

    	   bool any_motor_spinning = false;
    	              for (int m = 0; m < 4; m++) {
    	                  if (motor_status[m].is_busy) any_motor_spinning = true;
    	              }
    	   bool sensor_needed = false;
			   for(int j=0; j<MAX_TOTAL_ALARMS; j++) {
				   if(alarms[j].state == P_STATE_VERIFYING_DROP || alarms[j].state == P_STATE_WAIT_PICKUP) {
					   sensor_needed = true;
					   break;
				   }
    	   }
			   // Another Timer call : RTC_Set_From_UART_Sync(); at 30 second mark every 5 minute?


    	   if (sensor_needed && !any_motor_spinning && HAL_GPIO_ReadPin(myLoadCell.dat_gpio, myLoadCell.dat_pin) == GPIO_PIN_RESET) {
    		       int32_t raw_average = HX711_ReadAverage(&myLoadCell, 30);
    		       int32_t raw_delta = raw_average - myLoadCell.offset;
    		       change_happened = HX711_CheckChange(&myLoadCell, HX711_GetWeight(&myLoadCell, 5));
    	   	   	   }

    	   if (HAL_GetTick() - debug_print_timer >= 1000) {
    	           HAL_RTC_GetTime(&hrtc, &gTime, RTC_FORMAT_BIN);
    	           HAL_RTC_GetDate(&hrtc, &gDate, RTC_FORMAT_BIN);

    	           if ((gTime.Minutes % 2 == 0) && (gTime.Seconds == 30)) { //every 2 minutes 30 seconds
    	                   if (gTime.Minutes != last_sync_minute) {
    	                       last_sync_minute = gTime.Minutes;

    	                       // Just transmit the request and walk away! The interrupt will catch the reply.
    	                       __HAL_UART_FLUSH_DRREGISTER(&huart1);
    	                       HAL_UART_Transmit(&huart1, (uint8_t*)"GET_TIME\n", 9, 10);
    	                       printf("DBG: 5-Min Periodic Sync Request Sent.\r\n");
    	                   }
    	               }
    	           printf("\r\n[SYSTEM TIME] %02d/%02d/%02d %02d:%02d:%02d | Current User: %s\r\n",
    	        		   gDate.Month, gDate.Date, gDate.Year, gTime.Hours, gTime.Minutes, gTime.Seconds, current_logged_in_user);

    	           //print active alarms belonging ONLY to the logged-in user
    	           for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
    	               if (alarms[i].is_active && strcmp(alarms[i].user_id, current_logged_in_user) == 0) {
    	                   printf("  -> Active Alarm %d: Disp %d set for %02d:%02d | State: %d\r\n",
    	                          i, alarms[i].dispenser_id, alarms[i].hour, alarms[i].minute, alarms[i].state);
    	               }
    	           }
    	           debug_print_timer = HAL_GetTick();
    	       }

             if (message_ready_to_parse) {
            	 message_ready_to_parse = false;
                 Handle_Incoming_UART_Message();
                  //reset flag after processing
                 memset(processing_buffer, 0, MAX_ALARM_MSG_LENGTH);
             }

             if (msg_is_pending) {
                 if (HAL_UART_Transmit_IT(&huart1, (uint8_t*)pending_msg, strlen(pending_msg)) == HAL_OK) {
                     msg_is_pending = false;

                     HAL_Delay(5);
                     memset(pending_msg, 0, sizeof(pending_msg));
                 }
             }

             if (rx_byte_counter > 0 && (HAL_GetTick() % 1000 == 0)) {
//                 printf("DBG: Bytes received so far: %lu\r\n", rx_byte_counter);
             }

             Schedule_Manager_Check();

             //STATE MACHINE
             for (int i = 0; i < MAX_TOTAL_ALARMS; i++) {
                     uint8_t m_idx = alarms[i].dispenser_id;

                     static PillState last_reported_state[MAX_TOTAL_ALARMS] = {P_STATE_IDLE};

                     if (alarms[i].state != last_reported_state[i]) {
                         const char* name[] = {"IDLE", "QUEUED", "DISPENSE", "WAITING", "VERIFY", "SNOOZED"};
//                         printf("[LOG] Pill %02d changed to: %s\r\n", i, name[alarms[i].state]);
                         last_reported_state[i] = alarms[i].state;
                     }

                     switch (alarms[i].state) {
                         case P_STATE_QUEUED:
                             // Only start motor if the bin isn't already turning for someone else
                             if (!motor_status[m_idx].is_busy) {
                                 motor_status[m_idx].is_busy = true;
                                 motor_status[m_idx].steps_left = STEPS_PER_90_DEGREES;
                                 alarms[i].state = P_STATE_DISPENSING;
                             }
                             break;

                         case P_STATE_DISPENSING:
                             // Wait for the hardware to finish the physical turn
                             if (!motor_status[m_idx].is_busy) {

                            	 myLoadCell.baseline = HX711_GetWeight(&myLoadCell, 10);
                            	 for(int k = 0; k < 8; k++) myLoadCell.diff_history[k] = 0.0f;

                            	 alarms[i].state = P_STATE_VERIFYING_DROP;
                            	 alarms[i].timer_start = HAL_GetTick();
//                            	 printf("Pill %d motor move complete. Verifying drop...\r\n", i);
                             }
                             break;

                         case P_STATE_VERIFYING_DROP:
                        	 if (change_happened == 1) {
                        	             // We just moved the motor, so any change now IS the pill landing
                        		 	 	 SD_Log_User_Event(alarms[i].user_id, alarms[i].pill_name, alarms[i].dispenser_id, alarms[i].hour, alarms[i].minute, "DISPENSED");
                        	             Send_Status_To_ESP32("DISPENSED", i);
//                        	             printf("PILL DROP VERIFIED. Sending Data to SD Card.");

                        	             alarms[i].state = P_STATE_WAIT_PICKUP;
                        	             alarms[i].timer_start = HAL_GetTick();
                        	             //change_happened = 0;
                        	         }
                                   break;

                         case P_STATE_SNOOZED:
                             if (HAL_GetTick() - alarms[i].timer_start >= 300000) { // at 5 MINS could change for testing
                            	 char snooze_msg[64];
                            	 snprintf(snooze_msg, sizeof(snooze_msg), "SNOOZE_OVER:%d:%02d:%02d\n",
                            	                                           alarms[i].dispenser_id, alarms[i].hour, alarms[i].minute);
                            	 HAL_UART_Transmit(&huart1, (uint8_t*)snooze_msg, strlen(snooze_msg), 100);

                            	 alarms[i].state = P_STATE_WAIT_PICKUP;
                            	 alarms[i].timer_start = HAL_GetTick();

//                            	 printf("Snooze over for Disp %d\r\n", alarms[i].dispenser_id);
                            	 HAL_Delay(50);
                             }
                             break;

                         case P_STATE_WAIT_PICKUP:
                        	 if (change_happened == 2) {
                        		 	 	 SD_Log_User_Event(alarms[i].user_id, alarms[i].pill_name, alarms[i].dispenser_id, alarms[i].hour, alarms[i].minute, "TAKEN");
                        	             Send_Status_To_ESP32("TAKEN", i);
//                        	             printf("PILL TAKEN: Disp %d\r\n", alarms[i].dispenser_id);

                        	             alarms[i].state = P_STATE_IDLE;
//                        	             change_happened = 0; // CONSUME the event
//                        	             alarms[i].is_active = false;
//                        	             Sync_Schedules_To_SD(alarms[i].user_id);
                        	             HAL_Delay(10);
                        	         }
                        	         break;

                         default: break;
                     }
                 }

             if (change_happened == 2) {
                              change_happened = 0;
                          }

             if (HAL_GetTick() - last_step_time >= STEP_DELAY) {
                     for (int m = 0; m < 4; m++) {
                         if (motor_status[m].is_busy) Motor_PerformStep_NonBlocking(m);
                     }
                     last_step_time = HAL_GetTick();
                 }
       	  }
}
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 15;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6
                          |GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_12|GPIO_PIN_13
                          |GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_3, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC1 PC4 PC5 PC6
                           PC7 PC8 PC9 */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6
                          |GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA4 PA5 PA6 PA7 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB12 PB13
                           PB14 PB15 PB3 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_12|GPIO_PIN_13
                          |GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
