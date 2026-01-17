/**
 * @file mig_dumper.h
 * @brief MIG Dumper USB Integration for UFI
 * 
 * This module provides integration with the MIG Switch Dumper device,
 * allowing UFI to detect, communicate with, and control the dumper
 * for Nintendo Switch game cartridge preservation.
 * 
 * The MIG Dumper appears as a USB Mass Storage device when connected.
 * This module handles:
 * - Device detection and identification
 * - Automatic mounting/unmounting
 * - Firmware version detection
 * - Dump file management
 * - Integration with UFI's preservation workflow
 * 
 * @note This module requires the MIG Dumper hardware and does NOT
 *       include or use any proprietary MIG firmware code.
 * 
 * Copyright (c) 2026 UFI Project
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef UFI_MIG_DUMPER_H
#define UFI_MIG_DUMPER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants and Definitions
 *============================================================================*/

/** MIG Dumper USB Vendor ID (to be determined by inspection) */
#define MIG_DUMPER_VID          0x1234  /* TODO: Determine actual VID */

/** MIG Dumper USB Product ID (to be determined by inspection) */
#define MIG_DUMPER_PID          0x5678  /* TODO: Determine actual PID */

/** Alternative: MIG may use ESP32-S2 default USB IDs */
#define ESP32_S2_VID            0x303A  /* Espressif */
#define ESP32_S2_PID            0x0002  /* ESP32-S2 */

/** MIG Dumper internal storage label */
#define MIG_DUMPER_VOLUME_LABEL "GameCard"

/** Maximum path length for dump files */
#define MIG_MAX_PATH            256

/** Firmware update filename */
#define MIG_FIRMWARE_FILENAME   "update.s2"

/** System folder on MIG Dumper */
#define MIG_SYSTEM_FOLDER       "System"

/** Dump file extensions */
#define MIG_XCI_EXTENSION       ".xci"
#define MIG_CERT_EXTENSION      " (Certificate).bin"
#define MIG_INITDATA_EXTENSION  " (Initial Data).bin"
#define MIG_CARDID_EXTENSION    " (Card ID Set).bin"
#define MIG_CARDUID_EXTENSION   " (Card UID).bin"

/*============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief MIG Dumper connection state
 */
typedef enum {
    MIG_STATE_DISCONNECTED = 0,     /**< No dumper connected */
    MIG_STATE_DETECTING,            /**< USB device detected, identifying */
    MIG_STATE_MOUNTING,             /**< Mounting filesystem */
    MIG_STATE_READY,                /**< Ready for operations */
    MIG_STATE_BUSY,                 /**< Operation in progress */
    MIG_STATE_ERROR,                /**< Error state */
    MIG_STATE_COUNT
} mig_state_t;

/**
 * @brief MIG Dumper LED state (as observed)
 */
typedef enum {
    MIG_LED_OFF = 0,
    MIG_LED_RED,
    MIG_LED_GREEN,
    MIG_LED_BLUE,
    MIG_LED_FLASHING_RED,
    MIG_LED_FLASHING_BLUE
} mig_led_state_t;

/**
 * @brief Game cartridge status
 */
typedef enum {
    MIG_CART_NONE = 0,              /**< No cartridge inserted */
    MIG_CART_INSERTED,              /**< Cartridge detected */
    MIG_CART_READING,               /**< Reading cartridge info */
    MIG_CART_READY,                 /**< Ready to dump */
    MIG_CART_DUMPING,               /**< Dump in progress */
    MIG_CART_ERROR                  /**< Read error */
} mig_cart_status_t;

/**
 * @brief Nintendo Switch ROM size enumeration
 */
typedef enum {
    MIG_ROM_SIZE_1GB  = 0xFA,
    MIG_ROM_SIZE_2GB  = 0xF8,
    MIG_ROM_SIZE_4GB  = 0xF0,
    MIG_ROM_SIZE_8GB  = 0xE0,
    MIG_ROM_SIZE_16GB = 0xE1,
    MIG_ROM_SIZE_32GB = 0xE2
} mig_rom_size_t;

/**
 * @brief MIG Dumper device information
 */
typedef struct {
    char device_path[MIG_MAX_PATH];     /**< e.g., "/dev/sda1" */
    char mount_point[MIG_MAX_PATH];     /**< e.g., "/mnt/mig" */
    char volume_label[32];              /**< Volume label */
    char firmware_version[16];          /**< e.g., "1.2.3" */
    uint64_t total_space;               /**< Total storage bytes */
    uint64_t free_space;                /**< Available bytes */
    mig_state_t state;                  /**< Current state */
    bool is_v2;                         /**< Hardware revision (V1 vs V2) */
} mig_device_info_t;

