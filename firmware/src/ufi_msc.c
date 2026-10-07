/**
 * UFI Flux Engine - USB mass storage mode: the SD NAND as a USB drive (board v0.6)
 *
 * ST USB device library MSC class (bulk-only transport, SCSI) on top of ufi_sd.c block
 * access.  Entered with button B held 2 s or USB_MSC; the CDC command interface is gone
 * until button B is held again or the device restarts.  Dumps are refused meanwhile,
 * so the host and the firmware never use the FAT volume at the same time.
 */

#include "ufi_firmware.h"
#include "usbd_msc.h"

#define STANDARD_INQUIRY_DATA_LEN_UFI   0x24u

static int8_t st_init(uint8_t lun)
{
    (void)lun;
    return ufi_sd_init() == UFI_OK ? 0 : -1;
}

static int8_t st_capacity(uint8_t lun, uint32_t* block_num, uint16_t* block_size)
{
    (void)lun;
    *block_num = ufi_sd_blocks();
    *block_size = 512;
    return *block_num ? 0 : -1;
}

static int8_t st_ready(uint8_t lun)
{
    (void)lun;
    return ufi_sd_present() ? 0 : -1;
}

static int8_t st_write_protected(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t st_read(uint8_t lun, uint8_t* buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    return ufi_sd_read(buf, blk_addr, blk_len) == UFI_OK ? 0 : -1;
}

static int8_t st_write(uint8_t lun, uint8_t* buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    return ufi_sd_write(buf, blk_addr, blk_len) == UFI_OK ? 0 : -1;
}

static int8_t st_max_lun(void)
{
    return 0;
}

/* SCSI standard inquiry data, 36 bytes */
static int8_t inquiry[STANDARD_INQUIRY_DATA_LEN_UFI] = {
    0x00,                                       /* direct access block device */
    0x80,                                       /* removable */
    0x02, 0x02,
    (STANDARD_INQUIRY_DATA_LEN_UFI - 5),
    0x00, 0x00, 0x00,
    'U', 'F', 'I', ' ', ' ', ' ', ' ', ' ',     /* vendor, 8 bytes */
    'F', 'l', 'u', 'x', ' ', 'S', 't', 'o',     /* product, 16 bytes */
    'r', 'a', 'g', 'e', ' ', ' ', ' ', ' ',
    '0', '.', '6', ' ',                         /* revision, 4 bytes */
};

USBD_StorageTypeDef ufi_msc_fops = {
    st_init, st_capacity, st_ready, st_write_protected, st_read, st_write, st_max_lun, inquiry,
};
