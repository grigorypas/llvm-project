//===- CodeGenMapTable.cpp - Instruction Mapping Table Generator ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// CodeGenMapTable provides functionality for the TableGen to create
// relation mapping between instructions. Relation models are defined using
// InstrMapping as a base class. This file implements the functionality which
// parses these definitions and generates relation maps using the information
// specified there. These maps are emitted as tables in the XXXGenInstrInfo.inc
// file along with the functions to query them.
//
// A relationship model to relate non-predicate instructions with their
// predicated true/false forms can be defined as follows:
//
// def getPredOpcode : InstrMapping {
//  let FilterClass = "PredRel";
//  let RowFields = ["BaseOpcode"];
//  let ColFields = ["PredSense"];
//  let KeyCol = ["none"];
//  let ValueCols = [["true"], ["false"]]; }
//
// CodeGenMapTable parses this map and generates a table in XXXGenInstrInfo.inc
// file that contains the instructions modeling this relationship. This table
// is defined in the function
// "int getPredOpcode(uint16_t Opcode, enum PredSense inPredSense)"
// that can be used to retrieve the predicated form of the instruction by
// passing its opcode value and the predicate sense (true/false) of the desired
// instruction as arguments.
//
// Short description of the algorithm:
//
// 1) Iterate through all the records that derive from "InstrMapping" class.
// 2) For each record, filter out instructions based on the FilterClass value.
// 3) Iterate through this set of instructions and insert them into
// RowInstrMap map based on their RowFields values. RowInstrMap is keyed by the
// vector of RowFields values and contains vectors of Records (instructions) as
// values. RowFields is a list of fields that are required to have the same
// values for all the instructions appearing in the same row of the relation
// table. All the instructions in a given row of the relation table have some
// sort of relationship with the key instruction defined by the corresponding
// relationship model.
//
// Ex: RowInstrMap(RowVal1, RowVal2, ...) -> [Instr1, Instr2, Instr3, ... ]
// Here Instr1, Instr2, Instr3 have same values (RowVal1, RowVal2) for
// RowFields. These groups of instructions are later matched against ValueCols
// to determine the column they belong to, if any.
//
// While building the RowInstrMap map, collect all the key instructions in
// KeyInstrVec. These are the instructions having the same values as KeyCol
// for all the fields listed in ColFields.
//
// For Example:
//
// Relate non-predicate instructions with their predicated true/false forms.
//
// def getPredOpcode : InstrMapping {
//  let FilterClass = "PredRel";
//  let RowFields = ["BaseOpcode"];
//  let ColFields = ["PredSense"];
//  let KeyCol = ["none"];
//  let ValueCols = [["true"], ["false"]]; }
//
// Here, only instructions that have "none" as PredSense will be selected as key
// instructions.
//
// 4) For each key instruction, get the group of instructions that share the
// same key-value as the key instruction from RowInstrMap. Iterate over the list
// of columns in ValueCols (it is defined as a list<list<string> >. Therefore,
// it can specify multi-column relationships). For each column, find the
// instruction from the group that matches all the values for the column.
// Multiple matches are not allowed.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "TableGenBackends.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;
typedef std::map<std::string, std::vector<const Record *>> InstrRelMapTy;
typedef std::map<std::vector<const Init *>, std::vector<const Record *>>
    RowInstrMapTy;

namespace {

//===----------------------------------------------------------------------===//
// This class is used to represent InstrMapping class defined in Target.td file.
class InstrMap {
private:
  std::string Name;
  std::string FilterClass;
  const ListInit *RowFields;
  const ListInit *ColFields;
  const ListInit *KeyCol;
  std::vector<const ListInit *> ValueCols;

public:
  InstrMap(const Record *MapRec) {
    Name = MapRec->getName().str();

    // FilterClass - It's used to reduce the search space only to the
    // instructions that define the kind of relationship modeled by
    // this InstrMapping object/record.
    const RecordVal *Filter = MapRec->getValue("FilterClass");
    FilterClass = Filter->getValue()->getAsUnquotedString();

    // List of fields/attributes that need to be same across all the
    // instructions in a row of the relation table.
    RowFields = MapRec->getValueAsListInit("RowFields");

    // List of fields/attributes that are constant across all the instruction
    // in a column of the relation table. Ex: ColFields = 'predSense'
    ColFields = MapRec->getValueAsListInit("ColFields");

    // Values for the fields/attributes listed in 'ColFields'.
    // Ex: KeyCol = 'noPred' -- key instruction is non-predicated
    KeyCol = MapRec->getValueAsListInit("KeyCol");

    // List of values for the fields/attributes listed in 'ColFields', one for
    // each column in the relation table.
    //
    // Ex: ValueCols = [['true'],['false']] -- it results two columns in the
    // table. First column requires all the instructions to have predSense
    // set to 'true' and second column requires it to be 'false'.
    const ListInit *ColValList = MapRec->getValueAsListInit("ValueCols");

    // Each instruction map must specify at least one column for it to be valid.
    if (ColValList->empty())
      PrintFatalError(MapRec->getLoc(), "InstrMapping record `" + Name +
                                            "' has empty " +
                                            "`ValueCols' field!");

    for (const Init *I : ColValList->getElements()) {
      const auto *ColI = cast<ListInit>(I);

      // Make sure that all the sub-lists in 'ValueCols' have same number of
      // elements as the fields in 'ColFields'.
      if (ColI->size() != ColFields->size())
        PrintFatalError(MapRec->getLoc(),
                        "Record `" + Name +
                            "', field `ValueCols' entries don't match with " +
                            " the entries in 'ColFields'!");
      ValueCols.push_back(ColI);
    }
  }