/**
 * @brief Switch game cartridge information
 */
typedef struct {
    char title[128];                    /**< Game title */
    char title_id[17];                  /**< Title ID (hex string) */
    char version[16];                   /**< Game version */
    mig_rom_size_t rom_size;            /**< ROM capacity */
    uint64_t rom_size_bytes;            /**< Actual data size */
    uint64_t trimmed_size;              /**< Trimmed size */
    bool has_update;                    /**< Contains system update */
    uint32_t update_version;            /**< Bundled update version */
    mig_cart_status_t status;           /**< Current status */
} mig_cartridge_info_t;

/**
 * @brief Dump operation options
 */
typedef struct {
    bool dump_xci;                      /**< Dump full XCI image */
    bool dump_certificate;              /**< Dump certificate separately */
    bool dump_initial_data;             /**< Dump initial data */
    bool dump_card_id_set;              /**< Dump card ID set */
    bool dump_card_uid;                 /**< Dump card UID */
    bool trim_xci;                      /**< Trim XCI to actual size */
    bool verify_after_dump;             /**< Verify dump integrity */
    char output_path[MIG_MAX_PATH];     /**< Output directory */
} mig_dump_options_t;

/**
 * @brief Dump progress information
 */
typedef struct {
    uint64_t total_bytes;               /**< Total bytes to dump */
    uint64_t bytes_written;             /**< Bytes written so far */
    uint32_t percent;                   /**< Completion percentage */
    uint32_t speed_kbps;                /**< Current speed in KB/s */
    uint32_t eta_seconds;               /**< Estimated time remaining */
    char current_file[64];              /**< Current file being dumped */
    bool cancelled;                     /**< Operation cancelled */
    bool error;                         /**< Error occurred */
    char error_message[128];            /**< Error description */
} mig_dump_progress_t;

/**
 * @brief Callback function for dump progress updates
 * @param progress Current progress information
 * @param user_data User-provided context pointer
 */
typedef void (*mig_progress_callback_t)(const mig_dump_progress_t *progress, void *user_data);

/**
 * @brief Dump result structure
 */
typedef struct {
    bool success;                       /**< Overall success */
    char xci_path[MIG_MAX_PATH];        /**< Path to XCI file */
    char cert_path[MIG_MAX_PATH];       /**< Path to certificate */
    char initdata_path[MIG_MAX_PATH];   /**< Path to initial data */
    char cardid_path[MIG_MAX_PATH];     /**< Path to card ID set */
    char carduid_path[MIG_MAX_PATH];    /**< Path to card UID */
    uint64_t total_size;                /**< Total bytes dumped */
    uint32_t elapsed_seconds;           /**< Time taken */
    bool verified;                      /**< Verification passed */
    char error_message[128];            /**< Error if failed */
} mig_dump_result_t;

/*============================================================================
 * Initialization and Cleanup
 *============================================================================*/

/**
 * @brief Initialize MIG Dumper subsystem
 * 
 * Sets up USB hotplug monitoring and prepares for device detection.
 * Must be called before any other mig_* functions.
 * 
 * @return true on success, false on failure
 */
bool mig_init(void);

/**
 * @brief Cleanup MIG Dumper subsystem
 * 
 * Unmounts any connected device and releases resources.
 */
void mig_exit(void);

/*============================================================================
 * Device Detection and Management
 *============================================================================*/

/**
 * @brief Check if MIG Dumper is connected
 * 
 * @return true if device is connected and ready
 */
bool mig_is_connected(void);

/**
 * @brief Get current device state
 * 
 * @return Current mig_state_t value
 */
mig_state_t mig_get_state(void);

/**
 * @brief Get device information
 * 
 * @param info Pointer to structure to fill
 * @return true on success
 */
bool mig_get_device_info(mig_device_info_t *info);

/**
 * @brief Wait for device connection
 * 
 * Blocks until device is connected or timeout occurs.
 * 
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return true if device connected, false on timeout
 */
bool mig_wait_for_device(uint32_t timeout_ms);

/**
 * @brief Force re-scan for MIG Dumper
 * 
 * Useful if device was connected before initialization.
 * 
 * @return true if device found
 */
bool mig_rescan(void);

/**
 * @brief Safely eject MIG Dumper
 * 
 * Unmounts filesystem and prepares for safe removal.
 * 
 * @return true on success
 */
bool mig_eject(void);

/*============================================================================
 * Cartridge Operations
 *============================================================================*/

/**
 * @brief Check if cartridge is inserted
 * 
 * @return true if cartridge is present
 */
bool mig_cartridge_present(void);

/**
 * @brief Get cartridge information
 * 
 * @param info Pointer to structure to fill
 * @return true on success
 */
bool mig_get_cartridge_info(mig_cartridge_info_t *info);

