/*
 * EECS 370, University of Michigan, Fall 2023
 * Project 3: LC-2K Pipeline Simulator
 * Instructions are found in the project spec: https://eecs370.github.io/project_3_spec/
 * Make sure NOT to modify printState or any of the associated functions
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Machine Definitions
#define NUMMEMORY 65536 // maximum number of data words in memory
#define NUMREGS 8       // number of machine registers

#define ADD 0
#define NOR 1
#define LW 2
#define SW 3
#define BEQ 4
#define JALR 5 // will not implemented for Project 3
#define HALT 6
#define NOOP 7

const char *opcode_to_str_map[] = {
    "add", "nor", "lw", "sw", "beq", "jalr", "halt", "noop"};

#define NOOPINSTR (NOOP << 22)

// Pipeline Register Structs
typedef struct IFIDStruct
{
    int instr;
    int pcPlus1;
} IFIDType;

typedef struct IDEXStruct
{
    int instr;
    int pcPlus1;
    int valA;
    int valB;
    int offset;
} IDEXType;

typedef struct EXMEMStruct
{
    int instr;
    int branchTarget;
    int eq;
    int aluResult;
    int valB;
} EXMEMType;

typedef struct MEMWBStruct
{
    int instr;
    int writeData;
} MEMWBType;

typedef struct WBENDStruct
{
    int instr;
    int writeData;
} WBENDType;

typedef struct stateStruct
{
    unsigned int numMemory;
    unsigned int cycles;
    int pc;
    int instrMem[NUMMEMORY];
    int dataMem[NUMMEMORY];
    int reg[NUMREGS];
    IFIDType IFID;
    IDEXType IDEX;
    EXMEMType EXMEM;
    MEMWBType MEMWB;
    WBENDType WBEND;
} stateType;

// Helper Functions
static inline int opcode(int instruction)
{
    return instruction >> 22;
}

static inline int field0(int instruction)
{
    return (instruction >> 19) & 0x7;
}

static inline int field1(int instruction)
{
    return (instruction >> 16) & 0x7;
}

static inline int field2(int instruction)
{
    return instruction & 0xFFFF;
}

static inline int convertNum(int num)
{
    return num - ((num & (1 << 15)) ? 1 << 16 : 0);
}

// Function Prototypes
void printState(stateType *);
void printInstruction(int);
void readMachineCode(stateType *, char *);

// Helper function prototypes
void initializeState(stateType *state);
void handleBranchAndFetch(stateType *state, stateType *newState);
void decodeInstruction(stateType *state, stateType *newState);
void handleLoadUseHazard(stateType *state, stateType *newState);
void executeInstruction(stateType *state, stateType *newState);
void performMemoryOperation(stateType *state, stateType *newState);
void writeBack(stateType *state, stateType *newState);

int main(int argc, char *argv[])
{
    static stateType state, newState;

    if (argc != 2)
    {
        printf("error: usage: %s <machine-code file>\n", argv[0]);
        exit(1);
    }

    readMachineCode(&state, argv[1]);
    initializeState(&state);
    newState = state;

    while (opcode(state.MEMWB.instr) != HALT)
    {
        printState(&state);
        newState.cycles += 1;

        /* ---------------------- IF stage --------------------- */
        handleBranchAndFetch(&state, &newState);

        /* ---------------------- ID stage --------------------- */
        decodeInstruction(&state, &newState);
        handleLoadUseHazard(&state, &newState);

        /* ---------------------- EX stage --------------------- */
        executeInstruction(&state, &newState);

        /* --------------------- MEM stage --------------------- */
        performMemoryOperation(&state, &newState);

        /* ---------------------- WB stage --------------------- */
        writeBack(&state, &newState);

        /* ------------------------ END ------------------------ */
        state = newState;
    }

    printf("Machine halted\n");
    printf("Total of %d cycles executed\n", state.cycles);
    printf("Final state of machine:\n");
    printState(&state);
}

