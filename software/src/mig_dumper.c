/**
 * @file mig_dumper.c
 * @brief MIG Dumper USB Integration Implementation
 * 
 * Copyright (c) 2026 UFI Project
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mig_dumper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <libudev.h>
#include <errno.h>

/*============================================================================
 * Private Definitions
 *============================================================================*/

#define MIG_MOUNT_POINT         "/mnt/mig"
#define MIG_POLL_INTERVAL_MS    500
#define MIG_COPY_BUFFER_SIZE    (4 * 1024 * 1024)  /* 4 MB */

/* USB device identifiers to check */
static const struct {
    uint16_t vid;
    uint16_t pid;
    const char *name;
} mig_usb_ids[] = {
    { 0x303A, 0x0002, "ESP32-S2 (Generic)" },
    { 0x303A, 0x1001, "ESP32-S2 CDC" },
    /* Add actual MIG VID/PID when known */
    { 0, 0, NULL }
};

/*============================================================================
 * Private Types
 *============================================================================*/

typedef struct {
    /* Device state */
    mig_state_t state;
    mig_device_info_t device;
    mig_cartridge_info_t cartridge;
    
    /* USB monitoring */
    struct udev *udev;
    struct udev_monitor *udev_mon;
    int udev_fd;
    
    /* Thread management */
    pthread_t monitor_thread;
    pthread_mutex_t mutex;
    bool monitor_running;
    
    /* Dump operation */
    bool dump_active;
    mig_dump_options_t dump_options;
    mig_dump_progress_t dump_progress;
    mig_progress_callback_t dump_callback;
    void *dump_user_data;
    pthread_t dump_thread;
    
} mig_context_t;

/*============================================================================
 * Private Variables
 *============================================================================*/

static mig_context_t g_mig = {0};
static bool g_initialized = false;

/*============================================================================
 * Private Function Declarations
 *============================================================================*/

static void *mig_monitor_thread(void *arg);
static bool mig_detect_device(void);
static bool mig_mount_device(const char *dev_path);
static bool mig_unmount_device(void);
static bool mig_read_firmware_version(void);
static bool mig_parse_cartridge_info(void);
static void *mig_dump_thread(void *arg);
static bool mig_copy_file(const char *src, const char *dst, 
                          mig_progress_callback_t cb, void *ud,
                          uint64_t *progress_offset, uint64_t total);

/*============================================================================
 * Public Functions - Initialization
 *============================================================================*/

bool mig_init(void) {
    if (g_initialized) {
        return true;
    }
    
    memset(&g_mig, 0, sizeof(g_mig));
    
    /* Initialize mutex */
    if (pthread_mutex_init(&g_mig.mutex, NULL) != 0) {
        fprintf(stderr, "MIG: Failed to init mutex\n");
        return false;
    }
    
    /* Initialize udev for USB monitoring */
    g_mig.udev = udev_new();
    if (!g_mig.udev) {
        fprintf(stderr, "MIG: Failed to create udev context\n");
        pthread_mutex_destroy(&g_mig.mutex);
        return false;
    }
    
    /* Create udev monitor for USB events */
    g_mig.udev_mon = udev_monitor_new_from_netlink(g_mig.udev, "udev");
    if (g_mig.udev_mon) {
        udev_monitor_filter_add_match_subsystem_devtype(g_mig.udev_mon, "block", "partition");
        udev_monitor_filter_add_match_subsystem_devtype(g_mig.udev_mon, "usb", "usb_device");
        udev_monitor_enable_receiving(g_mig.udev_mon);
        g_mig.udev_fd = udev_monitor_get_fd(g_mig.udev_mon);
    }
    
    /* Create mount point */
    mkdir(MIG_MOUNT_POINT, 0755);
    
    /* Start monitor thread */
    g_mig.monitor_running = true;
    if (pthread_create(&g_mig.monitor_thread, NULL, mig_monitor_thread, NULL) != 0) {
        fprintf(stderr, "MIG: Failed to create monitor thread\n");
        g_mig.monitor_running = false;
    }
    
    /* Initial device scan */
    mig_detect_device();
    
    g_initialized = true;
    printf("MIG: Subsystem initialized\n");
    
    return true;
}