  const std::string &getName() const { return Name; }
  const std::string &getFilterClass() const { return FilterClass; }
  const ListInit *getRowFields() const { return RowFields; }
  const ListInit *getColFields() const { return ColFields; }
  const ListInit *getKeyCol() const { return KeyCol; }
  ArrayRef<const ListInit *> getValueCols() const { return ValueCols; }
};

//===----------------------------------------------------------------------===//
// class MapTableEmitter : It builds the instruction relation maps using
// the information provided in InstrMapping records. It outputs these
// relationship maps as tables into XXXGenInstrInfo.inc file along with the
// functions to query them.

class MapTableEmitter {
private:
  //  std::string TargetName;
  const CodeGenTarget &Target;
  // InstrMapDesc - InstrMapping record to be processed.
  InstrMap InstrMapDesc;

  // InstrDefs - list of instructions filtered using FilterClass defined
  // in InstrMapDesc.
  ArrayRef<const Record *> InstrDefs;

  // RowInstrMap - maps RowFields values to the instructions. It's keyed by the
  // values of the row fields and contains vector of records as values.
  RowInstrMapTy RowInstrMap;

  // KeyInstrVec - list of key instructions.
  std::vector<const Record *> KeyInstrVec;
  DenseMap<const Record *, std::vector<const Record *>> MapTable;

public:
  MapTableEmitter(const CodeGenTarget &Target, const RecordKeeper &Records,
                  const Record *IMRec)
      : Target(Target), InstrMapDesc(IMRec) {
    const std::string &FilterClass = InstrMapDesc.getFilterClass();
    InstrDefs = Records.getAllDerivedDefinitions(FilterClass);
  }

  void buildRowInstrMap();

  // Returns true if an instruction is a key instruction, i.e., its ColFields
  // have same values as KeyCol.
  bool isKeyColInstr(const Record *CurInstr);

  // Find column instruction corresponding to a key instruction based on the
  // constraints for that column.
  const Record *getInstrForColumn(const Record *KeyInstr,
                                  const ListInit *CurValueCol);

  // Find column instructions for each key instruction based
  // on ValueCols and store them into MapTable.
  void buildMapTable();

  void emitBinSearch(raw_ostream &OS, unsigned TableSize);
  void emitTablesWithFunc(raw_ostream &OS);
  unsigned emitBinSearchTable(raw_ostream &OS);