void initializeState(stateType *state)
{
    state->pc = 0;
    state->cycles = 0;
    state->IFID.instr = NOOPINSTR;
    state->IDEX.instr = NOOPINSTR;
    state->EXMEM.instr = NOOPINSTR;
    state->MEMWB.instr = NOOPINSTR;
    state->WBEND.instr = NOOPINSTR;

    for (int i = 0; i < NUMREGS; i++)
    {
        state->reg[i] = 0;
    }
}

void handleBranchAndFetch(stateType *state, stateType *newState)
{
    if (opcode(state->EXMEM.instr) == BEQ && state->EXMEM.eq)
    {
        // Branch taken - flush pipeline and update PC
        newState->pc = state->EXMEM.branchTarget;
        newState->IFID.instr = NOOPINSTR;
        newState->IDEX.instr = NOOPINSTR;
        newState->EXMEM.instr = NOOPINSTR;
        newState->IFID.pcPlus1 = state->EXMEM.branchTarget + 1;
    }
    else
    {
        // Normal instruction fetch
        newState->IFID.instr = state->instrMem[state->pc];
        newState->IFID.pcPlus1 = state->pc + 1;
        newState->pc = state->pc + 1;
    }
}

void decodeInstruction(stateType *state, stateType *newState)
{
    newState->IDEX.instr = state->IFID.instr;
    newState->IDEX.pcPlus1 = state->IFID.pcPlus1;
    newState->IDEX.offset = convertNum(field2(state->IFID.instr));
    newState->IDEX.valA = state->reg[field0(state->IFID.instr)];
    newState->IDEX.valB = state->reg[field1(state->IFID.instr)];
}

void handleLoadUseHazard(stateType *state, stateType *newState)
{
    if (opcode(state->IDEX.instr) == LW)
    {
        int loadDest = field1(state->IDEX.instr);
        int nextInstrOp = opcode(state->IFID.instr);

        // Check arithmetic and memory instructions
        if (nextInstrOp == ADD || nextInstrOp == NOR ||
            nextInstrOp == SW || nextInstrOp == BEQ)
        {
            if (field0(state->IFID.instr) == loadDest ||
                field1(state->IFID.instr) == loadDest)
            {
                // Stall the pipeline
                newState->IFID = state->IFID;
                newState->pc = state->pc;
                newState->IDEX.instr = NOOPINSTR;
            }
        }
        // Special case for load instruction
        else if (nextInstrOp == LW)
        {
            if (field0(state->IFID.instr) == loadDest)
            {
                // Stall the pipeline
                newState->IFID = state->IFID;
                newState->pc = state->pc;
                newState->IDEX.instr = NOOPINSTR;
            }
        }
    }

    else if (opcode(state->IDEX.instr) == LW &&
             (field0(newState->IDEX.instr) == field1(state->IDEX.instr) ||
              field1(newState->IDEX.instr) == field1(state->IDEX.instr)))
    {
        if (field1(state->IDEX.instr) == field0(state->IFID.instr) ||
            field1(state->IDEX.instr) == field1(state->IFID.instr))
        {
            newState->IFID = state->IFID;
            newState->IDEX.instr = NOOPINSTR;
            newState->pc = state->pc;
        }
    }
}

