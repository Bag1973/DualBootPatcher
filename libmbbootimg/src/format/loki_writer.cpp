/*
 * Copyright (C) 2015-2017  Andrew Gunnerson <andrewgunnerson@gmail.com>
 *
 * This file is part of DualBootPatcher
 *
 * DualBootPatcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DualBootPatcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with DualBootPatcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mbbootimg/format/loki_writer_p.h"

#include <algorithm>

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <openssl/sha.h>

#include "mbcommon/endian.h"
#include "mbcommon/file.h"
#include "mbcommon/file_util.h"
#include "mbcommon/string.h"

#include "mbbootimg/entry.h"
#include "mbbootimg/format/align_p.h"
#include "mbbootimg/format/loki_defs.h"
#include "mbbootimg/format/loki_p.h"
#include "mbbootimg/header.h"
#include "mbbootimg/writer.h"
#include "mbbootimg/writer_p.h"

namespace mb
{
namespace bootimg
{
namespace loki
{

constexpr size_t MAX_ABOOT_SIZE = 2 * 1024 * 1024;

LokiFormatWriter::LokiFormatWriter(MbBiWriter *biw)
    : FormatWriter(biw)
    , _hdr()
    , _file_size()
    , _aboot()
    , _sha_ctx()
    , _seg()
{
}

LokiFormatWriter::~LokiFormatWriter()
{
}

int LokiFormatWriter::type()
{
    return FORMAT_LOKI;
}

std::string LokiFormatWriter::name()
{
    return FORMAT_NAME_LOKI;
}

int LokiFormatWriter::init()
{
    if (!SHA1_Init(&_sha_ctx)) {
        writer_set_error(_biw, ERROR_INTERNAL_ERROR,
                         "Failed to initialize SHA_CTX");
        return RET_FAILED;
    }

    return RET_OK;
}

int LokiFormatWriter::get_header(Header &header)
{
    header.set_supported_fields(NEW_SUPPORTED_FIELDS);

    return RET_OK;
}

int LokiFormatWriter::write_header(const Header &header)
{
    int ret;

    // Construct header
    memset(&_hdr, 0, sizeof(_hdr));
    memcpy(_hdr.magic, android::BOOT_MAGIC, android::BOOT_MAGIC_SIZE);

    if (auto address = header.kernel_address()) {
        _hdr.kernel_addr = *address;
    }
    if (auto address = header.ramdisk_address()) {
        _hdr.ramdisk_addr = *address;
    }
    if (auto address = header.secondboot_address()) {
        _hdr.second_addr = *address;
    }
    if (auto address = header.kernel_tags_address()) {
        _hdr.tags_addr = *address;
    }
    if (auto page_size = header.page_size()) {
        switch (*page_size) {
        case 2048:
        case 4096:
        case 8192:
        case 16384:
        case 32768:
        case 65536:
        case 131072:
            _hdr.page_size = *page_size;
            break;
        default:
            writer_set_error(_biw, ERROR_FILE_FORMAT,
                             "Invalid page size: %" PRIu32, *page_size);
            return RET_FAILED;
        }
    } else {
        writer_set_error(_biw, ERROR_FILE_FORMAT,
                         "Page size field is required");
        return RET_FAILED;
    }

    if (auto board_name = header.board_name()) {
        if (board_name->size() >= sizeof(_hdr.name)) {
            writer_set_error(_biw, ERROR_FILE_FORMAT,
                             "Board name too long");
            return RET_FAILED;
        }

        strncpy(reinterpret_cast<char *>(_hdr.name), board_name->c_str(),
                sizeof(_hdr.name) - 1);
        _hdr.name[sizeof(_hdr.name) - 1] = '\0';
    }
    if (auto cmdline = header.kernel_cmdline()) {
        if (cmdline->size() >= sizeof(_hdr.cmdline)) {
            writer_set_error(_biw, ERROR_FILE_FORMAT,
                             "Kernel cmdline too long");
            return RET_FAILED;
        }

        strncpy(reinterpret_cast<char *>(_hdr.cmdline), cmdline->c_str(),
                sizeof(_hdr.cmdline) - 1);
        _hdr.cmdline[sizeof(_hdr.cmdline) - 1] = '\0';
    }

    // TODO: UNUSED
    // TODO: ID

    // Clear existing entries (none should exist unless this function fails and
    // the user reattempts to call it)
    _seg.entries_clear();

    ret = _seg.entries_add(ENTRY_TYPE_KERNEL,
                           0, false, _hdr.page_size, _biw);
    if (ret != RET_OK) return ret;

    ret = _seg.entries_add(ENTRY_TYPE_RAMDISK,
                           0, false, _hdr.page_size, _biw);
    if (ret != RET_OK) return ret;

    ret = _seg.entries_add(ENTRY_TYPE_DEVICE_TREE,
                           0, false, _hdr.page_size, _biw);
    if (ret != RET_OK) return ret;

    ret = _seg.entries_add(ENTRY_TYPE_ABOOT,
                           0, true, 0, _biw);
    if (ret != RET_OK) return ret;

    // Start writing after first page
    if (!_biw->file->seek(_hdr.page_size, SEEK_SET, nullptr)) {
        writer_set_error(_biw, _biw->file->error().value() /* TODO */,
                         "Failed to seek to first page: %s",
                         _biw->file->error_string().c_str());
        return _biw->file->is_fatal() ? RET_FATAL : RET_FAILED;
    }

    return RET_OK;
}

