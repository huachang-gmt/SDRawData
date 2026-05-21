# STM32H7 SDMMC SD Card Raw Data 高速盲寫專案

![STM32H7](https://img.shields.io/badge/MCU-STM32H755-blue.svg)
![Platform](https://img.shields.io/badge/Board-NUCLEO--H755ZI--Q-orange.svg)
![Protocol](https://img.shields.io/badge/Interface-SDMMC%204--bit%20%2B%20IDMA-red.svg)

本專案實現了基於 **STM32H755ZIT6U雙核微控製器**，利用硬體 **SDMMC1 4-bit 總線模式** 與 **內建 IDMA (Internal DMA)**，繞過傳統檔案系統，直接對 MicroSD 卡進行底層實體磁區（Physical Sector）的 Raw Data 高速盲寫。

此專案專為對寫入時限（Latency）有極端嚴苛要求的嵌入式 Data Logger 系統而設計。

---

## 📌 專案目的與背景

在常規的嵌入式開發中，通常會掛載 FATFS 等檔案系統來管理儲存媒介。然而在**高頻率資料採樣**的場景下，檔案系統會帶來無法接受的時延彈性（Jitter）：
- **傳統 File System 痛點**：即使在最理想狀態下，寫入檔案並執行 `f_sync()` 刷新組態表，至少需要花費 **1.8ms ~ 2.0ms**。當遇到 SD 卡內部進行垃圾回收（Garbage Collection）或跨 Cluster 寫入時，時延甚至會飆升至數十毫秒。
- **本專案核心目標**：擺脫檔案系統的管理開銷，實測將單次寫入時延壓低至 **1.0ms 以下（最優可達 0.5ms 以下）**，滿足高頻即時系統的資料不遺漏需求。

---

## 🛠️ 硬體平台與環境

### 硬體設備
- **微控製器 (MCU)**：STM32H755ZIT6U (Cortex-M7 @ 480MHz / Cortex-M4 @ 240MHz)
- **核心開發板**：NUCLEO-H755ZI-Q
- **SD 卡模組**：Arduino SD Card 外接模組（已確認硬體線路抗噪與電壓匹配）
- **支援 SD 卡種類**：MicroSD (SDSC) / MicroSDHC (實測 16GB FAT32 預格式化卡)

### 軟體工具鏈
- **開發環境**：STM32CubeIDE v1.13.0 或更新版本
- **組態工具**：STM32CubeMX
- **燒錄工具**：STM32CubeProgrammer / 板載 ST-LINK V3

---

## 📊 Raw Data 盲寫 vs File System 深度對比

| 特性 | Raw Data 直接實體磁區寫入 (本專案) | 傳統 File System (FATFS + f_sync) |
| :--- | :--- | :--- |
| **寫入速度 (Latency)** | **🚀 極快 (< 0.5ms ~ 1.0ms)** | 🐢 較慢 (1.8ms ~ 2.0ms+ 且有抖動) |
| **時間確定性 (Real-time)** | **極高**。每次皆為固定磁區定址搬移 | **低**。受檔案分配表(FAT)與碎裂化影響 |
| **電腦直接讀取** | ❌ 無法在檔案總管直接看到檔案（需工具軟體） |  可以在 Windows/Mac 檔案總管直接拖曳讀寫 |
| **硬體耗損 (WAF)** | 低。不需頻繁擦寫暫存結構區（如 FAT 表） | 高。每次同步皆需重複擦寫特定管理扇區 |
| **資料安全性** | 異常斷電時，已寫入的實體資料絕對存在 | 異常斷電時，若未及時 sync 容易導致全檔損毀 |

---

## ⚙️ CubeMX 關鍵設定注意項目 (核心防坑指南)

在 STM32H755 上組態 SDMMC + IDMA 有非常多的硬體陷阱，請務必嚴格遵循以下組態設定：

### 1. Clock Configuration (時鐘組態)
- **SDMMC 核心時鐘**：建議由 `PLL2R` 或 `PLL1Q` 驅動，頻率組態為 **120MHz**。
- **參數 `Clock Div` (sdmmc.c)**：
  - STM32H755 速度極快，初次實驗建議將 CubeMX 中的 `Clock Div` 設為 **4 或 8**（將 SDMMC 實際工作頻率降至 15MHz~30MHz），以確保在外接杜邦線或 Arduino 模組時，訊號不會因高頻反射干擾而報錯。

### 2. SDMMC1 參數組態
- **Mode**：`4-bit Wide Bus`
- **Hardware Flow Control**：`Disable`
- **Internal DMA (IDMA)**：`Enable` (H7 的 SDMMC 內建硬體 IDMA，無須另外組態常規的 DMA1/DMA2)。

### 3. MPU 與記憶體組態 (Memory Allocation)
> ⚠️ **這點最關鍵！** H7 的內建 IDMA **完全無法** 讀取 Cortex-M7 的 DTCM 記憶體（會直接觸發硬體錯誤或全零數據）。
- 必須在 `main.c` 中將寫入緩衝區（Buffer）強制指定到 **AXI SRAM (RAM_D1)** 區域。
- 緩衝區必須嚴格執行 **32-Byte 對齊**。

---

## 📐 系統架構與設計方法

### 系統分層
```text
  +---------------------------------------+
  |    Application Layer (主要應用程序)    |
  +---------------------------------------+
                      │ (調用 API 寫入 Log)
                      ▼
  +---------------------------------------+
  |       sd_filemanager.c / .h           | (高層管理：提供 Init 與測試介面)
  +---------------------------------------+
                      │
                      ▼
  +---------------------------------------+
  |          sdcard.c / .h                | (中介層：控制實體磁區與安全防禦)
  +---------------------------------------+
                      │
                      ▼
  +---------------------------------------+
  |   STM32 HAL Driver (HAL_SD_***)       | (底層硬體驅動：發動 IDMA 盲寫)
  +---------------------------------------+
                      │
                      ▼
  +---------------------------------------+
  |      MicroSD Card (實體硬體)           |
  +---------------------------------------+