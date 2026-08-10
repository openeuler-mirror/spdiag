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
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <elf.h>

namespace SpDiag {

struct SourceLocation {
    std::string filePath;
    uint32_t line = 0;
};

class DwarfDebugLine {
public:
    bool Load(const std::string& elfPath) {
        std::ifstream file(elfPath, std::ios::binary);
        if (!file.is_open()) return false;

        if (!ParseSections(file)) return false;
        if (!ParseDebugInfoCUs(file)) return false;
        if (!ParseLineTables(file)) return false;
        return true;
    }

    SourceLocation Lookup(uint64_t vaddr) const {
        SourceLocation loc;
        auto it = FindLineEntry(vaddr);
        if (it != lineEntries_.end()) {
            loc.line = it->line;
            if (it->fileIndex < fileNames_.size()) {
                loc.filePath = fileNames_[it->fileIndex];
            }
        }
        return loc;
    }

private:
    struct CuHeader {
        uint64_t offset = 0;
        uint64_t length = 0;
        uint64_t stmtList = 0;
    };

    struct LineEntry {
        uint64_t address = 0;
        uint32_t fileIndex = 0;
        uint32_t line = 0;
    };

    struct SectionInfo {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    static const uint64_t DW_AT_stmt_list = 0x10;
    static const uint64_t DW_TAG_compile_unit = 0x11;
    static const uint64_t DW_TAG_subprogram = 0x2E;
    static const uint64_t DW_TAG_module = 0x2D;
    static const uint64_t DW_TAG_namespace = 0x39;

    static const uint64_t DW_FORM_addr = 0x01;
    static const uint64_t DW_FORM_data1 = 0x0b;
    static const uint64_t DW_FORM_data2 = 0x05;
    static const uint64_t DW_FORM_data4 = 0x06;
    static const uint64_t DW_FORM_data8 = 0x07;
    static const uint64_t DW_FORM_string = 0x08;
    static const uint64_t DW_FORM_flag = 0x0c;
    static const uint64_t DW_FORM_sdata = 0x0d;
    static const uint64_t DW_FORM_udata = 0x0f;
    static const uint64_t DW_FORM_ref1 = 0x11;
    static const uint64_t DW_FORM_ref2 = 0x12;
    static const uint64_t DW_FORM_ref4 = 0x13;
    static const uint64_t DW_FORM_ref8 = 0x14;
    static const uint64_t DW_FORM_ref_udata = 0x15;
    static const uint64_t DW_FORM_sec_offset = 0x17;
    static const uint64_t DW_FORM_strp = 0x0e;
    static const uint64_t DW_FORM_ref_addr = 0x10;
    static const uint64_t DW_FORM_flag_present = 0x19;
    static const uint64_t DW_FORM_ref_sig8 = 0x20;