int LokiFormatWriter::get_entry(Entry &entry)
{
    return _seg.get_entry(*_biw->file, entry, _biw);
}

int LokiFormatWriter::write_entry(const Entry &entry)
{
    return _seg.write_entry(*_biw->file, entry, _biw);
}

int LokiFormatWriter::write_data(const void *buf, size_t buf_size,
                                 size_t &bytes_written)
{
    int ret;

    auto const *swentry = _seg.entry();

    if (swentry->type == ENTRY_TYPE_ABOOT) {
        if (buf_size > MAX_ABOOT_SIZE - _aboot.size()) {
            writer_set_error(_biw, ERROR_INTERNAL_ERROR,
                             "aboot image too large");
            return RET_FATAL;
        }

        size_t old_aboot_size = _aboot.size();
        _aboot.resize(old_aboot_size + buf_size);

        memcpy(_aboot.data() + old_aboot_size, buf, buf_size);

        bytes_written = buf_size;
    } else {
        ret = _seg.write_data(*_biw->file, buf, buf_size, bytes_written, _biw);
        if (ret != RET_OK) {
            return ret;
        }

        // We always include the image in the hash. The size is sometimes
        // included and is handled in finish_entry().
        if (!SHA1_Update(&_sha_ctx, buf, buf_size)) {
            writer_set_error(_biw, ERROR_INTERNAL_ERROR,
                             "Failed to update SHA1 hash");
            // This must be fatal as the write already happened and cannot be
            // reattempted
            return RET_FATAL;
        }
    }

    return RET_OK;
}

int LokiFormatWriter::finish_entry()
{
    int ret;

    ret = _seg.finish_entry(*_biw->file, _biw);
    if (ret != RET_OK) {
        return ret;
    }

    auto const *swentry = _seg.entry();

    // Update SHA1 hash
    uint32_t le32_size = mb_htole32(swentry->size);

    // Include fake 0 size for unsupported secondboot image
    if (swentry->type == ENTRY_TYPE_DEVICE_TREE
            && !SHA1_Update(&_sha_ctx, "\x00\x00\x00\x00", 4)) {
        writer_set_error(_biw, ERROR_INTERNAL_ERROR,
                         "Failed to update SHA1 hash");
        return RET_FATAL;
    }

    // Include size for everything except empty DT images
    if (swentry->type != ENTRY_TYPE_ABOOT
            && (swentry->type != ENTRY_TYPE_DEVICE_TREE || swentry->size > 0)
            && !SHA1_Update(&_sha_ctx, &le32_size, sizeof(le32_size))) {
        writer_set_error(_biw, ERROR_INTERNAL_ERROR,
                         "Failed to update SHA1 hash");
        return RET_FATAL;
    }

    switch (swentry->type) {
    case ENTRY_TYPE_KERNEL:
        _hdr.kernel_size = swentry->size;
        break;
    case ENTRY_TYPE_RAMDISK:
        _hdr.ramdisk_size = swentry->size;
        break;
    case ENTRY_TYPE_DEVICE_TREE:
        _hdr.dt_size = swentry->size;
        break;
    }

    return RET_OK;
}

