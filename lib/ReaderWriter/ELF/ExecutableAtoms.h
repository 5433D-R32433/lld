//===- lib/ReaderWriter/ELF/ExecutableAtoms.h ----------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_ELF_EXECUTABLE_ATOM_H_
#define LLD_READER_WRITER_ELF_EXECUTABLE_ATOM_H_


#include "lld/Core/DefinedAtom.h"
#include "lld/Core/UndefinedAtom.h"
#include "lld/Core/File.h"
#include "lld/Core/Reference.h"
#include "lld/ReaderWriter/WriterELF.h"
#include "AtomsELF.h"

namespace lld {
namespace elf {

/// \brief All atoms are owned by a File. To add linker specific atoms
/// the atoms need to be inserted to a file called (CRuntimeFile) which 
/// are basically additional symbols required by libc and other runtime 
/// libraries part of executing a program. This class provides support
/// for adding absolute symbols and undefined symbols
template<llvm::support::endianness target_endianness,
         std::size_t max_align,
         bool is64Bits>
class CRuntimeFile : public File {
public:
  typedef llvm::object::Elf_Sym_Impl<target_endianness, max_align, is64Bits> Elf_Sym;
  CRuntimeFile(const WriterOptionsELF &options) 
    : File("C runtime") 
  { }
  
  /// \brief add a global absolute atom
  void addAbsoluteAtom(const StringRef symbolName) {
    Elf_Sym *symbol = new(_allocator.Allocate<Elf_Sym>()) Elf_Sym;
    symbol->st_name = 0;
    symbol->st_value = 0;
    symbol->st_shndx = llvm::ELF::SHN_ABS;
    symbol->setBindingAndType(llvm::ELF::STB_GLOBAL, 
                              llvm::ELF::STT_OBJECT);
    symbol->st_other = llvm::ELF::STV_DEFAULT;
    symbol->st_size = 0;
    auto *newAtom = new (_allocator.Allocate<
      ELFAbsoluteAtom<target_endianness, max_align, is64Bits> > ())
      ELFAbsoluteAtom<target_endianness, max_align, is64Bits>(
        *this, symbolName, symbol, -1);
    _absoluteAtoms._atoms.push_back(newAtom);
  }

  /// \brief add an undefined atom 
  void addUndefinedAtom(const StringRef symbolName) {
    Elf_Sym *symbol = new(_allocator.Allocate<Elf_Sym>()) Elf_Sym;
    symbol->st_name = 0;
    symbol->st_value = 0;
    symbol->st_shndx = llvm::ELF::SHN_UNDEF;
    symbol->st_other = llvm::ELF::STV_DEFAULT;
    symbol->st_size = 0;
    auto *newAtom = new (_allocator.Allocate<
      ELFUndefinedAtom<target_endianness, max_align, is64Bits> > ())
      ELFUndefinedAtom<target_endianness, max_align, is64Bits>(
        *this, symbolName, symbol);
    _undefinedAtoms._atoms.push_back(newAtom);
  }

  const atom_collection<DefinedAtom> &defined() const {
    return _definedAtoms;
  }

  const atom_collection<UndefinedAtom> &undefined() const {
    return _undefinedAtoms;
  }

  const atom_collection<SharedLibraryAtom> &sharedLibrary() const {
    return _sharedLibraryAtoms;
  }

  const atom_collection<AbsoluteAtom> &absolute() const {
    return _absoluteAtoms;
  }

  // cannot add atoms to C Runtime file
  virtual void addAtom(const Atom&) {
    llvm_unreachable("cannot add atoms to C Runtime files");
  }

private:
  llvm::BumpPtrAllocator _allocator;
  atom_collection_vector<DefinedAtom>       _definedAtoms;
  atom_collection_vector<UndefinedAtom>     _undefinedAtoms;
  atom_collection_vector<SharedLibraryAtom> _sharedLibraryAtoms;
  atom_collection_vector<AbsoluteAtom>      _absoluteAtoms;
};

} // namespace elf
} // namespace lld 

#endif // LLD_READER_WRITER_ELF_EXECUTABLE_ATOM_H_
