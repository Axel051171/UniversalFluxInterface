# UFI Firmware Code Review

## Gesamtbewertung: ⭐⭐⭐⭐ (Gut!)

Solide Basis mit durchdachter Architektur. Die Trennung STM32 ↔ CM5 ist korrekt.

---

## 🔴 KRITISCH (Sofort beheben)

### 1. Race Condition im Index-Interrupt (ufi_main.c:541)

```c
// PROBLEM: memcpy während DMA aktiv → korrupte Daten!
memcpy(rev->samples, g_flux_dma_buffer, dma_pos * sizeof(uint32_t));

// LÖSUNG: DMA erst stoppen
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (g_capture.state == CAPTURE_RUNNING) {
        // ⚠️ ERST DMA stoppen!
        HAL_DMA_Abort(&hdma_tim2);
        
        // Jetzt sicher kopieren
        uint32_t dma_pos = MAX_FLUX_PER_REV - __HAL_DMA_GET_COUNTER(&hdma_tim2);
        memcpy(rev->samples, g_flux_dma_buffer, dma_pos * sizeof(uint32_t));
        
        // DMA neu starten für nächste Revolution
        HAL_DMA_Start(&hdma_tim2, ...);
    }
}
```

### 2. IEC-Bus Endlosschleifen (ufi_main.c:495-503)

```c
// PROBLEM: Kein Timeout → System hängt wenn Laufwerk nicht antwortet
while (!iec_read_clk());   // ⚠️ ENDLOSSCHLEIFE!
while (iec_read_clk());    // ⚠️ ENDLOSSCHLEIFE!

// LÖSUNG: Timeout hinzufügen
#define IEC_TIMEOUT_CYCLES (10000 * (SYSCLK_FREQ / 1000000))

static int iec_wait_clk_high(void) {
    uint32_t start = DWT->CYCCNT;
    while (!iec_read_clk()) {
        if ((DWT->CYCCNT - start) > IEC_TIMEOUT_CYCLES)
            return -1;  // Timeout!
    }
    return 0;
}
```

### 3. USB Ring-Buffer Overflow (ufi_usb.c:126-134)

```c
// PROBLEM: Falsche Berechnung bei Wrap-Around
if (usb_tx_head >= usb_tx_tail) {
    free_space = USB_HS_BUFFER_SIZE - usb_tx_head + usb_tx_tail;
}

// LÖSUNG: Korrekte Ring-Buffer Logik
uint32_t used = (usb_tx_head >= usb_tx_tail) 
    ? (usb_tx_head - usb_tx_tail)
    : (USB_HS_BUFFER_SIZE - usb_tx_tail + usb_tx_head);
uint32_t free_space = USB_HS_BUFFER_SIZE - used - 1;  // -1 wichtig!
```

### 4. `__packed` nicht definiert (ufi_firmware.h:43)

```c
// PROBLEM: __packed ist kein Standard-C
typedef struct __packed { ... }  // Compiler-Fehler möglich!

// LÖSUNG: Am Anfang der Header-Datei
#ifndef __packed
  #ifdef __GNUC__
    #define __packed __attribute__((packed))
  #else
    #define __packed
  #endif
#endif
```

---

## 🟡 WICHTIG (Sollte behoben werden)

### 5. SystemClock_Config() fehlt

```c
// ufi_main.c:73 ruft auf, aber Funktion existiert nicht!
SystemClock_Config();  // ❌ Nicht implementiert

// LÖSUNG: Hinzufügen (550 MHz für STM32H723)
void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 5;    // 25MHz/5 = 5MHz
    osc.PLL.PLLN = 220;  // 5MHz*220 = 1100MHz
    osc.PLL.PLLP = 2;    // 1100MHz/2 = 550MHz
    HAL_RCC_OscConfig(&osc);
    
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_HCLK_DIV2;  // 275MHz für Timer!
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3);
}
```

### 6. DWT Cycle Counter nicht initialisiert

```c
// iec_delay_us() nutzt DWT->CYCCNT, aber nie aktiviert!

// LÖSUNG: In ufi_init() hinzufügen
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
```