int LokiFormatWriter::close()
{
    int ret;
    size_t n;

    if (_file_size) {
        if (!_biw->file->seek(*_file_size, SEEK_SET, nullptr)) {
            writer_set_error(_biw, _biw->file->error().value() /* TODO */,
                             "Failed to seek to end of file: %s",
                             _biw->file->error_string().c_str());
            return _biw->file->is_fatal() ? RET_FATAL : RET_FAILED;
        }
    } else {
        uint64_t file_size;
        if (!_biw->file->seek(0, SEEK_CUR, &file_size)) {
            writer_set_error(_biw, _biw->file->error().value() /* TODO */,
                             "Failed to get file offset: %s",
                             _biw->file->error_string().c_str());
            return _biw->file->is_fatal() ? RET_FATAL : RET_FAILED;
        }

        _file_size = file_size;
    }

    auto const *swentry = _seg.entry();

    // If successful, finish up the boot image
    if (!swentry) {
        // Truncate to set size
        if (!_biw->file->truncate(*_file_size)) {
            writer_set_error(_biw, _biw->file->error().value() /* TODO */,
                             "Failed to truncate file: %s",
                             _biw->file->error_string().c_str());
            return _biw->file->is_fatal() ? RET_FATAL : RET_FAILED;
        }

        // Set ID
        unsigned char digest[SHA_DIGEST_LENGTH];
        if (!SHA1_Final(digest, &_sha_ctx)) {
            writer_set_error(_biw, ERROR_INTERNAL_ERROR,
                             "Failed to update SHA1 hash");
            return RET_FATAL;
        }
        memcpy(_hdr.id, digest, SHA_DIGEST_LENGTH);

        // Convert fields back to little-endian
        android::AndroidHeader hdr = _hdr;
        android_fix_header_byte_order(hdr);

        // Seek back to beginning to write header
        if (!_biw->file->seek(0, SEEK_SET, nullptr)) {
            writer_set_error(_biw, _biw->file->error().value() /* TODO */,
                             "Failed to seek to beginning: %s",
                             _biw->file->error_string().c_str());
            return _biw->file->is_fatal() ? RET_FATAL : RET_FAILED;
        }

        // Write header
        if (!file_write_fully(*_biw->file, &hdr, sizeof(hdr), n)
                || n != sizeof(hdr)) {
            writer_set_error(_biw, _biw->file->error().value() /* TODO */,
                             "Failed to write header: %s",
                             _biw->file->error_string().c_str());
            return _biw->file->is_fatal() ? RET_FATAL : RET_FAILED;
        }

        // Patch with Loki
        ret = _loki_patch_file(_biw, *_biw->file, _aboot.data(), _aboot.size());
        if (ret != RET_OK) {
            return ret;
        }
    }

    return RET_OK;
}

}

/*!
 * \brief Set Loki boot image output format
 *
 * \param biw MbBiWriter
 *
 * \return
 *   * #RET_OK if the format is successfully set
 *   * \<= #RET_WARN if an error occurs
 */
int writer_set_format_loki(MbBiWriter *biw)
{
    using namespace loki;

    std::unique_ptr<FormatWriter> format{new LokiFormatWriter(biw)};
    return _writer_register_format(biw, std::move(format));
}

}
}
