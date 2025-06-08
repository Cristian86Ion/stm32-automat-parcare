/*
 * Manager de Parcare Auto cu STM32F103CBT6
 * Echipa: 3 studenti
 * 
 * Componente utilizate:
 * - STM32F103CBT6 (microcontroller)
 * - ENC28J60 (modul Ethernet)
 * - PIR sensor (detectie ocupare loc)
 * - DS1307 (RTC - Real Time Clock)
 * - LED (semnalizare controlor)
 * - Switch/Buton (simulare monede)
 */

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_uart.h"
#include "stm32f1xx_hal_rtc.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// ===== DEFINIRI CONSTANTE =====
#define T1_MINUTES 10    // Timp de gratie dupa expirare (10 min)
#define T2_MINUTES 30    // Timp rezervare per moneda (30 min)
#define DEBOUNCE_DELAY 50 // Debounce pentru buton (ms)

// ===== STRUCTURA PENTRU STAREA PARCARII =====
typedef struct {
    bool loc_ocupat;           // TRUE daca locul este ocupat fizic
    bool loc_rezervat;         // TRUE daca locul este rezervat (platit)
    uint32_t timp_rezervare;   // Timestamp cand expira rezervarea
    uint32_t timp_ocupare;     // Timestamp cand s-a ocupat locul
    uint32_t monede_primite;   // Numar de monede primite
} ParkingState_t;

// ===== VARIABILE GLOBALE =====
ParkingState_t parking_state = {0};
uint32_t sistem_timestamp = 0;  // Timestamp sistem (secunde)
bool buton_apasat_anterior = false;
uint32_t last_button_time = 0;

// Handlere pentru perifericele HAL
UART_HandleTypeDef huart1;
RTC_HandleTypeDef hrtc;

// ===== FUNCTII UTILITARE =====

/**
 * Functie pentru obtinerea timpului curent de la DS1307
 * Returneaza timestamp in secunde
 */
uint32_t DS1307_GetTimestamp(void) {
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    
    // Citim timpul si data de la DS1307
    if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_GetTick() / 1000; // Fallback to system tick
    }
    if (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_GetTick() / 1000; // Fallback to system tick
    }
    
    // Convertim la timestamp simplu (pentru exemplificare)
    uint32_t timestamp = (uint32_t)sDate.Date * 24 * 3600 + 
                        (uint32_t)sTime.Hours * 3600 + 
                        (uint32_t)sTime.Minutes * 60 + 
                        (uint32_t)sTime.Seconds;
    
    return timestamp;
}

/**
 * Functie pentru citirea senzorului PIR
 * Returneaza TRUE daca detecteaza prezenta
 */
bool PIR_ReadSensor(void) {
    // Presupunem ca PIR este conectat la pinul PA0
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET;
}

/**
 * Functie pentru citirea butonului (simulare monede)
 * Returneaza TRUE daca butonul a fost apasat (cu debounce)
 */
bool Button_ReadWithDebounce(void) {
    bool buton_actual = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET);
    uint32_t timp_curent = HAL_GetTick();
    
    // Detectam frontul crescator (buton apasat)
    if (buton_actual && !buton_apasat_anterior && 
        (timp_curent - last_button_time > DEBOUNCE_DELAY)) {
        last_button_time = timp_curent;
        buton_apasat_anterior = buton_actual;
        return true;
    }
    
    buton_apasat_anterior = buton_actual;
    return false;
}

/**
 * Functie pentru controlul LED-ului de semnalizare
 */
void LED_SetState(bool state) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * Functie pentru trimiterea mesajelor prin UART (HyperTerminal)
 */
void UART_SendMessage(char* message) {
    HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), 1000);
}

/**
 * Functie pentru formatarea si afisarea timpului
 */
void PrintTimestamp(uint32_t timestamp, char* buffer) {
    uint32_t ore = (timestamp / 3600) % 24;
    uint32_t minute = (timestamp / 60) % 60;
    uint32_t secunde = timestamp % 60;
    sprintf(buffer, "%02lu:%02lu:%02lu", (unsigned long)ore, (unsigned long)minute, (unsigned long)secunde);
}

// ===== LOGICA PRINCIPALA PENTRU MANAGEMENTUL PARCARII =====