### 7. Timer Handle nicht global (ufi_main.c:157)

```c
// PROBLEM: htim ist lokal → kann nicht in ISR verwendet werden
void ufi_timer_init(void) {
    TIM_HandleTypeDef htim = {0};  // ❌ Lokal!
}

// LÖSUNG: Global machen
static TIM_HandleTypeDef htim2;  // ✅ Global
static DMA_HandleTypeDef hdma_tim2;
```

### 8. USB sendet ohne Prüfung (ufi_usb.c:203-207)

```c
// PROBLEM: Mehrfache Transmit ohne auf Completion zu warten
USBD_CDC_TransmitPacket(&hUsbDevice);
HAL_Delay(1);  // ❌ Unsauber!
USBD_CDC_TransmitPacket(&hUsbDevice);

// LÖSUNG: Auf TX Complete warten
while (USBD_CDC_GetTxState(&hUsbDevice) != 0) {}
USBD_CDC_TransmitPacket(&hUsbDevice);
```

---

## 🟢 EMPFOHLEN (Nice to have)

### 9. Watchdog hinzufügen

```c
void ufi_watchdog_init(void) {
    IWDG_HandleTypeDef hiwdg = {
        .Instance = IWDG1,
        .Init.Prescaler = IWDG_PRESCALER_64,
        .Init.Reload = 4095  // ~8 Sekunden
    };
    HAL_IWDG_Init(&hiwdg);
}

// In main loop:
HAL_IWDG_Refresh(&hiwdg);
```

### 10. Error-Codes definieren

```c
typedef enum {
    UFI_OK              = 0,
    UFI_ERR_BUSY        = -1,
    UFI_ERR_NO_DRIVE    = -2,
    UFI_ERR_TIMEOUT     = -3,
    UFI_ERR_IEC_NRFD    = -4,
    UFI_ERR_IEC_NOACK   = -5,
    UFI_ERR_DMA         = -6,
} ufi_error_t;
```

### 11. Write-Support fehlt

```c
// Header deklariert UFI_CMD_WRITE_TRACK (0x30), aber nicht implementiert!
// Mindestens Stub hinzufügen:
case UFI_CMD_WRITE_TRACK:
    response.status = 0xFE;  // Nicht implementiert
    break;
```

### 12. LED-Blinken blockiert nicht

```c
// PROBLEM: Toggle ohne Delay → zu schnelles Blinken
if (g_capture.state == CAPTURE_RUNNING) {
    HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
}

// LÖSUNG: Mit Timer
static uint32_t last_blink = 0;
if (HAL_GetTick() - last_blink > 100) {
    HAL_GPIO_TogglePin(...);
    last_blink = HAL_GetTick();
}
```

---

## 📊 Zusammenfassung

| Kategorie | Anzahl | Priorität |
|-----------|--------|-----------|
| 🔴 Kritisch | 4 | Sofort |
| 🟡 Wichtig | 4 | Diese Woche |
| 🟢 Empfohlen | 4 | Später |

---

## ✅ Was bereits gut ist

- **Architektur**: STM32 für Echtzeit, CM5 für Analyse ✓
- **DMA für Flux**: Richtige Wahl für glitch-free Capture ✓
- **Memory Sections**: DTCM für DMA-Buffer ist optimal ✓
- **32-bit Timer**: TIM2 bei 275MHz perfekt für Flux ✓
- **GPIO-Struktur**: Übersichtlich und wartbar ✓
- **USB Protokoll**: Sauber definierte Commands ✓

---

## 🎯 Empfohlene Reihenfolge

1. ⬜ Race Condition in Index-ISR (#1)
2. ⬜ IEC Timeouts (#2)
3. ⬜ SystemClock_Config (#5)
4. ⬜ DWT aktivieren (#6)
5. ⬜ Timer Handle global (#7)
6. ⬜ USB Buffer Fix (#3)
7. ⬜ __packed Definition (#4)
8. ⬜ USB TX Completion (#8)
9. ⬜ Watchdog (#9)
10. ⬜ Error Codes (#10)
