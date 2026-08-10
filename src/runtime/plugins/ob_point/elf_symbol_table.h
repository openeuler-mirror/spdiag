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


#include <cstdint>
#include <elf.h>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace SpDiag {

struct ElfSym {
    uint64_t addr;
    uint64_t size;
    std::string name;
};

struct ElfSymbolTable {
    std::vector<ElfSym> symbols;
    uint64_t vaddrBias = 0;
    bool loaded = false;
};

class ElfSymbolTableLoader {
public:
    static const ElfSymbolTable* Load(const std::string& filePath);
    static std::string FindSymbol(const ElfSymbolTable* tab, uint64_t vaddr);

private:
    static bool LoadOneSymtab(std::ifstream& file, const std::vector<Elf64_Shdr>& shdrs,
                              uint32_t sectionType, std::vector<ElfSym>& out);
    static std::unordered_map<std::string, std::unique_ptr<ElfSymbolTable>> cache_;
};

}