void mig_exit(void) {
    if (!g_initialized) {
        return;
    }
    
    /* Stop monitor thread */
    g_mig.monitor_running = false;
    if (g_mig.monitor_thread) {
        pthread_join(g_mig.monitor_thread, NULL);
    }
    
    /* Cancel any active dump */
    if (g_mig.dump_active) {
        mig_dump_cancel();
        pthread_join(g_mig.dump_thread, NULL);
    }
    
    /* Unmount device */
    mig_unmount_device();
    
    /* Cleanup udev */
    if (g_mig.udev_mon) {
        udev_monitor_unref(g_mig.udev_mon);
    }
    if (g_mig.udev) {
        udev_unref(g_mig.udev);
    }
    
    pthread_mutex_destroy(&g_mig.mutex);
    
    g_initialized = false;
    printf("MIG: Subsystem shutdown\n");
}

/*============================================================================
 * Public Functions - Device Management
 *============================================================================*/

bool mig_is_connected(void) {
    pthread_mutex_lock(&g_mig.mutex);
    bool connected = (g_mig.state == MIG_STATE_READY || 
                      g_mig.state == MIG_STATE_BUSY);
    pthread_mutex_unlock(&g_mig.mutex);
    return connected;
}

mig_state_t mig_get_state(void) {
    pthread_mutex_lock(&g_mig.mutex);
    mig_state_t state = g_mig.state;
    pthread_mutex_unlock(&g_mig.mutex);
    return state;
}

bool mig_get_device_info(mig_device_info_t *info) {
    if (!info) return false;
    
    pthread_mutex_lock(&g_mig.mutex);
    if (g_mig.state < MIG_STATE_READY) {
        pthread_mutex_unlock(&g_mig.mutex);
        return false;
    }
    memcpy(info, &g_mig.device, sizeof(*info));
    pthread_mutex_unlock(&g_mig.mutex);
    
    return true;
}

bool mig_wait_for_device(uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    
    while (elapsed < timeout_ms || timeout_ms == 0) {
        if (mig_is_connected()) {
            return true;
        }
        usleep(MIG_POLL_INTERVAL_MS * 1000);
        elapsed += MIG_POLL_INTERVAL_MS;
    }
    
    return false;
}

bool mig_rescan(void) {
    return mig_detect_device();
}

bool mig_eject(void) {
    pthread_mutex_lock(&g_mig.mutex);
    
    if (g_mig.state != MIG_STATE_READY) {
        pthread_mutex_unlock(&g_mig.mutex);
        return false;
    }
    
    /* Sync filesystem */
    sync();
    
    /* Unmount */
    bool result = mig_unmount_device();
    
    if (result) {
        g_mig.state = MIG_STATE_DISCONNECTED;
        printf("MIG: Device ejected safely\n");
    }
    
    pthread_mutex_unlock(&g_mig.mutex);
    return result;
}

/*============================================================================
 * Public Functions - Cartridge Operations
 *============================================================================*/

bool mig_cartridge_present(void) {
    /* Check for .nxindex file or any XCI files */
    if (!mig_is_connected()) return false;
    
    char path[MIG_MAX_PATH];
    snprintf(path, sizeof(path), "%s/.nxindex", g_mig.device.mount_point);
    
    struct stat st;
    return (stat(path, &st) == 0);
}

