/*
 * t4_sd.c — T4 emulator SD card block device driver
 *
 * Exposes the memory-mapped SD card (128MB @ 0x20000000) as a Linux
 * block device /dev/t4sd.  Uses the modern submit_bio path, which bypasses
 * the I/O scheduler and handles bios directly.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/bio.h>
#include <linux/io.h>

#define SD_BASE         0x20000000
#define SD_SIZE         (128 * 1024 * 1024)  /* 128MB */
#define SD_SECTOR_SIZE  512
#define SD_SECTORS      (SD_SIZE / SD_SECTOR_SIZE)
#define SD_MAJOR        240 /* local/experimental, stable for early userspace */
#define SD_MINORS       16  /* whole disk plus MBR partitions */
#define SD_NAME         "t4sd"

static int t4sd_major;
static struct gendisk *t4sd_disk;

/*
 * Copy between a bio segment and the MMIO SD region.
 * kaddr is the mapped page address + bv_offset.
 */
static int t4sd_transfer(unsigned long sd_off, void *buf,
                         unsigned int len, int write)
{
    volatile unsigned char *sd;
    unsigned char *p;
    unsigned long i;

    if (sd_off > SD_SIZE || len > SD_SIZE - sd_off)
        return -EIO;

    sd = (volatile unsigned char *)(SD_BASE + sd_off);
    p = (unsigned char *)buf;

    if (write) {
        for (i = 0; i < len; i++)
            sd[i] = p[i];
    } else {
        for (i = 0; i < len; i++)
            p[i] = sd[i];
    }

    return 0;
}

/*
 * make_request handler — processes bios directly, no I/O scheduler.
 */
static void t4sd_submit_bio(struct bio *bio)
{
    struct bio_vec *bvec;
    struct bvec_iter iter;
    unsigned long sd_off;
    int rw;

    sd_off = (unsigned long)bvec_iter_sector32(&bio->bi_iter) << SECTOR_SHIFT;
    if (sd_off > SD_SIZE || bio->bi_iter.bi_size > SD_SIZE - sd_off) {
        bio_io_error(bio);
        return;
    }

    rw = bio_data_dir(bio);

    iter = bio->bi_iter;
    while (iter.bi_size) {
	unsigned int offset;
        unsigned int len;
	struct page *page;
	void *buf;

	bvec = &bio->bi_io_vec[iter.bi_idx];
	offset = bvec->bv_offset + iter.bi_bvec_done;
	len = bvec->bv_len - iter.bi_bvec_done;
	if (len > iter.bi_size)
		len = iter.bi_size;
	if (len > PAGE_SIZE - (offset & (PAGE_SIZE - 1)))
		len = PAGE_SIZE - (offset & (PAGE_SIZE - 1));
	page = bvec->bv_page + (offset >> PAGE_SHIFT);
	buf = page_address(page) + (offset & (PAGE_SIZE - 1));

	if (t4sd_transfer(sd_off, buf, len, (rw != READ)) < 0) {
		bio_io_error(bio);
		return;
	}
	sd_off += len;
	iter.bi_size -= len;
	iter.bi_bvec_done += len;
	if (iter.bi_bvec_done == bvec->bv_len) {
		iter.bi_bvec_done = 0;
		iter.bi_idx++;
	}
    }

    bio_endio(bio);
}

/*
 * Geometry — needed for fdisk/partitioning tools.
 */
static int t4sd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
    geo->heads = 16;
    geo->sectors = 32;
    geo->cylinders = SD_SECTORS / (16 * 32);
    geo->start = 0;
    return 0;
}

static struct block_device_operations t4sd_ops = {
    .owner  = THIS_MODULE,
    .getgeo = t4sd_getgeo,
    .submit_bio = t4sd_submit_bio,
};

static int __init t4sd_init(void)
{
    struct queue_limits lim = {
        .logical_block_size = SD_SECTOR_SIZE,
        .physical_block_size = SD_SECTOR_SIZE,
    };
    int ret;

    ret = register_blkdev(SD_MAJOR, SD_NAME);
    if (ret < 0) {
        printk("t4sd: unable to register block device\n");
        return ret;
    }
    t4sd_major = SD_MAJOR;

    t4sd_disk = blk_alloc_disk(&lim, NUMA_NO_NODE);
    if (IS_ERR(t4sd_disk)) {
        ret = PTR_ERR(t4sd_disk);
        unregister_blkdev(t4sd_major, SD_NAME);
	return ret;
    }

    t4sd_disk->major = t4sd_major;
    t4sd_disk->first_minor = 0;
	 t4sd_disk->minors = SD_MINORS;
    t4sd_disk->fops = &t4sd_ops;
    {
        t4sd_disk->disk_name[0] = 't';
        t4sd_disk->disk_name[1] = '4';
        t4sd_disk->disk_name[2] = 's';
        t4sd_disk->disk_name[3] = 'd';
        t4sd_disk->disk_name[4] = 0;
    }
    set_capacity(t4sd_disk, SD_SECTORS);
    ret = add_disk(t4sd_disk);
    if (ret) {
        put_disk(t4sd_disk);
        unregister_blkdev(t4sd_major, SD_NAME);
        return ret;
    }

    printk("t4sd: 128MB SD card at 0x%x (%d sectors, major=%d)\n",
           SD_BASE, SD_SECTORS, t4sd_major);

    return 0;
}

device_initcall(t4sd_init);