/**
 * @brief Wait for cartridge insertion
 * 
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return true if cartridge inserted, false on timeout
 */
bool mig_wait_for_cartridge(uint32_t timeout_ms);

/*============================================================================
 * Dump Operations
 *============================================================================*/

/**
 * @brief Initialize dump options with defaults
 * 
 * @param options Pointer to options structure
 */
void mig_dump_options_init(mig_dump_options_t *options);

/**
 * @brief Start dump operation
 * 
 * This is a non-blocking call. Use mig_dump_get_progress() to monitor.
 * 
 * @param options Dump configuration
 * @param callback Progress callback (can be NULL)
 * @param user_data User data for callback
 * @return true if dump started successfully
 */
bool mig_dump_start(const mig_dump_options_t *options,
                    mig_progress_callback_t callback,
                    void *user_data);

/**
 * @brief Get current dump progress
 * 
 * @param progress Pointer to structure to fill
 * @return true if dump is active
 */
bool mig_dump_get_progress(mig_dump_progress_t *progress);

/**
 * @brief Cancel ongoing dump operation
 * 
 * @return true if cancellation requested
 */
bool mig_dump_cancel(void);

/**
 * @brief Wait for dump completion
 * 
 * Blocks until dump completes or is cancelled.
 * 
 * @param result Pointer to result structure (can be NULL)
 * @return true if dump completed successfully
 */
bool mig_dump_wait(mig_dump_result_t *result);

/**
 * @brief Perform synchronous dump (blocking)
 * 
 * Convenience function that combines start and wait.
 * 
 * @param options Dump configuration
 * @param result Pointer to result structure
 * @param callback Progress callback (can be NULL)
 * @param user_data User data for callback
 * @return true on success
 */
bool mig_dump_sync(const mig_dump_options_t *options,
                   mig_dump_result_t *result,
                   mig_progress_callback_t callback,
                   void *user_data);

/*============================================================================
 * File Management
 *============================================================================*/

/**
 * @brief List dump files on MIG Dumper
 * 
 * @param paths Array to fill with file paths
 * @param max_count Maximum entries in array
 * @return Number of dump files found
 */
int mig_list_dumps(char paths[][MIG_MAX_PATH], int max_count);

/**
 * @brief Copy dump files to UFI storage
 * 
 * @param source_path Path on MIG Dumper
 * @param dest_path Destination path on UFI
 * @param callback Progress callback (can be NULL)
 * @param user_data User data for callback
 * @return true on success
 */
bool mig_copy_dump(const char *source_path,
                   const char *dest_path,
                   mig_progress_callback_t callback,
                   void *user_data);

/**
 * @brief Delete dump from MIG Dumper
 * 
 * @param path Path to dump folder or file
 * @return true on success
 */
bool mig_delete_dump(const char *path);

/**
 * @brief Get available space on MIG Dumper
 * 
 * @param free_bytes Pointer to store free space
 * @param total_bytes Pointer to store total space (can be NULL)
 * @return true on success
 */
bool mig_get_storage_space(uint64_t *free_bytes, uint64_t *total_bytes);

/*============================================================================
 * Firmware Management
 *============================================================================*/

/**
 * @brief Get current firmware version
 * 
 * @param version Buffer to store version string
 * @param size Buffer size
 * @return true on success
 */
bool mig_get_firmware_version(char *version, size_t size);

/**
 * @brief Check if firmware update is available
 * 
 * Compares device version with update.s2 in System folder.
 * 
 * @return true if update available
 */
bool mig_firmware_update_available(void);

/**
 * @brief Apply firmware update
 * 
 * Copies update.s2 to System folder and triggers update.
 * 
 * @param firmware_path Path to update.s2 file
 * @return true on success
 */
bool mig_firmware_update(const char *firmware_path);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Convert ROM size code to bytes
 * 
 * @param rom_size ROM size enumeration value
 * @return Size in bytes
 */
uint64_t mig_rom_size_to_bytes(mig_rom_size_t rom_size);

/**
 * @brief Get human-readable ROM size string
 * 
 * @param rom_size ROM size enumeration value
 * @return Static string like "8 GB"
 */
const char *mig_rom_size_to_string(mig_rom_size_t rom_size);

/**
 * @brief Get state name string
 * 
 * @param state State enumeration value
 * @return Static string
 */
const char *mig_state_to_string(mig_state_t state);

/**
 * @brief Format title ID as string
 * 
 * @param title_id 64-bit title ID
 * @param buffer Output buffer (at least 17 bytes)
 */
void mig_format_title_id(uint64_t title_id, char *buffer);

#ifdef __cplusplus
}
#endif

#endif /* UFI_MIG_DUMPER_H */