bool mig_get_cartridge_info(mig_cartridge_info_t *info) {
    if (!info || !mig_is_connected()) return false;
    
    pthread_mutex_lock(&g_mig.mutex);
    
    /* Try to parse cartridge info from device */
    if (!mig_parse_cartridge_info()) {
        pthread_mutex_unlock(&g_mig.mutex);
        return false;
    }
    
    memcpy(info, &g_mig.cartridge, sizeof(*info));
    pthread_mutex_unlock(&g_mig.mutex);
    
    return true;
}

bool mig_wait_for_cartridge(uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    
    while (elapsed < timeout_ms || timeout_ms == 0) {
        if (mig_cartridge_present()) {
            return true;
        }
        usleep(MIG_POLL_INTERVAL_MS * 1000);
        elapsed += MIG_POLL_INTERVAL_MS;
    }
    
    return false;
}

/*============================================================================
 * Public Functions - Dump Operations
 *============================================================================*/

void mig_dump_options_init(mig_dump_options_t *options) {
    if (!options) return;
    
    memset(options, 0, sizeof(*options));
    options->dump_xci = true;
    options->dump_certificate = true;
    options->dump_initial_data = true;
    options->dump_card_id_set = true;
    options->dump_card_uid = true;
    options->trim_xci = false;
    options->verify_after_dump = true;
    strncpy(options->output_path, "/var/lib/ufi/images", MIG_MAX_PATH - 1);
}

bool mig_dump_start(const mig_dump_options_t *options,
                    mig_progress_callback_t callback,
                    void *user_data) {
    if (!options || !mig_is_connected()) {
        return false;
    }
    
    pthread_mutex_lock(&g_mig.mutex);
    
    if (g_mig.dump_active) {
        pthread_mutex_unlock(&g_mig.mutex);
        return false;
    }
    
    /* Store options and callback */
    memcpy(&g_mig.dump_options, options, sizeof(*options));
    g_mig.dump_callback = callback;
    g_mig.dump_user_data = user_data;
    
    /* Initialize progress */
    memset(&g_mig.dump_progress, 0, sizeof(g_mig.dump_progress));
    
    /* Start dump thread */
    g_mig.dump_active = true;
    g_mig.state = MIG_STATE_BUSY;
    
    if (pthread_create(&g_mig.dump_thread, NULL, mig_dump_thread, NULL) != 0) {
        g_mig.dump_active = false;
        g_mig.state = MIG_STATE_READY;
        pthread_mutex_unlock(&g_mig.mutex);
        return false;
    }
    
    pthread_mutex_unlock(&g_mig.mutex);
    return true;
}

bool mig_dump_get_progress(mig_dump_progress_t *progress) {
    if (!progress) return false;
    
    pthread_mutex_lock(&g_mig.mutex);
    memcpy(progress, &g_mig.dump_progress, sizeof(*progress));
    bool active = g_mig.dump_active;
    pthread_mutex_unlock(&g_mig.mutex);
    
    return active;
}

bool mig_dump_cancel(void) {
    pthread_mutex_lock(&g_mig.mutex);
    
    if (!g_mig.dump_active) {
        pthread_mutex_unlock(&g_mig.mutex);
        return false;
    }
    
    g_mig.dump_progress.cancelled = true;
    pthread_mutex_unlock(&g_mig.mutex);
    
    return true;
}

bool mig_dump_wait(mig_dump_result_t *result) {
    if (!g_mig.dump_active && !g_mig.dump_thread) {
        return false;
    }
    
    pthread_join(g_mig.dump_thread, NULL);
    
    pthread_mutex_lock(&g_mig.mutex);
    g_mig.dump_thread = 0;
    
    if (result) {
        /* TODO: Fill result from dump progress */
        result->success = !g_mig.dump_progress.error && !g_mig.dump_progress.cancelled;
        result->total_size = g_mig.dump_progress.bytes_written;
    }
    
    pthread_mutex_unlock(&g_mig.mutex);
    
    return result ? result->success : true;
}

