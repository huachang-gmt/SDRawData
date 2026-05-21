# STM32H7 SDMMC SD Card Raw Data 高速盲寫專案

![STM32H7](https://img.shields.io/badge/MCU-STM32H755-blue.svg)
![Platform](https://img.shields.io/badge/Board-NUCLEO--H755ZI--Q-orange.svg)
![Protocol](https://img.shields.io/badge/Interface-SDMMC%204--bit%20%2B%20IDMA-red.svg)

本專案實現了基於 **STM32H755ZIT6U 雙核微控制器**，利用硬體 **SDMMC1 4-bit 總線模式** 與 **內建 IDMA (Internal DMA)**，繞過傳統檔案系統，直接對 MicroSD 卡進行底層實體磁區（Physical Sector）的 Raw Data 高速盲寫。

此專案專為對寫入時限（Latency）有極端嚴苛要求的嵌入式 Data Logger 系統而設計。

---

# 📌 專案目的與背景

在常規的嵌入式開發中，通常會掛載 FATFS 等檔案系統來管理儲存媒介。然而在 **高頻率資料採樣** 的場景下，檔案系統會帶來無法接受的時延彈性（Jitter）：

- **傳統 File System 痛點**
  - 即使在最理想狀態下，寫入檔案並執行 `f_sync()` 刷新組態表，至少需要花費 **1.8ms ~ 2.0ms**
  - 當遇到 SD 卡內部進行垃圾回收（Garbage Collection）或跨 Cluster 寫入時，時延甚至會飆升至數十毫秒

- **本專案核心目標**
  - 擺脫檔案系統的管理開銷
  - 實測將單次寫入時延壓低至 **1.0ms 以下**
  - 最佳情況可達 **0.5ms 以下**
  - 滿足高頻即時系統的資料不遺漏需求

---

# 🛠️ 硬體平台與環境

## 硬體設備

| 項目 | 說明 |
| :--- | :--- |
| 微控制器 (MCU) | STM32H755ZIT6U (Cortex-M7 @ 480MHz / Cortex-M4 @ 240MHz) |
| 核心開發板 | NUCLEO-H755ZI-Q |
| SD 卡模組 | DIGILENT Pmod MicroSD 卡模組 |
| 測試 SD 卡 | FAT32 / 16GB / SDHC / Allocation Unit = 8192 bytes |
| 支援 SD 卡種類 | MicroSD (SDSC) / MicroSDHC |

---

## 軟體工具鏈

| 工具 | 說明 |
| :--- | :--- |
| 開發環境 | STM32CubeIDE v2.1.1 |
| 組態工具 | STM32CubeMX |
| 燒錄工具 | STM32CubeProgrammer / 板載 ST-LINK V3 |

---

# 📊 Raw Data 盲寫 vs File System 深度對比

| 特性 | Raw Data 直接實體磁區寫入 (本專案) | 傳統 File System (FATFS + f_sync) |
| :--- | :--- | :--- |
| **寫入速度 (Latency)** | 🚀 極快 (< 0.5ms ~ 1.0ms) | 🐢 較慢 (1.8ms ~ 2.0ms+ 且有抖動) |
| **時間確定性 (Real-time)** | 極高，每次皆為固定磁區定址搬移 | 低，受 FAT 表與碎裂化影響 |
| **電腦直接讀取** | ❌ 無法在檔案總管直接看到檔案 | ✅ 可直接拖曳讀寫 |
| **硬體耗損 (WAF)** | 低，不需頻繁擦寫 FAT 管理區 | 高，每次同步皆需重複擦寫 |
| **資料安全性** | 已寫入資料實體存在 | 異常斷電可能導致整檔損毀 |

---

# ⚙️ CubeMX 關鍵設定注意項目（核心防坑指南）

在 STM32H755 上組態 SDMMC + IDMA 有非常多的硬體陷阱，請務必嚴格遵循以下設定。

---

## 1. Clock Configuration（時鐘組態）

### SDMMC 核心時鐘

建議由：

- `PLL2R`
- 或 `PLL1Q`

驅動，頻率建議配置為：

```text
120MHz
```

### Clock Div（sdmmc.c）

STM32H755 速度極快，初次實驗建議：

```text
Clock Div = 4 或 8
```

將 SDMMC 工作頻率降低至：

```text
15MHz ~ 30MHz
```

避免外接杜邦線或 Arduino 模組時產生高頻反射與訊號完整性問題。

---

## 2. SDMMC1 參數組態

| 參數 | 設定 |
| :--- | :--- |
| Mode | `4-bit Wide Bus` |
| Hardware Flow Control | `Enable` |
| Internal DMA (IDMA) | `Enable` |

> STM32H7 的 SDMMC 內建 IDMA，無須再額外配置 DMA1 / DMA2。

---

## 3. MPU 與記憶體組態（最重要）

> ⚠️ H7 的 IDMA **無法存取 Cortex-M7 的 DTCM RAM**

若 Buffer 放在 DTCM：

- 可能直接硬體錯誤
- 或寫入全零資料

因此：

- Buffer 必須放在 **AXI SRAM (RAM_D1)**
- 並且必須做 **32-byte alignment**

---

# 📐 系統架構與設計方法

## 系統分層架構

```text
+---------------------------------------+
|    Application Layer (主要應用程序)    |
+---------------------------------------+
                    │
                    ▼
+---------------------------------------+
|       sd_filemanager.c / .h           |
|       高層 API 與初始化管理             |
+---------------------------------------+
                    │
                    ▼
+---------------------------------------+
|          sdcard.c / .h                |
|       實體磁區控制與安全防禦             |
+---------------------------------------+
                    │
                    ▼
+---------------------------------------+
|   STM32 HAL Driver (HAL_SD_***)       |
|       底層硬體 IDMA Driver             |
+---------------------------------------+
                    │
                    ▼
+---------------------------------------+
|      MicroSD Card (實體硬體)           |
+---------------------------------------+
```

---

# 🧠 核心防禦性設計方法

本專案在追求極致速度的同時，也加入了兩大底層防禦機制。

---

## 1. 4KB 邊界對齊與地址跨步（Address Stride）

為了迎合 SD 卡內部 Flash Page 擦寫特性：

- 單次寫入固定為：

```text
8 Sector = 4096 Bytes = 4KB
```

- 定址方式：

```c
start_sector + (i * sector_count)
```

### 好處

- 避免地址重疊
- 避免 SD 卡內部 Busy 鎖死
- 提升連續寫入穩定性

---

## 2. 雙階段硬體狀態校驗與總線優化

在 IDMA 發動寫入後：

- 不採用盲目輪詢
- 改為檢查：
  - MCU SDMMC 狀態機
  - SD 卡控制器狀態

等待期間加入：

```c
__NOP();
```

### 目的

- 釋放 H7 高速 AXI Bus
- 避免 CPU 造成 DMA 總線擁堵
- 提高 DMA 穩定性

---

# 📂 專案目錄結構

```text
Core/
├── Inc/
│   ├── main.h
│   ├── sdcard.h
│   └── sd_filemanager.h
│
├── Src/
│   ├── main.c
│   ├── sdcard.c
│   └── sd_filemanager.c
```

---

# 🚀 關鍵代碼與範例

## 1. 記憶體強制定向與對齊（AXI SRAM）

```c
/* 核心防禦：
 * 強迫 log_buffer 放在 AXI SRAM (RAM_D1)
 * 並進行 32-byte 對齊
 */

uint32_t start_sector = 20000;
uint32_t sector_count = 8;

__attribute__((section(".RAM_D1")))
ALIGN_32BYTES(uint8_t log_buffer[4096]);
```

---

## 2. 跨步高速寫入核心迴圈

```c
for(i = 0; i < 10; i++)
{
    BSP_LED_On(LED_YELLOW);

    // 確保 IDMA 抓到的資料完全同步
    __DSB();

    hsd1.State = HAL_SD_STATE_READY;
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;

    // 地址跨步避免踩踏
    sd_res = HAL_SD_WriteBlocks_DMA(
                &hsd1,
                log_buffer,
                start_sector + (i * sector_count),
                sector_count);

    if(sd_res == HAL_OK)
    {
        uint32_t timeout = 0x3FFFFFF;

        while(timeout--)
        {
            if(hsd1.State == HAL_SD_STATE_READY)
            {
                break;
            }

            __NOP();
        }

        if(timeout == 0)
        {
            sd_res = HAL_ERROR;
        }
    }

    BSP_LED_Off(LED_YELLOW);

    if(sd_res != HAL_OK)
    {
        BSP_LED_On(LED_RED);

        while(1);
    }

    HAL_Delay(200);
}
```

---

# ⚠️ 專案限制與缺點（Limitations）

---

## 1. 電腦無法直接辨識資料

由於繞過 FAT Table：

- Windows 可能仍看到舊 FAT32 檔案
- 或提示需要格式化

### 讀取方式

需自行撰寫：

- STM32 讀取程式
- Python / C# 實體磁區讀取工具

---

## 2. 容量邊界需自行維護

沒有檔案系統保護：

- 必須自行管理目前寫入到哪個 Sector
- 超出容量將導致硬體錯誤

---

# 💡 故障排除與除錯提示（FAQ）

---

## Q1：黃燈恆亮或紅燈卡死怎麼辦？

### 原因

通常是：

- 未完全斷電重新燒錄
- SD 卡仍處於 Busy 狀態

### 解法

1. 完全斷電
2. 將 SD 卡拔下
3. 插回電腦讓 OS 重新讀取
4. 再插回 STM32 開發板

即可恢復正常。

---

## Q2：如何驗證資料真的有寫入？

### 方法 1：STM32 回讀驗證

使用：

```c
HAL_SD_ReadBlocks_DMA()
```

讀回同一實體磁區後比對 Buffer。

---

### 方法 2：使用磁碟十六進位工具

例如：

- WinHex
- HxD

直接跳轉：

```text
Sector 20000
```

即可查看 Raw Data。

---

# 📌 專案適用場景

本專案特別適合：

- 高速 Data Logger
- ADC 高頻採樣儲存
- 工業即時記錄系統
- IMU / Sensor Burst Logging
- 高速事件記錄器
- Real-time Edge AI 資料快取

---

# 📄 License

本專案可自由學習與修改使用。

建議實際產品化前：

- 重新驗證 SD 卡穩定性
- 驗證長時間連續寫入可靠度
- 建立完整 Wear-Leveling 策略

---