  // Lookup functions to query binary search tables.
  void emitMapFuncBody(raw_ostream &OS, unsigned TableSize);
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Process all the instructions that model this relation (alreday present in
// InstrDefs) and insert them into RowInstrMap which is keyed by the values of
// the fields listed as RowFields. It stores vectors of records as values.
// All the related instructions have the same values for the RowFields thus are
// part of the same key-value pair.
//===----------------------------------------------------------------------===//

void MapTableEmitter::buildRowInstrMap() {
  for (const Record *CurInstr : InstrDefs) {
    std::vector<const Init *> KeyValue;
    const ListInit *RowFields = InstrMapDesc.getRowFields();
    for (const Init *RowField : RowFields->getElements()) {
      const RecordVal *RecVal = CurInstr->getValue(RowField);
      if (RecVal == nullptr)
        PrintFatalError(CurInstr->getLoc(),
                        "No value " + RowField->getAsString() + " found in \"" +
                            CurInstr->getName() +
                            "\" instruction description.");
      const Init *CurInstrVal = RecVal->getValue();
      KeyValue.push_back(CurInstrVal);
    }

    // Collect key instructions into KeyInstrVec. Later, these instructions are
    // processed to assign column position to the instructions sharing
    // their KeyValue in RowInstrMap.
    if (isKeyColInstr(CurInstr))
      KeyInstrVec.push_back(CurInstr);

    RowInstrMap[KeyValue].push_back(CurInstr);
  }
}

//===----------------------------------------------------------------------===//
// Return true if an instruction is a KeyCol instruction.
//===----------------------------------------------------------------------===//

bool MapTableEmitter::isKeyColInstr(const Record *CurInstr) {
  const ListInit *ColFields = InstrMapDesc.getColFields();
  const ListInit *KeyCol = InstrMapDesc.getKeyCol();

  // Check if the instruction is a KeyCol instruction.
  bool MatchFound = true;
  for (unsigned J = 0, EndCf = ColFields->size(); (J < EndCf) && MatchFound;
       J++) {
    const RecordVal *ColFieldName =
        CurInstr->getValue(ColFields->getElement(J));
    std::string CurInstrVal = ColFieldName->getValue()->getAsUnquotedString();
    std::string KeyColValue = KeyCol->getElement(J)->getAsUnquotedString();
    MatchFound = CurInstrVal == KeyColValue;
  }
  return MatchFound;
}

//===----------------------------------------------------------------------===//
// Build a map to link key instructions with the column instructions arranged
// according to their column positions.
//===----------------------------------------------------------------------===//

void MapTableEmitter::buildMapTable() {
  // Find column instructions for a given key based on the ColField
  // constraints.
  ArrayRef<const ListInit *> ValueCols = InstrMapDesc.getValueCols();
  unsigned NumOfCols = ValueCols.size();
  for (const Record *CurKeyInstr : KeyInstrVec) {
    std::vector<const Record *> ColInstrVec(NumOfCols);

    // Find the column instruction based on the constraints for the column.
    for (unsigned ColIdx = 0; ColIdx < NumOfCols; ColIdx++) {
      const ListInit *CurValueCol = ValueCols[ColIdx];
      const Record *ColInstr = getInstrForColumn(CurKeyInstr, CurValueCol);
      ColInstrVec[ColIdx] = ColInstr;
    }
    MapTable[CurKeyInstr] = ColInstrVec;
  }
}

//===----------------------------------------------------------------------===//
// Find column instruction based on the constraints for that column.
//===----------------------------------------------------------------------===//

const Record *MapTableEmitter::getInstrForColumn(const Record *KeyInstr,
                                                 const ListInit *CurValueCol) {
  const ListInit *RowFields = InstrMapDesc.getRowFields();
  std::vector<const Init *> KeyValue;

  // Construct KeyValue using KeyInstr's values for RowFields.
  for (const Init *RowField : RowFields->getElements()) {
    const Init *KeyInstrVal = KeyInstr->getValue(RowField)->getValue();
    KeyValue.push_back(KeyInstrVal);
  }

  // Get all the instructions that share the same KeyValue as the KeyInstr
  // in RowInstrMap. We search through these instructions to find a match
  // for the current column, i.e., the instruction which has the same values
  // as CurValueCol for all the fields in ColFields.
  ArrayRef<const Record *> RelatedInstrVec = RowInstrMap[KeyValue];

  const ListInit *ColFields = InstrMapDesc.getColFields();
  const Record *MatchInstr = nullptr;

  for (const Record *CurInstr : RelatedInstrVec) {
    bool MatchFound = true;
    for (unsigned J = 0, EndCf = ColFields->size(); (J < EndCf) && MatchFound;
         J++) {
      const Init *ColFieldJ = ColFields->getElement(J);
      const Init *CurInstrInit = CurInstr->getValue(ColFieldJ)->getValue();
      std::string CurInstrVal = CurInstrInit->getAsUnquotedString();
      const Init *ColFieldJVallue = CurValueCol->getElement(J);
      MatchFound = CurInstrVal == ColFieldJVallue->getAsUnquotedString();
    }

    if (MatchFound) {
      if (MatchInstr) {
        // Already had a match
        // Error if multiple matches are found for a column.
        std::string KeyValueStr;
        for (const Init *Value : KeyValue) {
          if (!KeyValueStr.empty())
            KeyValueStr += ", ";
          KeyValueStr += Value->getAsString();
        }

        PrintFatalError("Multiple matches found for `" + KeyInstr->getName() +
                        "', for the relation `" + InstrMapDesc.getName() +
                        "', row fields [" + KeyValueStr + "], column `" +
                        CurValueCol->getAsString() + "'");
      }
      MatchInstr = CurInstr;
    }
  }
  return MatchInstr;
}

//===----------------------------------------------------------------------===//
// Emit one table per relation. Only instructions with a valid relation of a
// given type are included in the table sorted by their enum values (opcodes).
// Binary search is used for locating instructions in the table.
//===----------------------------------------------------------------------===//

unsigned MapTableEmitter::emitBinSearchTable(raw_ostream &OS) {
  ArrayRef<const CodeGenInstruction *> NumberedInstructions =
      Target.getInstructions();
  StringRef Namespace = Target.getInstNamespace();
  ArrayRef<const ListInit *> ValueCols = InstrMapDesc.getValueCols();
  unsigned NumCol = ValueCols.size();
  unsigned TableSize = 0;

  OS << "  using namespace " << Namespace << ";\n";
  // Number of columns in the table are NumCol+1 because key instructions are
  // emitted as first column.
  for (const CodeGenInstruction *Inst : NumberedInstructions) {
    const Record *CurInstr = Inst->TheDef;
    ArrayRef<const Record *> ColInstrs = MapTable[CurInstr];
    if (ColInstrs.empty())
      continue;
    std::string OutStr;
    bool RelExists = false;
    for (const Record *ColInstr : ColInstrs) {
      if (ColInstr) {
        RelExists = true;
        OutStr += ", ";
        OutStr += ColInstr->getName();
      } else {
        OutStr += ", (uint16_t)-1U";
      }
    }

    if (RelExists) {
      if (TableSize == 0)
        OS << "  static constexpr uint16_t Table[][" << NumCol + 1 << "] = {\n";
      OS << "    { " << CurInstr->getName() << OutStr << " },\n";
      ++TableSize;
    }
  }

  if (TableSize != 0)
    OS << "  }; // End of Table\n\n";
  return TableSize;
}

//===----------------------------------------------------------------------===//
// Emit binary search algorithm as part of the functions used to query
// relation tables.
//===----------------------------------------------------------------------===//

void MapTableEmitter::emitBinSearch(raw_ostream &OS, unsigned TableSize) {
  if (TableSize == 0) {
    OS << "  return -1;\n";
    return;
  }

  OS << "  unsigned mid;\n";
  OS << "  unsigned start = 0;\n";
  OS << "  unsigned end = " << TableSize << ";\n";
  OS << "  while (start < end) {\n";
  OS << "    mid = start + (end - start) / 2;\n";
  OS << "    if (Opcode == Table[mid][0]) \n";
  OS << "      break;\n";
  OS << "    if (Opcode < Table[mid][0])\n";
  OS << "      end = mid;\n";
  OS << "    else\n";
  OS << "      start = mid + 1;\n";
  OS << "  }\n";
  OS << "  if (start == end)\n";
  OS << "    return -1; // Instruction doesn't exist in this table.\n\n";
}

//===----------------------------------------------------------------------===//
// Emit functions to query relation tables.
//===----------------------------------------------------------------------===//

void MapTableEmitter::emitMapFuncBody(raw_ostream &OS, unsigned TableSize) {
  const ListInit *ColFields = InstrMapDesc.getColFields();
  ArrayRef<const ListInit *> ValueCols = InstrMapDesc.getValueCols();

  // Emit binary search algorithm to locate instructions in the
  // relation table. If found, return opcode value from the appropriate column
  // of the table.
  emitBinSearch(OS, TableSize);
  if (TableSize == 0)
    return;

  if (ValueCols.size() > 1) {
    for (unsigned I = 0, E = ValueCols.size(); I < E; I++) {
      const ListInit *ColumnI = ValueCols[I];
      OS << "  if (";
      for (unsigned J = 0, ColSize = ColumnI->size(); J < ColSize; ++J) {
        std::string ColName = ColFields->getElement(J)->getAsUnquotedString();
        OS << "in" << ColName;
        OS << " == ";
        OS << ColName << "_" << ColumnI->getElement(J)->getAsUnquotedString();
        if (J < ColumnI->size() - 1)
          OS << " && ";
      }
      OS << ")\n";
      OS << "    return Table[mid][" << I + 1 << "];\n";
    }
    OS << "  return -1;";
  } else {
    OS << "  return Table[mid][1];\n";
  }
}

//===----------------------------------------------------------------------===//
// Emit relation tables and the functions to query them.
//===----------------------------------------------------------------------===//

void MapTableEmitter::emitTablesWithFunc(raw_ostream &OS) {
  // Emit function name and the input parameters : mostly opcode value of the
  // current instruction. However, if a table has multiple columns (more than 2
  // since first column is used for the key instructions), then we also need
  // to pass another input to indicate the column to be selected.

  const ListInit *ColFields = InstrMapDesc.getColFields();
  ArrayRef<const ListInit *> ValueCols = InstrMapDesc.getValueCols();
  OS << "// " << InstrMapDesc.getName() << "\nLLVM_READONLY\n";
  OS << "int " << InstrMapDesc.getName() << "(uint16_t Opcode";
  if (ValueCols.size() > 1) {
    for (const Init *CF : ColFields->getElements()) {
      std::string ColName = CF->getAsUnquotedString();
      OS << ", enum " << ColName << " in" << ColName;
    }
  }
  OS << ") {\n";

  // Emit map table.
  unsigned TableSize = emitBinSearchTable(OS);

  // Emit rest of the function body.
  emitMapFuncBody(OS, TableSize);

  OS << "}\n\n";
}

//===----------------------------------------------------------------------===//
// Emit enums for the column fields across all the instruction maps.
//===----------------------------------------------------------------------===//

static void emitEnums(raw_ostream &OS, const RecordKeeper &Records) {
  std::map<std::string, SetVector<const Init *>> ColFieldValueMap;

  // Iterate over all InstrMapping records and create a map between column
  // fields and their possible values across all records.
  for (const Record *CurMap :
       Records.getAllDerivedDefinitions("InstrMapping")) {
    const ListInit *ColFields = CurMap->getValueAsListInit("ColFields");
    const ListInit *List = CurMap->getValueAsListInit("ValueCols");
    std::vector<const ListInit *> ValueCols;

    for (const Init *Elem : *List) {
      const auto *ListJ = cast<ListInit>(Elem);

      if (ListJ->size() != ColFields->size())
        PrintFatalError("Record `" + CurMap->getName() +
                        "', field "
                        "`ValueCols' entries don't match with the entries in "
                        "'ColFields' !");
      ValueCols.push_back(ListJ);
    }

    for (unsigned J = 0, EndCf = ColFields->size(); J < EndCf; J++) {
      std::string ColName = ColFields->getElement(J)->getAsUnquotedString();
      auto &MapEntry = ColFieldValueMap[ColName];
      for (const ListInit *List : ValueCols)
        MapEntry.insert(List->getElement(J));
    }
  }

  for (auto &[EnumName, FieldValues] : ColFieldValueMap) {
    // Emit enumerated values for the column fields.
    OS << "enum " << EnumName << " {\n";
    ListSeparator LS(",\n");
    for (const Init *Field : FieldValues)
      OS << LS << "  " << EnumName << "_" << Field->getAsUnquotedString();
    OS << "\n};\n\n";
  }
}

//===----------------------------------------------------------------------===//
// Parse 'InstrMapping' records and use the information to form relationship
// between instructions. These relations are emitted as tables along with the
// functions to query them.
//===----------------------------------------------------------------------===//
void llvm::EmitMapTable(const RecordKeeper &Records, raw_ostream &OS) {
  CodeGenTarget Target(Records);
  StringRef NameSpace = Target.getInstNamespace();
  ArrayRef<const Record *> InstrMapVec =
      Records.getAllDerivedDefinitions("InstrMapping");

  if (InstrMapVec.empty())
    return;

  OS << "#ifdef GET_INSTRMAP_INFO\n";
  OS << "#undef GET_INSTRMAP_INFO\n";
  OS << "namespace llvm::" << NameSpace << " {\n\n";

  // Emit coulumn field names and their values as enums.
  emitEnums(OS, Records);

  // Iterate over all instruction mapping records and construct relationship
  // maps based on the information specified there.
  //
  for (const Record *CurMap : InstrMapVec) {
    MapTableEmitter IMap(Target, Records, CurMap);

    // Build RowInstrMap to group instructions based on their values for
    // RowFields. In the process, also collect key instructions into
    // KeyInstrVec.
    IMap.buildRowInstrMap();

    // Build MapTable to map key instructions with the corresponding column
    // instructions.
    IMap.buildMapTable();

    // Emit map tables and the functions to query them.
    IMap.emitTablesWithFunc(OS);
  }
  OS << "} // end namespace llvm::" << NameSpace << '\n';
  OS << "#endif // GET_INSTRMAP_INFO\n\n";
}