/**
 * Functie pentru procesarea unei monede (apasare buton)
 */
void ProcessCoin(void) {
    uint32_t timp_curent = DS1307_GetTimestamp();
    
    parking_state.monede_primite++;
    parking_state.loc_rezervat = true;
    
    // Adaugam T2_MINUTES pentru fiecare moneda
    if (parking_state.timp_rezervare < timp_curent) {
        // Prima moneda sau rezervarea a expirat
        parking_state.timp_rezervare = timp_curent + (T2_MINUTES * 60);
    } else {
        // Monede suplimentare - cumulam timpul
        parking_state.timp_rezervare += (T2_MINUTES * 60);
    }
    
    // Log pe HyperTerminal
    char buffer[100];
    char timp_str[20];
    PrintTimestamp(timp_curent, timp_str);
    sprintf(buffer, "[%s] Moneda primita! Rezervat pana la ", timp_str);
    UART_SendMessage(buffer);
    
    PrintTimestamp(parking_state.timp_rezervare, timp_str);
    sprintf(buffer, "%s\r\n", timp_str);
    UART_SendMessage(buffer);
}

/**
 * Functie pentru procesarea ocuparii locului (PIR activated)
 */
void ProcessParkingOccupied(void) {
    if (!parking_state.loc_ocupat) {
        parking_state.loc_ocupat = true;
        parking_state.timp_ocupare = DS1307_GetTimestamp();
        
        // Log pe HyperTerminal
        char buffer[100];
        char timp_str[20];
        PrintTimestamp(parking_state.timp_ocupare, timp_str);
        sprintf(buffer, "[%s] Loc ocupat!\r\n", timp_str);
        UART_SendMessage(buffer);
    }
}

/**
 * Functie pentru procesarea eliberarii locului (PIR deactivated)
 */
void ProcessParkingFree(void) {
    if (parking_state.loc_ocupat) {
        parking_state.loc_ocupat = false;
        uint32_t timp_eliberare = DS1307_GetTimestamp();
        
        // Log pe HyperTerminal
        char buffer[100];
        char timp_str[20];
        PrintTimestamp(timp_eliberare, timp_str);
        sprintf(buffer, "[%s] Loc eliberat!\r\n", timp_str);
        UART_SendMessage(buffer);
    }
}

/**
 * Functie pentru verificarea si semnalizarea controlorului
 */
void CheckControllerAlert(void) {
    uint32_t timp_curent = DS1307_GetTimestamp();
    
    // Verificam daca locul este ocupat si rezervarea a expirat + T1
    if (parking_state.loc_ocupat && 
        !parking_state.loc_rezervat && 
        (timp_curent > parking_state.timp_rezervare + (T1_MINUTES * 60))) {
        
        // Semnalizam controlorul
        LED_SetState(true);
        
        // Log pe HyperTerminal
        char buffer[100];
        char timp_str[20];
        PrintTimestamp(timp_curent, timp_str);
        sprintf(buffer, "[%s] ALERT: Controlor chemat!\r\n", timp_str);
        UART_SendMessage(buffer);
    } else {
        LED_SetState(false);
    }
}

/**
 * Functie pentru actualizarea starii rezervarii
 */
void UpdateReservationStatus(void) {
    uint32_t timp_curent = DS1307_GetTimestamp();
    
    // Verificam daca rezervarea a expirat
    if (parking_state.loc_rezervat && timp_curent > parking_state.timp_rezervare) {
        parking_state.loc_rezervat = false;
        
        char buffer[100];
        char timp_str[20];
        PrintTimestamp(timp_curent, timp_str);
        sprintf(buffer, "[%s] Rezervare expirata!\r\n", timp_str);
        UART_SendMessage(buffer);
    }
}

/**
 * Functie pentru procesarea comenzilor de la HyperTerminal
 */