    bool ParseSections(std::ifstream& file) {
        Elf64_Ehdr ehdr;
        if (!file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr))) return false;
        if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) return false;
        if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) return false;
        if (ehdr.e_shnum == 0 || ehdr.e_shoff == 0) return false;

        std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
        file.seekg(static_cast<std::streamoff>(ehdr.e_shoff));
        if (!file.read(reinterpret_cast<char*>(shdrs.data()),
                       static_cast<std::streamsize>(ehdr.e_shnum * sizeof(Elf64_Shdr)))) {
            return false;
        }

        uint64_t shstrtabOff = shdrs[ehdr.e_shstrndx].sh_offset;

        for (const auto& sh : shdrs) {
            if (sh.sh_name == 0) continue;
            std::string name = ReadStringAt(file, shstrtabOff + sh.sh_name);
            if (name == ".debug_line") {
                sections_[".debug_line"] = {sh.sh_offset, sh.sh_size};
            } else if (name == ".debug_info") {
                sections_[".debug_info"] = {sh.sh_offset, sh.sh_size};
            } else if (name == ".debug_abbrev") {
                sections_[".debug_abbrev"] = {sh.sh_offset, sh.sh_size};
            }
        }

        return sections_.count(".debug_line") > 0 && sections_.count(".debug_info") > 0
               && sections_.count(".debug_abbrev") > 0;
    }

    bool ParseDebugInfoCUs(std::ifstream& file) {
        auto it = sections_.find(".debug_info");
        uint64_t offset = it->second.offset;
        uint64_t end = offset + it->second.size;

        while (offset + 11 <= end) {
            file.seekg(static_cast<std::streamoff>(offset));

            uint32_t length32 = 0;
            if (!file.read(reinterpret_cast<char*>(&length32), 4)) break;
            if (length32 == 0) break;

            uint64_t cuLength = 0;
            uint8_t format = 32;
            if (length32 == 0xFFFFFFFF) {
                uint64_t length64 = 0;
                if (!file.read(reinterpret_cast<char*>(&length64), 8)) break;
                cuLength = length64;
                format = 64;
            } else {
                cuLength = length32;
            }

            if (cuLength < (format == 64 ? 23U : 11U)) {
                offset += (format == 64 ? 12 : 4) + cuLength;
                continue;
            }

            uint16_t version = 0;
            if (!file.read(reinterpret_cast<char*>(&version), 2)) break;

            uint64_t abbrevOffset = 0;
            if (format == 64) {
                if (!file.read(reinterpret_cast<char*>(&abbrevOffset), 8)) break;
            } else {
                uint32_t abbrevOff32 = 0;
                if (!file.read(reinterpret_cast<char*>(&abbrevOff32), 4)) break;
                abbrevOffset = abbrevOff32;
            }

            uint8_t addressSize = 0;
            if (!file.read(reinterpret_cast<char*>(&addressSize), 1)) break;

            uint64_t cuBodyEnd = offset + (format == 64 ? 12 : 4) + cuLength;

            uint64_t stmtList = ExtractStmtList(file, abbrevOffset);

            if (stmtList > 0) {
                CuHeader cu;
                cu.offset = offset;
                cu.length = cuLength;
                cu.stmtList = stmtList;
                cuHeaders_.push_back(cu);
            }

            file.seekg(static_cast<std::streamoff>(cuBodyEnd));
            offset = cuBodyEnd;
        }
        return true;
    }

    uint64_t ExtractStmtList(std::ifstream& file, uint64_t abbrevOffset) {
        auto abbrevIt = sections_.find(".debug_abbrev");
        if (abbrevIt == sections_.end()) return 0;

        uint64_t code = ReadUleb128(file);
        if (code == 0) return 0;

        std::vector<std::pair<uint64_t, uint64_t>> decl = ReadAbbrevDecl(file, abbrevOffset, code);

        uint64_t result = 0;
        for (const auto& [attrName, attrForm] : decl) {
            uint64_t val = SkipForm(file, attrForm);
            if (attrName == DW_AT_stmt_list) {
                result = val;
            }
        }
        return result;
    }

    std::vector<std::pair<uint64_t, uint64_t>> ReadAbbrevDecl(
        std::ifstream& file, uint64_t abbrevOffset, uint64_t targetCode) {
        std::vector<std::pair<uint64_t, uint64_t>> decl;

        auto abbrevIt = sections_.find(".debug_abbrev");
        if (abbrevIt == sections_.end()) return decl;

        auto savedPos = file.tellg();
        uint64_t abbrevBase = abbrevIt->second.offset;
        uint64_t abbrevEnd = abbrevBase + abbrevIt->second.size;
        uint64_t searchOff = abbrevBase + abbrevOffset;

        file.seekg(static_cast<std::streamoff>(searchOff));

        while (static_cast<uint64_t>(file.tellg()) < abbrevEnd) {
            uint64_t code = ReadUleb128(file);
            if (code == 0) break;
            uint64_t tag = ReadUleb128(file);
            (void)tag;
            uint8_t hasChildren = 0;
            file.read(reinterpret_cast<char*>(&hasChildren), 1);

            if (code == targetCode) {
                while (true) {
                    uint64_t attrName = ReadUleb128(file);
                    uint64_t attrForm = ReadUleb128(file);
                    if (attrName == 0 && attrForm == 0) break;
                    decl.push_back({attrName, attrForm});
                }
                file.seekg(savedPos);
                return decl;
            }

            while (true) {
                uint64_t attrName = ReadUleb128(file);
                uint64_t attrForm = ReadUleb128(file);
                if (attrName == 0 && attrForm == 0) break;
            }
        }

        file.seekg(savedPos);
        return decl;
    }

    uint64_t SkipForm(std::ifstream& file, uint64_t form) {
        if (form == DW_FORM_addr) {
            file.seekg(8, std::ios::cur);
            return 0;
        }
        if (form == DW_FORM_data1 || form == DW_FORM_ref1 || form == DW_FORM_flag) {
            file.seekg(1, std::ios::cur);
            return 0;
        }
        if (form == DW_FORM_data2 || form == DW_FORM_ref2) {
            file.seekg(2, std::ios::cur);
            return 0;
        }
        if (form == DW_FORM_data4 || form == DW_FORM_ref4 || form == DW_FORM_sec_offset
            || form == DW_FORM_strp) {
            uint32_t val = 0;
            file.read(reinterpret_cast<char*>(&val), 4);
            return val;
        }
        if (form == DW_FORM_data8 || form == DW_FORM_ref8 || form == DW_FORM_ref_sig8) {
            file.seekg(8, std::ios::cur);
            return 0;
        }
        if (form == DW_FORM_sdata) {
            ReadSleb128(file);
            return 0;
        }
        if (form == DW_FORM_udata || form == DW_FORM_ref_udata) return ReadUleb128(file);
        if (form == DW_FORM_string) {
            while (file.get() != '\0' && file.good()) {}
            return 0;
        }
        if (form == DW_FORM_ref_addr) {
            file.seekg(4, std::ios::cur);
            return 0;
        }
        if (form == DW_FORM_flag_present) return 0;
        return 0;
    }

    bool ParseLineTables(std::ifstream& file) {
        auto lineIt = sections_.find(".debug_line");
        if (lineIt == sections_.end()) return false;

        for (const auto& cu : cuHeaders_) {
            if (cu.stmtList == 0) continue;
            uint64_t lineOffset = lineIt->second.offset + cu.stmtList;
            ParseOneLineTable(file, lineOffset, lineIt->second.offset + lineIt->second.size);
        }
        return !lineEntries_.empty();
    }

    void ParseOneLineTable(std::ifstream& file, uint64_t offset, uint64_t sectionEnd) {
        if (offset + 4 > sectionEnd) return;

        file.seekg(static_cast<std::streamoff>(offset));

        uint32_t unitLength = 0;
        if (!file.read(reinterpret_cast<char*>(&unitLength), 4)) return;
        if (unitLength == 0xFFFFFFFF) return;
        if (unitLength < 10) return;

        uint64_t unitEnd = offset + 4 + unitLength;
        if (unitEnd > sectionEnd) return;

        uint16_t version = 0;
        if (!file.read(reinterpret_cast<char*>(&version), 2)) return;
        if (version < 2 || version > 4) return;

        uint32_t headerLength = 0;
        if (!file.read(reinterpret_cast<char*>(&headerLength), 4)) return;
        (void)headerLength;

        uint8_t minInstrLen = 0;
        if (!file.read(reinterpret_cast<char*>(&minInstrLen), 1)) return;

        if (version >= 4) {
            file.seekg(1, std::ios::cur);
        }

        uint8_t defaultIsStmt = 0;
        if (!file.read(reinterpret_cast<char*>(&defaultIsStmt), 1)) return;

        int8_t lineBase = 0;
        if (!file.read(reinterpret_cast<char*>(&lineBase), 1)) return;

        uint8_t lineRange = 0;
        if (!file.read(reinterpret_cast<char*>(&lineRange), 1)) return;

        uint8_t opcodeBase = 0;
        if (!file.read(reinterpret_cast<char*>(&opcodeBase), 1)) return;

        if (opcodeBase < 2) opcodeBase = 2;

        for (int i = 1; i < opcodeBase; ++i) {
            ReadUleb128(file);
        }

        std::vector<std::string> dirs;
        dirs.push_back("");
        while (true) {
            std::string dir = ReadCString(file);
            if (dir.empty()) break;
            dirs.push_back(dir);
        }

        std::vector<std::string> localFileNames;
        localFileNames.push_back("");
        while (true) {
            std::string fname = ReadCString(file);
            if (fname.empty()) break;
            uint64_t dirIdx = ReadUleb128(file);
            ReadUleb128(file);
            ReadUleb128(file);

            if (dirIdx > 0 && dirIdx <= dirs.size()) {
                localFileNames.push_back(dirs[dirIdx - 1] + fname);
            } else {
                localFileNames.push_back(fname);
            }
        }

        RunLineProgram(file, unitEnd, localFileNames, minInstrLen, lineBase,
                       lineRange, opcodeBase);

        if (!fileNames_.empty()) {
            for (size_t i = fileNames_.size(); i < localFileNames.size(); ++i) {
                fileNames_.push_back(localFileNames[i]);
            }
        } else {
            fileNames_ = std::move(localFileNames);
        }
    }

    void RunLineProgram(std::ifstream& file, uint64_t unitEnd,
                        const std::vector<std::string>& localFileNames,
                        uint8_t minInstrLen, int8_t lineBase, uint8_t lineRange,
                        uint8_t opcodeBase) {
        uint64_t address = 0;
        uint64_t fileIdx = 1;
        uint64_t line = 1;
        bool endSequence = false;

        while (static_cast<uint64_t>(file.tellg()) < unitEnd) {
            uint8_t opcode = 0;
            if (!file.read(reinterpret_cast<char*>(&opcode), 1)) break;

            if (opcode == 0) {
                uint64_t extLen = ReadUleb128(file);
                uint64_t extEnd = static_cast<uint64_t>(file.tellg()) + extLen;

                uint8_t extOpcode = 0;
                if (!file.read(reinterpret_cast<char*>(&extOpcode), 1)) break;

                switch (extOpcode) {
                    case 1:
                        endSequence = true;
                        break;
                    case 2:
                        file.read(reinterpret_cast<char*>(&address), 8);
                        break;
                    case 3:
                        fileIdx = ReadUleb128(file);
                        break;
                    case 4:
                        ReadUleb128(file);
                        break;
                    default:
                        break;
                }

                file.seekg(static_cast<std::streamoff>(extEnd));
            } else if (opcode >= opcodeBase) {
                uint64_t adjOpcode = opcode - opcodeBase;
                uint64_t addrIncr = adjOpcode / lineRange;
                uint64_t lineIncr = static_cast<int64_t>(lineBase) +
                                    static_cast<int64_t>(adjOpcode % lineRange);
                address += addrIncr * minInstrLen;
                line += static_cast<uint64_t>(static_cast<int64_t>(lineIncr));

                if (address > 0 && fileIdx > 0 && fileIdx <= localFileNames.size()) {
                    LineEntry entry;
                    entry.address = address;
                    entry.fileIndex = static_cast<uint32_t>(fileIdx);
                    entry.line = static_cast<uint32_t>(line);
                    lineEntries_.push_back(entry);
                }
            } else {
                switch (opcode) {
                    case 1:
                        address += ReadUleb128(file) * minInstrLen;
                        break;
                    case 2:
                        line += static_cast<uint64_t>(static_cast<int64_t>(ReadSleb128(file)));
                        break;
                    case 3:
                        fileIdx = ReadUleb128(file);
                        break;
                    case 4:
                        ReadUleb128(file);
                        break;
                    default:
                        for (int i = 1; i < opcode; ++i) {
                            ReadUleb128(file);
                        }
                        break;
                }
            }

            if (endSequence) {
                address = 0;
                fileIdx = 1;
                line = 1;
                endSequence = false;
            }
        }
    }

    std::vector<LineEntry>::const_iterator FindLineEntry(uint64_t vaddr) const {
        if (lineEntries_.empty()) return lineEntries_.end();
        auto it = std::upper_bound(lineEntries_.begin(), lineEntries_.end(), vaddr,
            [](uint64_t addr, const LineEntry& e) { return addr < e.address; });
        if (it == lineEntries_.begin()) return lineEntries_.end();
        --it;
        return it;
    }

    static std::string ReadStringAt(std::ifstream& file, uint64_t offset) {
        auto pos = file.tellg();
        file.seekg(static_cast<std::streamoff>(offset));
        std::string result = ReadCString(file);
        file.seekg(pos);
        return result;
    }

    static std::string ReadCString(std::ifstream& file) {
        std::string s;
        char c;
        while (file.get(c)) {
            if (c == '\0') break;
            s.push_back(c);
        }
        return s;
    }

    static uint64_t ReadUleb128(std::ifstream& file) {
        uint64_t result = 0;
        int shift = 0;
        uint8_t byte = 0;
        do {
            if (!file.read(reinterpret_cast<char*>(&byte), 1)) break;
            result |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
        } while (byte & 0x80);
        return result;
    }

    static int64_t ReadSleb128(std::ifstream& file) {
        int64_t result = 0;
        int shift = 0;
        uint8_t byte = 0;
        do {
            if (!file.read(reinterpret_cast<char*>(&byte), 1)) break;
            result |= static_cast<int64_t>(byte & 0x7F) << shift;
            shift += 7;
        } while (byte & 0x80);
        if (byte & 0x40) {
            result |= -(static_cast<int64_t>(1) << shift);
        }
        return result;
    }

    std::unordered_map<std::string, SectionInfo> sections_;
    std::vector<CuHeader> cuHeaders_;
    std::vector<std::string> fileNames_;
    std::vector<LineEntry> lineEntries_;
};

}