bool mig_dump_sync(const mig_dump_options_t *options,
                   mig_dump_result_t *result,
                   mig_progress_callback_t callback,
                   void *user_data) {
    if (!mig_dump_start(options, callback, user_data)) {
        return false;
    }
    
    return mig_dump_wait(result);
}

/*============================================================================
 * Public Functions - File Management
 *============================================================================*/

int mig_list_dumps(char paths[][MIG_MAX_PATH], int max_count) {
    if (!paths || max_count <= 0 || !mig_is_connected()) {
        return 0;
    }
    
    DIR *dir = opendir(g_mig.device.mount_point);
    if (!dir) return 0;
    
    int count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        /* Skip hidden files and special entries */
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, MIG_SYSTEM_FOLDER) == 0) continue;
        
        /* Check for XCI files or dump folders */
        char full_path[MIG_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", 
                 g_mig.device.mount_point, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            /* Check if it's a directory with XCI file or direct XCI */
            if (S_ISDIR(st.st_mode)) {
                char xci_path[MIG_MAX_PATH];
                snprintf(xci_path, sizeof(xci_path), "%s/%s.xci", 
                         full_path, entry->d_name);
                if (access(xci_path, F_OK) == 0) {
                    strncpy(paths[count++], full_path, MIG_MAX_PATH - 1);
                }
            } else if (strstr(entry->d_name, ".xci") != NULL) {
                strncpy(paths[count++], full_path, MIG_MAX_PATH - 1);
            }
        }
    }
    
    closedir(dir);
    return count;
}

bool mig_copy_dump(const char *source_path,
                   const char *dest_path,
                   mig_progress_callback_t callback,
                   void *user_data) {
    if (!source_path || !dest_path) return false;
    
    struct stat st;
    if (stat(source_path, &st) != 0) {
        return false;
    }
    
    /* Create destination directory */
    mkdir(dest_path, 0755);
    
    uint64_t total_size = 0;
    uint64_t progress = 0;
    
    /* Calculate total size first */
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(source_path);
        if (!dir) return false;
        
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            
            char file_path[MIG_MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s", 
                     source_path, entry->d_name);
            
            if (stat(file_path, &st) == 0) {
                total_size += st.st_size;
            }
        }
        closedir(dir);
        
        /* Copy all files */
        dir = opendir(source_path);
        if (!dir) return false;
        
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            
            char src_file[MIG_MAX_PATH], dst_file[MIG_MAX_PATH];
            snprintf(src_file, sizeof(src_file), "%s/%s", 
                     source_path, entry->d_name);
            snprintf(dst_file, sizeof(dst_file), "%s/%s", 
                     dest_path, entry->d_name);
            
            if (!mig_copy_file(src_file, dst_file, callback, user_data, 
                               &progress, total_size)) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);
    } else {
        /* Single file copy */
        char dst_file[MIG_MAX_PATH];
        const char *filename = strrchr(source_path, '/');
        filename = filename ? filename + 1 : source_path;
        snprintf(dst_file, sizeof(dst_file), "%s/%s", dest_path, filename);
        
        total_size = st.st_size;
        return mig_copy_file(source_path, dst_file, callback, user_data,
                             &progress, total_size);
    }
    
    return true;
}

bool mig_delete_dump(const char *path) {
    if (!path) return false;
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    
    if (S_ISDIR(st.st_mode)) {
        /* Delete all files in directory first */
        DIR *dir = opendir(path);
        if (!dir) return false;
        
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || 
                strcmp(entry->d_name, "..") == 0) continue;
            
            char file_path[MIG_MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s", 
                     path, entry->d_name);
            unlink(file_path);
        }
        closedir(dir);
        
        return rmdir(path) == 0;
    } else {
        return unlink(path) == 0;
    }
}