void executeInstruction(stateType *state, stateType *newState)
{
    // Setup for execution
    int regA = state->IDEX.valA;
    int regB = state->IDEX.valB;
    newState->EXMEM.instr = state->IDEX.instr;
    newState->EXMEM.branchTarget = state->IDEX.pcPlus1 + state->IDEX.offset;

    // Data forwarding logic
    if (opcode(state->IDEX.instr) != NOOP)
    {
        int destA = field0(state->IDEX.instr);
        int destB = field1(state->IDEX.instr);

        // Forward from WB/END stage
        if (opcode(state->WBEND.instr) == ADD || opcode(state->WBEND.instr) == NOR ||
            opcode(state->WBEND.instr) == LW)
        {
            int wbendDest = (opcode(state->WBEND.instr) == LW) ? field1(state->WBEND.instr) : field2(state->WBEND.instr);
            if (destA == wbendDest)
                regA = state->WBEND.writeData;
            if (destB == wbendDest)
                regB = state->WBEND.writeData;
        }

        // Forward from MEM/WB stage
        if (opcode(state->MEMWB.instr) == ADD || opcode(state->MEMWB.instr) == NOR ||
            opcode(state->MEMWB.instr) == LW)
        {
            int memwbDest = (opcode(state->MEMWB.instr) == LW) ? field1(state->MEMWB.instr) : field2(state->MEMWB.instr);
            if (destA == memwbDest)
                regA = state->MEMWB.writeData;
            if (destB == memwbDest)
                regB = state->MEMWB.writeData;
        }

        // Forward from EX/MEM stage
        if (opcode(state->EXMEM.instr) == ADD || opcode(state->EXMEM.instr) == NOR ||
            opcode(state->EXMEM.instr) == LW)
        {
            int exmemDest = (opcode(state->EXMEM.instr) == LW) ? field1(state->EXMEM.instr) : field2(state->EXMEM.instr);
            if (destA == exmemDest)
                regA = state->EXMEM.aluResult;
            if (destB == exmemDest)
                regB = state->EXMEM.aluResult;
        }
    }

    // ALU operation
    switch (opcode(state->IDEX.instr))
    {
    case ADD:
        newState->EXMEM.aluResult = regA + regB;
        newState->EXMEM.valB = regB;
        break;
    case NOR:
        newState->EXMEM.aluResult = ~(regA | regB);
        newState->EXMEM.valB = regB;
        break;
    case LW:
    case SW:
        newState->EXMEM.aluResult = regA + state->IDEX.offset;
        newState->EXMEM.valB = regB;
        break;
    case BEQ:
        newState->EXMEM.eq = (regA == regB);
        newState->EXMEM.valB = regB;
        newState->EXMEM.branchTarget = state->IDEX.pcPlus1 + state->IDEX.offset;
        break;
    }
}

void performMemoryOperation(stateType *state, stateType *newState)
{
    newState->MEMWB.instr = state->EXMEM.instr;
    int memOp = opcode(state->EXMEM.instr);

    if (memOp == BEQ)
    {
        if (state->EXMEM.eq)
        {
            newState->pc = state->EXMEM.branchTarget;
            newState->IDEX.instr = NOOPINSTR;
            newState->IFID.instr = NOOPINSTR;
            newState->EXMEM.instr = NOOPINSTR;
        }
    }
    else if (memOp == LW)
    {
        newState->MEMWB.writeData = state->dataMem[state->EXMEM.aluResult];
    }
    else if (memOp == SW)
    {
        newState->dataMem[state->EXMEM.aluResult] = state->EXMEM.valB;
    }
    else if (memOp == ADD || memOp == NOR)
    {
        newState->MEMWB.writeData = state->EXMEM.aluResult;
    }
}

void writeBack(stateType *state, stateType *newState)
{
    newState->WBEND.instr = state->MEMWB.instr;
    newState->WBEND.writeData = state->MEMWB.writeData;

    int wbOp = opcode(state->MEMWB.instr);
    if (wbOp == ADD || wbOp == NOR)
    {
        newState->reg[field2(state->MEMWB.instr)] = state->MEMWB.writeData;
    }
    else if (wbOp == LW)
    {
        newState->reg[field1(state->MEMWB.instr)] = state->MEMWB.writeData;
    }
}

/*
 * DO NOT MODIFY ANY OF THE CODE BELOW.
 */
