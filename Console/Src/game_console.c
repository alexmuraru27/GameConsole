#include "game_console.h"
#include <stm32f407xx.h> /* NVIC_SystemReset() */
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Peripherals/usart.h"
#include "Network/network.h"
#include "Devices/joystick.h"
#include "Renderer/renderer.h"
#include "Devices/buzzer.h"
#include "Devices/backlight.h"
#include "Peripherals/gpio.h"
#include "Devices/esp01.h"
#include "Peripherals/dma.h"
#include "Devices/ILI9341.h"
#include "Peripherals/adc.h"
#include "Peripherals/rng.h"
#include "Peripherals/timer.h"
#include "Peripherals/sdio.h"
#include "ff.h"
#include <string.h>
#include "Loader/asset_loader.h"
#include "Loader/loader.h"
#include "Loader/loader_media.h"
#include "Peripherals/i2c.h"
#include "Devices/external_eeprom.h"
#include "SettingsStorage/settings_storage.h"
#include "SettingsStorage/console_settings_storage.h"
#include "Swo/swo.h"
#include "Logger/logger.h"
#include "Fonts/fonts.h"
#include "Fonts/font_utils.h"
#include "Kernel/faults.h"
#include "Kernel/syscall.h"
#include "Kernel/os_services.h"
#include "MainMenu/keyboard.h"
#include "Kernel/mpu.h"
#include "Peripherals/watchdog.h"
#include <stdio.h>

const uint16_t s_boot_notes[] = {
    NOTE_D4, 450, NOTE_FS4, 400, NOTE_A4, 350, NOTE_D5, 300, NOTE_FS5, 250, NOTE_A5, 200,
    NOTE_D6, 150, NOTE_A5, 200, NOTE_FS5, 250, NOTE_D5, 300, NOTE_A4, 600};

static void playBootSong()
{
    buzzerPlay(0, false, s_boot_notes, sizeof(s_boot_notes) / sizeof(uint16_t) / 2U);
}

static const uint16_t s_step_notes[] = {
    NOTE_C4,
    80,
    NOTE_D4,
    80,
    NOTE_E4,
    80,
    NOTE_F4,
    80,
    NOTE_G4,
    80,
    NOTE_A4,
    80,
    NOTE_B4,
    80,
    NOTE_C5,
    80,
    NOTE_D5,
    80,
    NOTE_E5,
    80,
    NOTE_F5,
    80,
};

/* Boot-progress beep cursor. Each phase calls beepNext() in bring-up order; the
 * step index auto-advances so inserting or removing an init step can never
 * desync the ascending scale from s_step_notes[] (it is reset at the top of
 * gameConsoleInit). */
static uint8_t s_boot_step;

static void beepStep(uint8_t step)
{
    if (step >= (sizeof(s_step_notes) / sizeof(uint16_t) / 2U))
    {
        LOGGER_LOG_ERROR(LOGGER_CORE, "beepStep: step %u out of range", step);
        return;
    }
    buzzerPlay(0, false, &s_step_notes[step * 2U], 1);
    delay(150);
}

static void beepNext(void)
{
    beepStep(s_boot_step++);
}

/* The minimal core every later phase (and the boot beeps) depends on: trace,
 * GPIO, timers, buzzer. Silent — runs before the mute flag is known. */
static void coreInit()
{
    swoInit(2000000);
    LOGGER_LOG_INFO(LOGGER_CORE, "=== GameConsole boot ===");
    faultsInit();   /* enable + decode MemManage/Bus/Usage faults once SWO is up */
    syscallInit();  /* set SVC/PendSV priorities for the game syscall trap */
    gpioInit();
    timerInit();
    buzzerInit();
    backlightInit(); /* PA3 PWM up (default brightness) before the panel comes up */
    LOGGER_LOG_INFO(LOGGER_CORE, "core up: trace/faults/syscall/gpio/timers/buzzer/backlight");
}

/* The I2C storage stack (EEPROM + settings). Brought up on its own, before any
 * boot sound, so the persisted mute flag can be read and applied first; none of
 * it depends on the display, DMA or SD card. Silent. */
static void storageInit()
{
    i2cInit();
    externalEepromInit(EXTERNAL_EEPROM_AT24C512_ADDRESS);
    settingsStorageInit();
    LOGGER_LOG_INFO(LOGGER_CORE, "storage up: I2C/EEPROM/settings");
}

/* Apply the persisted console settings to the hardware. Runs before the boot
 * scale and boot song so a console muted by the user boots silent. The settings
 * menu owns the editable copy and the write-through path; this is the
 * read-and-apply side. */
static void applyConsoleSettings(void)
{
    ConsoleSettings cs;
    consoleSettingsLoad(&cs); /* fills defaults on miss/corrupt */
    buzzerSetMute(!cs.audio_enabled);
    backlightSetBrightness(cs.brightness);
}

static void peripheralsInit()
{
    beepNext();
    dmaInit((uint32_t)FSMC_DATA_ADDRESS);
    beepNext();
    usartInit();
    networkInit();
    beepNext();
    adcInit();
    rngInit();
    LOGGER_LOG_INFO(LOGGER_CORE, "peripherals up: DMA/USART/ADC/RNG");
}

static void devicesInit()
{
    beepNext();
    ili9341Init(1U, ILI9341_WIDTH, ILI9341_HEIGHT);
    beepNext();
    rendererInit();
    beepNext();
    joystickInit();
    beepNext();
    loaderMediaInit();
    beepNext();
    LOGGER_LOG_INFO(LOGGER_CORE, "devices up: display/renderer/joystick/SD");
}

void gameConsoleInit()
{
    s_boot_step = 0U;
    coreInit();
    storageInit();
    applyConsoleSettings(); /* honor the persisted mute before the first beep */

    peripheralsInit();
    devicesInit();

    /* Wire the app's on-screen keyboard into the kernel's osTextInput service, so
     * the kernel reaches it downward instead of including the menu UI. */
    osServicesSetTextInput(keyboardModal);

    mpuInit(); /* arm MPU confinement before any game can run */
    beepNext();
    playBootSong();
    watchdogInit(); /* arm the last-resort reset backstop now that bring-up is done */
    LOGGER_LOG_INFO(LOGGER_CORE, "console ready");
}

void gameConsoleReboot(void)
{
    LOGGER_LOG_INFO(LOGGER_CORE, "reboot requested");
    /* Reboot the ESP-01 along with the console: hold its EN low so the module is
     * disabled across the reset. peripheralsInit() -> networkInit() drives EN
     * high again on the next boot, so the ESP comes up fresh (e.g. running
     * firmware just flashed by the WiFi upgrade). */
    esp01SetEnable(false);
    delay(20U);
    NVIC_SystemReset(); /* full MCU reset; does not return */
}