/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#pragma once
#include <cerrno>
#include <string>

#include "platform.h"
#include "spdiag/shm_layout.h"

namespace SpDiag {

enum class ShmProbeStatus {
    ABSENT,
    COMPATIBLE,
    INCOMPATIBLE,
    MALFORMED,
};

struct ShmProbeResult {
    ShmProbeStatus status = ShmProbeStatus::ABSENT;
    bool valid = false;
    size_t totalSize = 0;
    std::string detail;

    bool IsCompatible() const {
        return status == ShmProbeStatus::COMPATIBLE;
    }
};

inline ShmProbeResult ValidateShmHeader(const ShmHeader& hdr,
                                        size_t objectSize) {
    ShmProbeResult result;
    result.totalSize = hdr.shmTotalSize;

    if (hdr.magic != shmMagic || hdr.version != 3) {
        result.status = ShmProbeStatus::MALFORMED;
        result.detail = "magic or layout version mismatch";
        return result;
    }
    if (hdr.reserved[0] != shmLayoutAbiRevision ||
        hdr.reserved[1] != CurrentShmFeatureMask()) {
        result.status = ShmProbeStatus::INCOMPATIBLE;
        result.detail = "layout ABI or feature mask mismatch";
        return result;
    }
    if (hdr.registeredPrograms > hdr.maxPrograms) {
        result.status = ShmProbeStatus::MALFORMED;
        result.detail = "registered program count exceeds capacity";
        return result;
    }

    ShmLayoutDescriptor expected;
    if (!ComputeShmLayout(hdr.maxSlots, hdr.maxPrograms, hdr.numCores,
                          expected)) {
        result.status = ShmProbeStatus::MALFORMED;
        result.detail = "layout dimensions are invalid or overflow";
        return result;
    }

    const bool offsetsMatch =
        hdr.registryOffset == expected.registryOffset &&
        hdr.keyNamesOffset == expected.keyNamesOffset &&
        hdr.keyModuleNamesOffset == expected.keyModuleNamesOffset &&
        hdr.activeBitmapOffset == expected.activeBitmapOffset &&
        hdr.shardsOffset == expected.shardsOffset &&
        hdr.globalAccumOffset == expected.globalAccumOffset &&
        hdr.globalAccumCount == hdr.maxSlots;
    if (!offsetsMatch || hdr.shmTotalSize != expected.totalSize ||
        objectSize != expected.totalSize) {
        result.status = ShmProbeStatus::INCOMPATIBLE;
        result.detail = "layout offsets or object size mismatch";
        return result;
    }

    result.status = ShmProbeStatus::COMPATIBLE;
    result.valid = true;
    result.detail = "compatible";
    return result;
}

inline ShmProbeResult ValidateMappedShmHeader(const ShmHeader& hdr,
                                              const ShmHandle& handle) {
    if (handle.mappedSize != handle.objectSize) {
        ShmProbeResult result;
        result.status = ShmProbeStatus::INCOMPATIBLE;
        result.totalSize = hdr.shmTotalSize;
        result.detail = "mapping does not cover the complete SHM object";
        return result;
    }
    return ValidateShmHeader(hdr, handle.mappedSize);
}

inline ShmProbeResult ProbeShmHeader(const std::string& name) {
    ShmProbeResult result;
    ShmHandle probeHandle = Platform::ShmOpenReadOnly(name, sizeof(ShmHeader));
    if (probeHandle.baseAddr == nullptr) {
        if (probeHandle.errorCode != 0 && probeHandle.errorCode != ENOENT) {
            result.status = ShmProbeStatus::MALFORMED;
            result.detail = "cannot open a complete SHM header";
        }
        return result;
    }
    const ShmHeader* hdr = reinterpret_cast<const ShmHeader*>(probeHandle.baseAddr);
    result = ValidateShmHeader(*hdr, probeHandle.objectSize);
    Platform::ShmClose(probeHandle);
    return result;
}

}
