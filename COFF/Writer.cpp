//===- Writer.cpp ---------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Symbol.h"
#include "Writer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <map>
#include <utility>

using namespace llvm;
using namespace llvm::object;

namespace lld {
namespace coff {

static uint64_t getSectionAlignment(const coff_section *Sec) {
  return uint64_t(1) << (((Sec->Characteristics & 0x00F00000) >> 20) - 1);
}

static StringRef stripDollar(StringRef Name) {
  return Name.substr(0, Name.find('$'));
}

static uint32_t
mergeCharacteristics(const coff_section *A, const coff_section *B) {
  uint32_t Mask = (llvm::COFF::IMAGE_SCN_MEM_SHARED
		   | llvm::COFF::IMAGE_SCN_MEM_EXECUTE
		   | llvm::COFF::IMAGE_SCN_MEM_READ
		   | llvm::COFF::IMAGE_SCN_CNT_CODE);
  return (A->Characteristics | B->Characteristics) & Mask;
}

void OutputSection::addSection(Section *Sec) {
  uint64_t Align = getSectionAlignment(Sec->Header);
  Header.VirtualSize = RoundUpToAlignment(Header.VirtualSize, Align);
  Header.SizeOfRawData = RoundUpToAlignment(Header.SizeOfRawData, Align);
  Sec->RVA = Header.VirtualSize;
  Sec->FileOffset = Header.SizeOfRawData;
  Header.VirtualSize += Sec->Header->SizeOfRawData;
  Header.SizeOfRawData += Sec->Header->SizeOfRawData;
  Header.Characteristics = mergeCharacteristics(&Header, Sec->Header);
  Sections.push_back(Sec);
}

void OutputSection::sort() {
  auto comp = [](const Section *A, const Section *B) {
    return A->Name < B->Name;
  };
  std::stable_sort(Sections.begin(), Sections.end(), comp);
}

void OutputSection::setRVA(uint64_t RVA) {
  Header.VirtualAddress = RVA;
  for (Section *Sec : Sections)
    Sec->RVA += RVA;
}

void OutputSection::setFileOffset(uint64_t Off) {
  Header.PointerToRawData = Off;
  for (Section *Sec : Sections)
    Sec->FileOffset += Off;
}

void OutputSection::finalize() {
  strncpy(Header.Name, Name.data(), std::min(Name.size(), size_t(8)));
  Header.SizeOfRawData = RoundUpToAlignment(Header.SizeOfRawData, FileAlignment);
}

void Writer::groupSections() {
  StringRef Last = "";
  for (std::unique_ptr<Section> &Sec : Sections) {
    StringRef Name = stripDollar(Sec->Name);
    if (Name != Last)
      OutputSections.emplace_back();
    OutputSection &Out = OutputSections.back();
    Out.Name = Name;
    Out.addSection(Sec.get());
    Last = Name;
  }
  for (OutputSection &Sec : OutputSections)
    Sec.sort();
  EndOfSectionTable = RoundUpToAlignment(
    HeaderSize + sizeof(coff_section) * OutputSections.size(), PageSize);
}

void Writer::removeEmptySections() {
  auto IsEmpty = [](const OutputSection &S) {
    return S.Header.VirtualSize == 0;
  };
  OutputSections.erase(std::remove_if(OutputSections.begin(),
				      OutputSections.end(),
				      IsEmpty),
		       OutputSections.end());
}

void Writer::assignAddresses() {
  uint64_t RVA = 0x1000;
  uint64_t FileOff = EndOfSectionTable;
  uint64_t InitRVA = RVA;
  uint64_t InitFileOff = FileOff;
  for (OutputSection &Out : OutputSections) {
    Out.setRVA(RVA);
    Out.setFileOffset(FileOff);
    RVA += RoundUpToAlignment(Out.Header.VirtualSize, PageSize);
    FileOff += RoundUpToAlignment(Out.Header.SizeOfRawData, FileAlignment);
  }
  SectionTotalSizeDisk = RoundUpToAlignment(FileOff - InitFileOff, FileAlignment);
  SectionTotalSizeMemory = RoundUpToAlignment(RVA - InitRVA, PageSize);
}

void Writer::writeHeader() {
  // Write DOS stub header
  uint8_t *P = Buffer->getBufferStart();
  auto *DOS = reinterpret_cast<dos_header *>(P);
  P += DOSStubSize;
  DOS->Magic[0] = 'M';
  DOS->Magic[1] = 'Z';
  DOS->AddressOfRelocationTable = sizeof(dos_header);
  DOS->AddressOfNewExeHeader = DOSStubSize;

  // Write PE magic
  memcpy(P, llvm::COFF::PEMagic, sizeof(llvm::COFF::PEMagic));
  P += sizeof(llvm::COFF::PEMagic);

  // Write COFF header
  COFF = reinterpret_cast<coff_file_header *>(P);
  P += sizeof(coff_file_header);
  COFF->Machine = llvm::COFF::IMAGE_FILE_MACHINE_AMD64;
  COFF->NumberOfSections = OutputSections.size();
  COFF->Characteristics = (llvm::COFF::IMAGE_FILE_EXECUTABLE_IMAGE
			   | llvm::COFF::IMAGE_FILE_RELOCS_STRIPPED
			   | llvm::COFF::IMAGE_FILE_LARGE_ADDRESS_AWARE);
  COFF->SizeOfOptionalHeader = sizeof(pe32plus_header)
    + sizeof(llvm::object::data_directory) * NumberfOfDataDirectory;

  // Write PE header
  PE = reinterpret_cast<pe32plus_header *>(P);
  P += sizeof(pe32plus_header);
  PE->Magic = llvm::COFF::PE32Header::PE32_PLUS;
  PE->ImageBase = 0x140000000;
  PE->AddressOfEntryPoint = 0x1000;
  PE->SectionAlignment = SectionAlignment;
  PE->FileAlignment = FileAlignment;
  PE->MajorOperatingSystemVersion = 6;
  PE->MajorOperatingSystemVersion = 6;
  PE->MajorSubsystemVersion = 6;
  PE->Subsystem = llvm::COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI;
  PE->SizeOfImage = EndOfSectionTable + SectionTotalSizeMemory;
  PE->SizeOfStackReserve = 1024 * 1024;
  PE->SizeOfStackCommit = 4096;
  PE->SizeOfHeapReserve = 1024 * 1024;
  PE->SizeOfHeapCommit = 4096;
  PE->NumberOfRvaAndSize = NumberfOfDataDirectory;

  // Write data directory
  DataDirectory = reinterpret_cast<data_directory *>(P);
  P += sizeof(data_directory) * NumberfOfDataDirectory;

  // Initialize SectionTable pointer
  SectionTable = reinterpret_cast<coff_section *>(P);
  PE->SizeOfHeaders = RoundUpToAlignment(
    HeaderSize + sizeof(coff_section) * OutputSections.size(), FileAlignment);
}

void Writer::openFile() {
  uint64_t Size = EndOfSectionTable + SectionTotalSizeDisk;
  if (auto EC = FileOutputBuffer::create(
	Path, Size, Buffer, FileOutputBuffer::F_executable)) {
    llvm::errs() << "Failed to open " << Path << ": " << EC.message() << "\n";
  }
}

void Writer::writeSections() {
  int Idx = 0;
  for (OutputSection &Out : OutputSections) {
    Out.finalize();
    SectionTable[Idx++] = Out.Header;
  }
  uint8_t *P = Buffer->getBufferStart();
  for (std::unique_ptr<Section> &Sec : Sections) {
    ArrayRef<uint8_t> C = Sec->getContent();
    memcpy(P + Sec->FileOffset, C.data(), C.size());
  }
}

void Writer::backfillHeaders() {
  for (OutputSection &Out : OutputSections) {
    if (Out.Name == ".text") {
      PE->SizeOfCode = Out.Header.SizeOfRawData;
      PE->BaseOfCode = Out.Header.VirtualAddress;
      return;
    }
  }
}

void Writer::write() {
  groupSections();
  removeEmptySections();
  assignAddresses();
  openFile();
  writeHeader();
  writeSections();
  backfillHeaders();
  Buffer->commit();
}

} // namespace coff
} // namespace lld