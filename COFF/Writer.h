//===- Writer.h -----------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_WRITER_H
#define LLD_COFF_WRITER_H

#include "Resolver.h"
#include "Symbol.h"
#include "llvm/Support/FileOutputBuffer.h"
#include <memory>
#include <vector>

namespace lld {
namespace coff {

const int PageSize = 4096;
const int FileAlignment = 512;
const int SectionAlignment = 4096;
const int DOSStubSize = 64;
const int NumberfOfDataDirectory = 16;
const int HeaderSize = DOSStubSize + sizeof(llvm::COFF::PEMagic)
  + sizeof(llvm::object::coff_file_header)
  + sizeof(llvm::object::pe32plus_header)
  + sizeof(llvm::object::data_directory) * NumberfOfDataDirectory;

class OutputSection {
public:
  OutputSection(StringRef N, uint32_t SI,
		std::vector<InputSection *> &&Sections);
  void setRVA(uint64_t);
  void setFileOffset(uint64_t);

  StringRef Name;
  llvm::object::coff_section Header;
  std::vector<InputSection *> InputSections;
  uint32_t SectionIndex;
};

class Writer {
public:
  explicit Writer(Resolver *R) : Res(R) {}
  void write(StringRef Path);

private:
  void groupSections();
  void assignAddresses();
  void removeEmptySections();
  void openFile(StringRef OutputPath);
  void writeHeader();
  void writeSections();
  void applyRelocations();
  void backfillHeaders();

  void applyOneRelocation(InputSection *Sec, OutputSection *OSec,
			  const coff_relocation *Rel);

  Resolver *Res;
  std::unique_ptr<llvm::FileOutputBuffer> Buffer;
  llvm::object::coff_file_header *COFF;
  llvm::object::pe32plus_header *PE;
  llvm::object::data_directory *DataDirectory;
  llvm::object::coff_section *SectionTable;
  std::vector<OutputSection> OutputSections;

  uint64_t EndOfSectionTable;
  uint64_t SectionTotalSizeDisk;
  uint64_t SectionTotalSizeMemory;
};

} // namespace pecoff
} // namespace lld

#endif
