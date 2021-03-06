#include "java.h"
#include "image.h"
#include "io.h"
#include "const.h"
#include "util.h"
#include <zlib.h>

extern "C" JNIEXPORT jlong JNICALL
Java_com_kdrag0n_tipatch_jni_Image_init(JNIEnv *env, jobject, jobject fis) {
    try {
        Image *image = new Image();

        // header
        {
            auto hdr_bytes = read_bytes(env, fis, sizeof(boot_img_hdr));
            auto hdr = ((boot_img_hdr *) hdr_bytes.bytes());

            if (memcmp(hdr->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0)
                throw img_exception("Unable to find header. Are you sure this is a TWRP image?");

            image->hdr = *hdr;
        }

        read_padding(env, fis, sizeof(boot_img_hdr), image->hdr.page_size);

        // kernel
        {
            auto kernel = read_bytes(env, fis, image->hdr.kernel_size);
            image->kernel = byte_array::ref(kernel.copy_bytes(), kernel.len);
        }

        read_padding(env, fis, image->hdr.kernel_size, image->hdr.page_size);

        // ramdisk
        {
            auto ramdisk = read_bytes(env, fis, image->hdr.ramdisk_size);
            image->ramdisk = byte_array::ref(ramdisk.copy_bytes(), ramdisk.len);
        }

        read_padding(env, fis, image->hdr.ramdisk_size, image->hdr.page_size);

        // second-stage loader
        if (image->hdr.second_size > 0) {
            auto second = read_bytes(env, fis, image->hdr.second_size);
            image->second = byte_array::ref(second.copy_bytes(), second.len);
        } else {
            image->second = byte_array::ref(nullptr, 0);
        }

        read_padding(env, fis, image->hdr.second_size, image->hdr.page_size);

        // device tree
        if (image->hdr.dt_size > 0) {
            auto dt = read_bytes(env, fis, image->hdr.dt_size);
            image->device_tree = byte_array::ref(dt.copy_bytes(), dt.len);
        } else {
            image->device_tree = byte_array::ref(nullptr, 0);
        }

        read_padding(env, fis, image->hdr.dt_size, image->hdr.page_size);

        return (jlong) image;
    } catch (...) {
        rethrow(env);
        return (jlong) 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_kdrag0n_tipatch_jni_Image_free(JNIEnv, jobject, jlong handle) {
    Image *ptr = (Image*) handle;
    delete ptr;
}

extern "C" JNIEXPORT jbyte JNICALL
Java_com_kdrag0n_tipatch_jni_Image_nvDetectCompressor(JNIEnv, jobject, jlong handle) {
    Image *image = (Image*) handle;
    auto data = image->ramdisk->data;
    int b1 = data[0];
    int b2 = data[1];
    int b3 = data[2];
    int b4 = data[3];
    int b5 = data[4];

    if (b1 == 0x42 && b2 == 0x5a) {
        return comp::bzip2;
    } else if (b1 == 0x1f && (b2 == 0x8b || b2 == 0x9e)) {
        return comp::gzip;
    } else if (b1 == 0x04 && b2 == 0x22) {
        return comp::lz4;
    } else if (b1 == 0x89 && b2 == 0x4c) {
        return comp::lzo;
    } else if (b1 == 0x5d && b2 == 0x00) {
        return comp::lzma;
    } else if (b1 == 0xfd && b2 == 0x37) {
        return comp::xz;
    } else if (b1 == 0x07 && b2 == 0x07 && b3 == 0x07) { // legacy cpio: old kernels?
        return comp::none;
    } else if (b1 == 0x30 && b2 == 0x37 && b3 == 0x30 && b4 == 0x37 && b5 == 0x30) {
        // modern cpio: string "07070[X]" being 070701, 070702, or 070707
        return comp::none;
    } else {
        return comp::unknown;
    }
}

unsigned long Image::hash() {
    auto sum = adler32(0L, kernel->data, (uInt) kernel->len);
    sum = adler32(sum, ramdisk->data, (uInt) ramdisk->len);

    if (second->len > 0) {
        sum = adler32(sum, second->data, (uInt) second->len);
    }

    if (device_tree->len > 0) {
        sum = adler32(sum, device_tree->data, (uInt) device_tree->len);
    }

    return sum;
}