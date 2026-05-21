/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "sdmmc.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* DUAL_CORE_BOOT_SYNC_SEQUENCE: Define for dual core boot synchronization    */
/*                             demonstration code based on hardware semaphore */
/* This define is present in both CM7/CM4 projects                            */
/* To comment when developping/debugging on a single core                     */
//#define DUAL_CORE_BOOT_SYNC_SEQUENCE

#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;

/* USER CODE BEGIN PV */

/* 借用自 sdmmc.c 的 SD 卡控制結構體 */
extern SD_HandleTypeDef hsd1;

/* 宣告測試結果與狀態變數 */
HAL_StatusTypeDef sd_res;
uint32_t start_sector = 20000; // 盲寫的起始實體磁區（跳過前段引導區，非常安全）

/* 核心防禦：強迫 log_buffer 組態在 AXI SRAM (RAM_D1) 區域，並做 32 位元組對齊 */
uint32_t sector_count = 8; // 一次寫入 8 個扇區 (4KB)

/* 緩衝區同步放大到 4096 bytes (4KB) */
__attribute__((section(".RAM_D1"))) ALIGN_32BYTES(uint8_t log_buffer[4096]);

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  int32_t timeout;
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_0 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  /* Wait until CPU2 boots and enters in stop mode or timeout*/
  timeout = 0xFFFF;
  while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0));
  if ( timeout < 0 )
  {
  Error_Handler();
  }
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
/* When system initialization is finished, Cortex-M7 will release Cortex-M4 by means of
HSEM notification */
/*HW semaphore Clock enable*/
__HAL_RCC_HSEM_CLK_ENABLE();
/*Take HSEM */
HAL_HSEM_FastTake(HSEM_ID_0);
/*Release HSEM in order to notify the CPU2(CM4)*/
HAL_HSEM_Release(HSEM_ID_0,0);
/* wait until CPU2 wakes up from stop mode */
timeout = 0xFFFF;
while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0));
if ( timeout < 0 )
{
Error_Handler();
}
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */
  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_YELLOW);
  BSP_LED_Init(LED_RED);
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  HAL_Delay(2000);
  

  MX_GPIO_Init();
  
  BSP_LED_On(LED_GREEN);
  HAL_Delay(2000);
  BSP_LED_Off(LED_GREEN);

  MX_SDMMC1_SD_Init();
  
  BSP_LED_On(LED_YELLOW);
  HAL_Delay(2000);
  BSP_LED_Off(LED_YELLOW);




  /* USER CODE BEGIN 2 */

  // 1. 強迫將 SD 卡與 MCU 暫存器狀態重設回初始狀態
  HAL_SD_DeInit(&hsd1); 
  HAL_Delay(100); // 讓電壓與線路沉澱一下
  
  // 2. 重新初始化 SDMMC 硬體，確保所有中斷 Flag、DMA 狀態完全清空
  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
      BSP_LED_On(LED_RED); // 如果連重新初始化都失敗，亮紅燈卡死
      while(1);
  }

  // 1. 初始化測試資料：將 4096 位元組的緩衝區填入固定數值（例如 0xAA）
  for(uint16_t idx = 0; idx < 4096; idx++)
  {
      log_buffer[idx] = 0xAA; 
  }

  // 2. 開機安全延遲，點亮綠燈 2 秒，隨後熄滅，準備進入測試
  BSP_LED_On(LED_GREEN);
  HAL_Delay(2000);
  BSP_LED_Off(LED_GREEN);

  hsd1.State = HAL_SD_STATE_READY; 
  
  volatile uint32_t i = 0;

  // 4. 開始連續 多 次的實體磁區全速寫入測試
  for(i = 0; i < 100; i++)
  {
      // 【防禦守衛】雖然去掉了 while，但在發動下一輪傳輸前，
      // 必須確保硬體狀態已經從上一次的中斷中恢復為 READY。
      while(hsd1.State != HAL_SD_STATE_READY)
      {
          __NOP(); 
      }

      uint32_t card_ready_timeout = 0x3FFFFFF;
      while(card_ready_timeout--)
      {
          // 這段代碼就是在向 SD 卡發送 CMD13 詢問它燒完沒。
          if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER)
          {
              break; 
          }
          __NOP();
      }
      if(card_ready_timeout == 0)
      {
          BSP_LED_On(LED_RED); // 卡片物理逾時鎖死
          while(1);
      }
    
      BSP_LED_On(LED_YELLOW); // 點亮黃燈代表進入傳輸階段  MCU 將資料傳輸到 SD 卡的 FIFO ，不包含 真正寫入到 SD 卡的 flash
        
      /* ==================== 【示波器觀測起點】 ==================== */
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET); // PA6 拉高

      // 確保 IDMA 抓到的資料絕對 100% 寫入完成
      __DSB();

      hsd1.ErrorCode = HAL_SD_ERROR_NONE;      

      // 修正後：讓起始地址隨著 i 跨步前進，每一次前進 8 個磁區 (i * sector_count)
      sd_res = HAL_SD_WriteBlocks_DMA(&hsd1, log_buffer, start_sector + (i * sector_count), sector_count);

      // 如果發射失敗（例如總線衝突或參數錯誤），立刻亮紅燈卡死
      if(sd_res != HAL_OK)
      {
          BSP_LED_On(LED_RED);
          while(1);
      }

      /* ==================== 【示波器觀測終點】 ==================== */

  }

  // 以下兩個 while 是為了確保 SD 卡 寫入都完成，讓三個 LED 都點亮 所設置，並不是必要的。
  /* ========================================================
     【最後一哩路守衛】：強迫等待最後一次 (i=9) 的 DMA 中斷完全結束
     ======================================================== */
  while(hsd1.State != HAL_SD_STATE_READY)
  {
      __NOP(); 
  }
  
  // 確保最後一次卡片內部的 Flash 也燒錄完成了
  while(HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
  {
      __NOP();
  }

  // 10 次寫入全部真正安全完工！此時點亮三燈，黃燈就不會再被中斷熄滅了！
  BSP_LED_On(LED_GREEN);
  BSP_LED_On(LED_YELLOW);
  BSP_LED_On(LED_RED);

  /* USER CODE END 2 */

  

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 9;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOMEDIUM;
  RCC_OscInitStruct.PLL.PLLFRACN = 3072;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief IDMA 中斷程式 ISR 
  * @retval None
  */
// 當 DMA 把 4096 位元組完全 『傳輸』 到  SD 卡 的 FIFO 之後，硬體中斷會自動跳進這裡
// 要注意區隔，傳輸到 SD 卡的 FIFO 是一件事，從 FIFO 把資料寫到 SD 卡內的 flash 又是一件事情，兩件事情各自佔用時間
// 傳輸到 SD 卡的 FIFO 目前量測的時間是 0.4ms 
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    if(hsd->Instance == SDMMC1)
    {
        /* ==================== 【示波器觀測終點】 ==================== */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); // PA6 在這裡拉低！
        
        BSP_LED_Off(LED_YELLOW); // 熄滅黃燈，代表這趟總線傳輸任務搞定
    }
}



void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    // 如果傳輸中出錯（例如 FIFO 溢出），會跳進這裡
}
/* USER CODE END 4 */
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