bool mig_get_storage_space(uint64_t *free_bytes, uint64_t *total_bytes) {
    if (!mig_is_connected()) return false;
    
    struct statvfs st;
    if (statvfs(g_mig.device.mount_point, &st) != 0) {
        return false;
    }
    
    if (free_bytes) {
        *free_bytes = (uint64_t)st.f_bavail * st.f_bsize;
    }
    if (total_bytes) {
        *total_bytes = (uint64_t)st.f_blocks * st.f_bsize;
    }
    
    return true;
}

/*============================================================================
 * Public Functions - Firmware
 *============================================================================*/

bool mig_get_firmware_version(char *version, size_t size) {
    if (!version || size == 0 || !mig_is_connected()) {
        return false;
    }
    
    pthread_mutex_lock(&g_mig.mutex);
    strncpy(version, g_mig.device.firmware_version, size - 1);
    version[size - 1] = '\0';
    pthread_mutex_unlock(&g_mig.mutex);
    
    return strlen(version) > 0;
}

bool mig_firmware_update_available(void) {
    /* TODO: Compare versions */
    return false;
}

bool mig_firmware_update(const char *firmware_path) {
    if (!firmware_path || !mig_is_connected()) {
        return false;
    }
    
    char dest_path[MIG_MAX_PATH];
    snprintf(dest_path, sizeof(dest_path), "%s/%s/%s",
             g_mig.device.mount_point, MIG_SYSTEM_FOLDER, MIG_FIRMWARE_FILENAME);
    
    uint64_t progress = 0;
    return mig_copy_file(firmware_path, dest_path, NULL, NULL, &progress, 0);
}

/*============================================================================
 * Public Functions - Utilities
 *============================================================================*/

uint64_t mig_rom_size_to_bytes(mig_rom_size_t rom_size) {
    switch (rom_size) {
        case MIG_ROM_SIZE_1GB:  return 1ULL << 30;
        case MIG_ROM_SIZE_2GB:  return 2ULL << 30;
        case MIG_ROM_SIZE_4GB:  return 4ULL << 30;
        case MIG_ROM_SIZE_8GB:  return 8ULL << 30;
        case MIG_ROM_SIZE_16GB: return 16ULL << 30;
        case MIG_ROM_SIZE_32GB: return 32ULL << 30;
        default: return 0;
    }
}

const char *mig_rom_size_to_string(mig_rom_size_t rom_size) {
    switch (rom_size) {
        case MIG_ROM_SIZE_1GB:  return "1 GB";
        case MIG_ROM_SIZE_2GB:  return "2 GB";
        case MIG_ROM_SIZE_4GB:  return "4 GB";
        case MIG_ROM_SIZE_8GB:  return "8 GB";
        case MIG_ROM_SIZE_16GB: return "16 GB";
        case MIG_ROM_SIZE_32GB: return "32 GB";
        default: return "Unknown";
    }
}

const char *mig_state_to_string(mig_state_t state) {
    static const char *names[] = {
        "Disconnected",
        "Detecting",
        "Mounting",
        "Ready",
        "Busy",
        "Error"
    };
    
    if (state < MIG_STATE_COUNT) {
        return names[state];
    }
    return "Unknown";
}

void mig_format_title_id(uint64_t title_id, char *buffer) {
    if (!buffer) return;
    snprintf(buffer, 17, "%016lX", title_id);
}

/*============================================================================
 * Private Functions
 *============================================================================*/

static void *mig_monitor_thread(void *arg) {
    (void)arg;
    
    fd_set fds;
    struct timeval tv;
    
    while (g_mig.monitor_running) {
        if (g_mig.udev_fd >= 0) {
            FD_ZERO(&fds);
            FD_SET(g_mig.udev_fd, &fds);
            tv.tv_sec = 0;
            tv.tv_usec = MIG_POLL_INTERVAL_MS * 1000;
            
            if (select(g_mig.udev_fd + 1, &fds, NULL, NULL, &tv) > 0) {
                struct udev_device *dev = udev_monitor_receive_device(g_mig.udev_mon);
                if (dev) {
                    const char *action = udev_device_get_action(dev);
                    
                    if (strcmp(action, "add") == 0) {
                        mig_detect_device();
                    } else if (strcmp(action, "remove") == 0) {
                        pthread_mutex_lock(&g_mig.mutex);
                        if (g_mig.state != MIG_STATE_DISCONNECTED) {
                            g_mig.state = MIG_STATE_DISCONNECTED;
                            printf("MIG: Device disconnected\n");
                        }
                        pthread_mutex_unlock(&g_mig.mutex);
                    }
                    
                    udev_device_unref(dev);
                }
            }
        } else {
            usleep(MIG_POLL_INTERVAL_MS * 1000);
        }
    }
    
    return NULL;
}