void ProcessUARTCommand(void) {
    uint8_t received_char;
    
    // Verificam daca avem date disponibile
    if (HAL_UART_Receive(&huart1, &received_char, 1, 10) == HAL_OK) {
        if (received_char == '?') {  // Comanda de interogare
            uint32_t timp_curent = DS1307_GetTimestamp();
            char buffer[150];
            char timp_str[20];
            
            PrintTimestamp(timp_curent, timp_str);
            
            if (parking_state.loc_rezervat) {
                char timp_rezervare_str[20];
                PrintTimestamp(parking_state.timp_rezervare, timp_rezervare_str);
                sprintf(buffer, "[%s] Status: REZERVAT pana la %s\r\n", 
                       timp_str, timp_rezervare_str);
            } else {
                sprintf(buffer, "[%s] Status: LIBER\r\n", timp_str);
            }
            
            UART_SendMessage(buffer);
        }
    }
}

// ===== FUNCTII DE INITIALIZARE =====

/**
 * Initializare System Clock pentru STM32F103
 */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Initializes the RCC Oscillators according to the specified parameters
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    // Initializes the CPU, AHB and APB buses clocks
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * Initializare GPIO
 */
void GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Enable clock pentru porturile folosite
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // PIR Sensor - PA0 (Input)
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // Button - PA1 (Input cu pull-up)
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // LED - PB0 (Output)
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/**
 * Initializare UART pentru comunicare cu HyperTerminal
 */
void UART_Init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * Initializare DS1307 (RTC) pentru STM32F103
 */
void RTC_Init(void) {
    hrtc.Instance = RTC;
    hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
    
    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        Error_Handler();
    }
}

// ===== FUNCTIA MAIN =====
int main(void) {
    // Initializare sistem HAL
    HAL_Init();
    
    // Configure the system clock
    SystemClock_Config();
    
    // Initializare perifericele
    GPIO_Init();
    UART_Init();
    RTC_Init();
    
    // Mesaj de start
    UART_SendMessage("=== Manager Parcare Auto ===\r\n");
    UART_SendMessage("Sistem pornit! Trimite '?' pentru status.\r\n\r\n");
    
    // Bucla principala
    while (1) {
        // 1. Citim senzorii
        bool pir_activ = PIR_ReadSensor();
        bool moneda_primita = Button_ReadWithDebounce();
        
        // 2. Procesam evenimentele
        if (moneda_primita) {
            ProcessCoin();
        }
        
        if (pir_activ) {
            ProcessParkingOccupied();
        } else {
            ProcessParkingFree();
        }
        
        // 3. Actualizam starea sistemului
        UpdateReservationStatus();
        CheckControllerAlert();
        
        // 4. Procesam comenzile UART
        ProcessUARTCommand();
        
        // 5. Pauza scurta pentru a nu suprasolicita CPU
        HAL_Delay(100);
    }
}

// ===== CALLBACK-URI SI HANDLERE DE INTRERUPERI =====

/**
 * Callback pentru erori sistem
 */
void Error_Handler(void) {
    __disable_irq();
    while (1) {
        // Error state - LED intermitent
        LED_SetState(true);
        HAL_Delay(200);
        LED_SetState(false);
        HAL_Delay(200);
    }
}

/**
 * HAL MSP Initialization
 */
void HAL_MspInit(void) {
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

/**
 * UART MSP Initialization pentru STM32F103
 */
void HAL_UART_MspInit(UART_HandleTypeDef* huart) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    if (huart->Instance == USART1) {
        // Peripheral clock enable
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        
        // USART1 GPIO Configuration pentru STM32F103
        // PA9  ------> USART1_TX
        // PA10 ------> USART1_RX
        GPIO_InitStruct.Pin = GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

/**
 * RTC MSP Initialization pentru STM32F103
 */
void HAL_RTC_MspInit(RTC_HandleTypeDef* hrtc) {
    if (hrtc->Instance == RTC) {
        // Peripheral clock enable
        __HAL_RCC_RTC_ENABLE();
        __HAL_RCC_BKP_CLK_ENABLE();
        __HAL_RCC_PWR_CLK_ENABLE();
        
        // Enable access to backup domain
        HAL_PWR_EnableBkUpAccess();
        
        // Enable LSE Oscillator
        __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
        
        // Wait till LSE is ready
        while(__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET);
        
        // Select LSE as RTC clock source
        __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
    }
}

/**
 * Assert handler pentru debug
 */
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {
    // Assert failed - debugging
    char buffer[100];
    sprintf(buffer, "ASSERT FAILED: %s:%lu\r\n", (char*)file, (unsigned long)line);
    UART_SendMessage(buffer);
    while (1);
}
#endif