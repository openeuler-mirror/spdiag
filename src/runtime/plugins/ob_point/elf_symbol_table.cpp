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


#include "elf_symbol_table.h"

#include <algorithm>
#include <cstring>

namespace SpDiag {

std::unordered_map<std::string, std::unique_ptr<ElfSymbolTable>> ElfSymbolTableLoader::cache_;

const ElfSymbolTable* ElfSymbolTableLoader::Load(const std::string& filePath) {
    auto it = cache_.find(filePath);
    if (it != cache_.end()) {
        return it->second.get();
    }

    auto tab = std::make_unique<ElfSymbolTable>();
    tab->loaded = true;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        cache_[filePath] = std::move(tab);
        return cache_[filePath].get();
    }

    Elf64_Ehdr ehdr;
    if (!file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr))) {
        cache_[filePath] = std::move(tab);
        return cache_[filePath].get();
    }

    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        cache_[filePath] = std::move(tab);
        return cache_[filePath].get();
    }

    tab->vaddrBias = UINT64_MAX;
    if (ehdr.e_phnum > 0 && ehdr.e_phoff > 0) {
        std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
        file.seekg(static_cast<std::streamoff>(ehdr.e_phoff));
        if (file.read(reinterpret_cast<char*>(phdrs.data()),
                      static_cast<std::streamsize>(ehdr.e_phnum * sizeof(Elf64_Phdr)))) {
            for (const auto& ph : phdrs) {
                if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X)) {
                    uint64_t bias = ph.p_vaddr - ph.p_offset;
                    if (bias < tab->vaddrBias) {
                        tab->vaddrBias = bias;
                    }
                }
            }
        }
    }
    if (tab->vaddrBias == UINT64_MAX) {
        tab->vaddrBias = 0;
    }

    if (ehdr.e_shnum == 0 || ehdr.e_shoff == 0) {
        cache_[filePath] = std::move(tab);
        return cache_[filePath].get();
    }

    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    file.seekg(static_cast<std::streamoff>(ehdr.e_shoff));
    if (!file.read(reinterpret_cast<char*>(shdrs.data()),
                   static_cast<std::streamsize>(ehdr.e_shnum * sizeof(Elf64_Shdr)))) {
        cache_[filePath] = std::move(tab);
        return cache_[filePath].get();
    }

    LoadOneSymtab(file, shdrs, SHT_SYMTAB, tab->symbols);
    LoadOneSymtab(file, shdrs, SHT_DYNSYM, tab->symbols);

    std::sort(tab->symbols.begin(), tab->symbols.end(),
        [](const ElfSym& a, const ElfSym& b) { return a.addr < b.addr; });

    auto newEnd = std::unique(tab->symbols.begin(), tab->symbols.end(),
        [](const ElfSym& a, const ElfSym& b) { return a.addr == b.addr; });
    tab->symbols.erase(newEnd, tab->symbols.end());

    cache_[filePath] = std::move(tab);
    return cache_[filePath].get();
}

std::string ElfSymbolTableLoader::FindSymbol(const ElfSymbolTable* tab, uint64_t vaddr) {
    if (!tab || tab->symbols.empty()) return "";

    auto it = std::upper_bound(tab->symbols.begin(), tab->symbols.end(), vaddr,
        [](uint64_t v, const ElfSym& s) { return v < s.addr; });

    if (it == tab->symbols.begin()) return "";
    --it;

    if (it->size > 0) {
        if (vaddr >= it->addr + it->size) return "";
        return it->name;
    }

    return it->name;
}

bool ElfSymbolTableLoader::LoadOneSymtab(std::ifstream& file, const std::vector<Elf64_Shdr>& shdrs,
                                         uint32_t sectionType, std::vector<ElfSym>& out) {
    const Elf64_Shdr* target = nullptr;
    for (const auto& sh : shdrs) {
        if (sh.sh_type == sectionType) {
            target = &sh;
            break;
        }
    }
    if (!target || target->sh_link >= shdrs.size()) return false;
    if (target->sh_entsize == 0) return false;

    const Elf64_Shdr& strtabSh = shdrs[target->sh_link];
    std::string strtab;
    strtab.resize(strtabSh.sh_size);
    file.seekg(static_cast<std::streamoff>(strtabSh.sh_offset));
    if (!file.read(&strtab[0], static_cast<std::streamsize>(strtabSh.sh_size))) return false;

    size_t symCount = target->sh_size / target->sh_entsize;
    std::vector<Elf64_Sym> rawSyms(symCount);
    file.seekg(static_cast<std::streamoff>(target->sh_offset));
    if (!file.read(reinterpret_cast<char*>(rawSyms.data()),
                   static_cast<std::streamsize>(symCount * sizeof(Elf64_Sym)))) return false;

    for (const auto& sym : rawSyms) {
        uint8_t type = ELF64_ST_TYPE(sym.st_info);
        if (type != STT_FUNC && type != STT_NOTYPE) continue;
        if (sym.st_value == 0) continue;
        if (type == STT_NOTYPE && sym.st_size == 0) continue;

        ElfSym es;
        es.addr = sym.st_value;
        es.size = sym.st_size;
        if (sym.st_name < strtab.size()) {
            es.name = &strtab[sym.st_name];
        }
        if (es.name.empty()) continue;
        if (es.name[0] == '$') continue;
        out.push_back(std::move(es));
    }
    return true;
}

}