static bool mig_detect_device(void) {
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    bool found = false;
    
    if (!g_mig.udev) return false;
    
    enumerate = udev_enumerate_new(g_mig.udev);
    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_add_match_property(enumerate, "DEVTYPE", "partition");
    udev_enumerate_scan_devices(enumerate);
    
    devices = udev_enumerate_get_list_entry(enumerate);
    
    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(g_mig.udev, path);
        
        if (dev) {
            /* Get parent USB device */
            struct udev_device *usb = udev_device_get_parent_with_subsystem_devtype(
                dev, "usb", "usb_device");
            
            if (usb) {
                const char *vid_str = udev_device_get_sysattr_value(usb, "idVendor");
                const char *pid_str = udev_device_get_sysattr_value(usb, "idProduct");
                
                if (vid_str && pid_str) {
                    uint16_t vid = strtol(vid_str, NULL, 16);
                    uint16_t pid = strtol(pid_str, NULL, 16);
                    
                    /* Check against known MIG VID/PIDs */
                    for (int i = 0; mig_usb_ids[i].name != NULL; i++) {
                        if (vid == mig_usb_ids[i].vid && pid == mig_usb_ids[i].pid) {
                            const char *devnode = udev_device_get_devnode(dev);
                            
                            if (devnode) {
                                printf("MIG: Found potential device %s (%s)\n", 
                                       devnode, mig_usb_ids[i].name);
                                
                                pthread_mutex_lock(&g_mig.mutex);
                                g_mig.state = MIG_STATE_MOUNTING;
                                strncpy(g_mig.device.device_path, devnode, 
                                        MIG_MAX_PATH - 1);
                                pthread_mutex_unlock(&g_mig.mutex);
                                
                                if (mig_mount_device(devnode)) {
                                    found = true;
                                }
                            }
                            break;
                        }
                    }
                }
            }
            
            udev_device_unref(dev);
        }
        
        if (found) break;
    }
    
    udev_enumerate_unref(enumerate);
    
    return found;
}

static bool mig_mount_device(const char *dev_path) {
    /* Try to mount as vfat/exfat */
    if (mount(dev_path, MIG_MOUNT_POINT, "vfat", MS_NOATIME, 
              "utf8,umask=0000") != 0) {
        if (mount(dev_path, MIG_MOUNT_POINT, "exfat", MS_NOATIME, NULL) != 0) {
            fprintf(stderr, "MIG: Failed to mount %s: %s\n", 
                    dev_path, strerror(errno));
            pthread_mutex_lock(&g_mig.mutex);
            g_mig.state = MIG_STATE_ERROR;
            pthread_mutex_unlock(&g_mig.mutex);
            return false;
        }
    }
    
    pthread_mutex_lock(&g_mig.mutex);
    
    strncpy(g_mig.device.mount_point, MIG_MOUNT_POINT, MIG_MAX_PATH - 1);
    
    /* Get storage info */
    struct statvfs st;
    if (statvfs(MIG_MOUNT_POINT, &st) == 0) {
        g_mig.device.total_space = (uint64_t)st.f_blocks * st.f_bsize;
        g_mig.device.free_space = (uint64_t)st.f_bavail * st.f_bsize;
    }
    
    /* Read firmware version */
    mig_read_firmware_version();
    
    g_mig.state = MIG_STATE_READY;
    g_mig.device.state = MIG_STATE_READY;
    
    printf("MIG: Device mounted at %s (FW: %s)\n", 
           MIG_MOUNT_POINT, g_mig.device.firmware_version);
    
    pthread_mutex_unlock(&g_mig.mutex);
    
    return true;
}