void printInstruction(int instr)
{
    const char *instr_opcode_str;
    int instr_opcode = opcode(instr);
    if (ADD <= instr_opcode && instr_opcode <= NOOP)
    {
        instr_opcode_str = opcode_to_str_map[instr_opcode];
    }
    switch (instr_opcode)
    {
    case ADD:
    case NOR:
    case LW:
    case SW:
    case BEQ:
        printf("%s %d %d %d", instr_opcode_str, field0(instr), field1(instr), convertNum(field2(instr)));
        break;
    case JALR:
        printf("%s %d %d", instr_opcode_str, field0(instr), field1(instr));
        break;
    case HALT:
    case NOOP:
        printf("%s", instr_opcode_str);
        break;
    default:
        printf(".fill %d", instr);
        return;
    }
}
void printState(stateType *statePtr)
{
    printf("\n@@@\n");
    printf("state before cycle %d starts:\n", statePtr->cycles);
    printf("\tpc = %d\n", statePtr->pc);
    printf("\tdata memory:\n");
    for (int i = 0; i < statePtr->numMemory; ++i)
    {
        printf("\t\tdataMem[ %d ] = 0x%08X\n", i, statePtr->dataMem[i]);
    }
    printf("\tregisters:\n");
    for (int i = 0; i < NUMREGS; ++i)
    {
        printf("\t\treg[ %d ] = %d\n", i, statePtr->reg[i]);
    }
    // IF/ID
    printf("\tIF/ID pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->IFID.instr);
    printInstruction(statePtr->IFID.instr);
    printf(" )\n");
    printf("\t\tpcPlus1 = %d", statePtr->IFID.pcPlus1);
    if (opcode(statePtr->IFID.instr) == NOOP)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    // ID/EX
    int idexOp = opcode(statePtr->IDEX.instr);
    printf("\tID/EX pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->IDEX.instr);
    printInstruction(statePtr->IDEX.instr);
    printf(" )\n");
    printf("\t\tpcPlus1 = %d", statePtr->IDEX.pcPlus1);
    if (idexOp == NOOP)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegA = %d", statePtr->IDEX.valA);
    if (idexOp >= HALT || idexOp < 0)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegB = %d", statePtr->IDEX.valB);
    if (idexOp == LW || idexOp > BEQ || idexOp < 0)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\toffset = %d", statePtr->IDEX.offset);
    if (idexOp != LW && idexOp != SW && idexOp != BEQ)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    // EX/MEM
    int exmemOp = opcode(statePtr->EXMEM.instr);
    printf("\tEX/MEM pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->EXMEM.instr);
    printInstruction(statePtr->EXMEM.instr);
    printf(" )\n");
    printf("\t\tbranchTarget %d", statePtr->EXMEM.branchTarget);
    if (exmemOp != BEQ)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\teq ? %s", (statePtr->EXMEM.eq ? "True" : "False"));
    if (exmemOp != BEQ)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\taluResult = %d", statePtr->EXMEM.aluResult);
    if (exmemOp > SW || exmemOp < 0)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegB = %d", statePtr->EXMEM.valB);
    if (exmemOp != SW)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    // MEM/WB
    int memwbOp = opcode(statePtr->MEMWB.instr);
    printf("\tMEM/WB pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->MEMWB.instr);
    printInstruction(statePtr->MEMWB.instr);
    printf(" )\n");
    printf("\t\twriteData = %d", statePtr->MEMWB.writeData);
    if (memwbOp >= SW || memwbOp < 0)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    // WB/END
    int wbendOp = opcode(statePtr->WBEND.instr);
    printf("\tWB/END pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->WBEND.instr);
    printInstruction(statePtr->WBEND.instr);
    printf(" )\n");
    printf("\t\twriteData = %d", statePtr->WBEND.writeData);
    if (wbendOp >= SW || wbendOp < 0)
    {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("end state\n");
    fflush(stdout);
}
// File
#define MAXLINELENGTH 1000 // MAXLINELENGTH is the max number of characters we read
void readMachineCode(stateType *state, char *filename)
{
    char line[MAXLINELENGTH];
    FILE *filePtr = fopen(filename, "r");
    if (filePtr == NULL)
    {
        printf("error: can't open file %s", filename);
        exit(1);
    }
    printf("instruction memory:\n");
    for (state->numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL; ++state->numMemory)
    {
        if (sscanf(line, "%x", state->instrMem + state->numMemory) != 1)
        {
            printf("error in reading address %d\n", state->numMemory);
            exit(1);
        }
        // Initialize dataMem with instrMem content
        state->dataMem[state->numMemory] = state->instrMem[state->numMemory];
        printf("\tinstrMem[ %d ] = 0x%08X ( ", state->numMemory,
               state->instrMem[state->numMemory]);
        printInstruction(state->dataMem[state->numMemory]);
        printf(" )\n");
    }
}