static bool mig_unmount_device(void) {
    sync();
    
    if (umount(MIG_MOUNT_POINT) != 0) {
        /* Try lazy unmount */
        if (umount2(MIG_MOUNT_POINT, MNT_DETACH) != 0) {
            fprintf(stderr, "MIG: Failed to unmount: %s\n", strerror(errno));
            return false;
        }
    }
    
    return true;
}

static bool mig_read_firmware_version(void) {
    /* Look for version file in System folder */
    DIR *dir;
    struct dirent *entry;
    char system_path[MIG_MAX_PATH];
    
    snprintf(system_path, sizeof(system_path), "%s/%s", 
             MIG_MOUNT_POINT, MIG_SYSTEM_FOLDER);
    
    dir = opendir(system_path);
    if (!dir) {
        strcpy(g_mig.device.firmware_version, "Unknown");
        return false;
    }
    
    /* Look for version marker file (e.g., "1.2.3") */
    while ((entry = readdir(dir)) != NULL) {
        /* Version files are typically just numbers like "1.2.3" */
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') {
            /* Check if it looks like a version number */
            if (strchr(entry->d_name, '.') != NULL) {
                strncpy(g_mig.device.firmware_version, entry->d_name, 15);
                closedir(dir);
                return true;
            }
        }
    }
    
    closedir(dir);
    strcpy(g_mig.device.firmware_version, "Unknown");
    return false;
}

static bool mig_parse_cartridge_info(void) {
    /* Parse .nxindex or scan for XCI files */
    /* This is device-specific and may need adjustment */
    
    memset(&g_mig.cartridge, 0, sizeof(g_mig.cartridge));
    g_mig.cartridge.status = MIG_CART_NONE;
    
    /* TODO: Implement actual parsing based on MIG Dumper behavior */
    
    return false;
}

static void *mig_dump_thread(void *arg) {
    (void)arg;
    
    /* The actual dump is performed by the MIG Dumper hardware
     * We just monitor and copy the results */
    
    pthread_mutex_lock(&g_mig.mutex);
    
    /* Wait for dump files to appear */
    /* TODO: Implement actual monitoring */
    
    g_mig.dump_active = false;
    g_mig.state = MIG_STATE_READY;
    
    pthread_mutex_unlock(&g_mig.mutex);
    
    return NULL;
}

static bool mig_copy_file(const char *src, const char *dst,
                          mig_progress_callback_t cb, void *ud,
                          uint64_t *progress_offset, uint64_t total) {
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) return false;
    
    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        close(src_fd);
        return false;
    }
    
    char *buffer = malloc(MIG_COPY_BUFFER_SIZE);
    if (!buffer) {
        close(src_fd);
        close(dst_fd);
        return false;
    }
    
    ssize_t bytes_read;
    bool success = true;
    
    while ((bytes_read = read(src_fd, buffer, MIG_COPY_BUFFER_SIZE)) > 0) {
        ssize_t bytes_written = write(dst_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            success = false;
            break;
        }
        
        if (progress_offset) {
            *progress_offset += bytes_read;
        }
        
        if (cb && total > 0) {
            mig_dump_progress_t prog = {0};
            prog.total_bytes = total;
            prog.bytes_written = progress_offset ? *progress_offset : 0;
            prog.percent = (uint32_t)((prog.bytes_written * 100) / total);
            cb(&prog, ud);
        }
    }
    
    free(buffer);
    close(dst_fd);
    close(src_fd);
    
    return success && bytes_read == 0;
}
