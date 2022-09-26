#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "Basic.h"

vector<BasicMachine::tInstructionInfo> BasicMachine::instructionInfo;
vector<BasicMachine::tFunctionInfo> BasicMachine::functionInfo;
vector<BasicMachine::tOperatorInfo> BasicMachine::operatorInfo;
vector<BasicMachine::tSystemVarInfo> BasicMachine::systemVarInfo;

string BasicMachine::GetUserInput()
{
    printPos = 0;

    char inputBuffer[256];
    if(!suppressPrompt)
        puts("Ok");
    gets_s(inputBuffer, 256);
    return inputBuffer;
}

// The parser is either smarter or dumber than the original, still not sure - for one thing, it allows
// to omit separating colons wherever it is unambiguous (so "CLS STOP" will interpret fine, but
// "NEXT I=3" won't). Overall, I like it this way, and it does not seem to cause any problems with
// existing programs.

pair<BasicMachine::tLineNumber, BasicMachine::tStatement> BasicMachine::ParseCommandLine(const char*& ptr)
{
    pair<tLineNumber, tStatement> result{ kCommandLine, vector<byte>{} };

    IgnoreSpaces(ptr);
        
    // Empty input
    if(*ptr == 0)
        return result;

    // See if the line starts with line number
    if (isdigit(*ptr))
    {
        short lineNum = GetLineNum(ptr);
        if (lineNum < 0)
        {
            ErrorCondition("Line number is too large");
            return result;
        }

        result.first = lineNum;
    }

    IgnoreSpaces(ptr);

    // The rest of the line is the sequence of instructions separated by colons (a little bit more
    // complex than that, but not relevant here). Colons are ignored, they can be deduced from the
    // tokenized state. Go instruction by instruction. If no instruction matched, assume it is an
    // assignment, almost identical to LET.
    tStatement tokenizedInput;

    // Continuation?
    if(IsNextSymbolDrop(ptr, ':') && lastLineNum > kCommandLine && program.find(lastLineNum) != program.end())
    {
        IgnoreSpaces(ptr);
        result.first = lastLineNum;
        tokenizedInput = program[lastLineNum];
    }
    else
    {
        parseIfStack.clear();
    }

    while (*ptr && !inErrorCondition)
    {
        // Identify the instruction
        bool matched = false;
        int code = 0;
        for (auto i : instructionInfo)
        {
            if (matched = Match(ptr, i.name))
            {
                if (i.do_parse != nullptr)
                {
                    tokenizedInput.push_back((byte)code);
                    size_t bookmark = ReserveParmsLength(tokenizedInput);
                    if (!i.do_parse(*this, tokenizedInput, ptr))
                    {
                        string err = "Syntax error in ";
                        err += i.name;
                        ErrorCondition(err.c_str());
                    }
                    if (!EncodeParmsLength(tokenizedInput, bookmark))
                        ErrorCondition("The statement is too long");
                }
                break;
            }
            ++code;
        }

        // If no instruction matched, this may be an assignment or an implicit GOTO (in THEN or ELSE)
        if (!matched)
        {
            IgnoreSpaces(ptr);
            int instructionToTry = isdigit(*ptr) ? 1 : 0;

            tokenizedInput.push_back((byte)instructionToTry);
            size_t bookmark = ReserveParmsLength(tokenizedInput);
            if (!instructionInfo[instructionToTry].do_parse(*this, tokenizedInput, ptr))
                ErrorCondition("Syntax error");
            if (!EncodeParmsLength(tokenizedInput, bookmark))
                ErrorCondition("The statement is too long");
        }

        IgnoreSpaces(ptr);
    }

    result.second = move(tokenizedInput);
    lastLineNum = result.first;
    return result;
}

void BasicMachine::ExecuteAtPC()
{
    if (TestKeyboard() == 27)
    {
        ExecuteEnd(nullptr);
        return;
    }

    byte instruction = executionPointer.lineNum > kCommandLine ? executionPointer.cachedStatement->second[executionPointer.offset] : commandLine[executionPointer.offset];
    byte* parms = executionPointer.lineNum > kCommandLine ? &executionPointer.cachedStatement->second[executionPointer.offset+1] : &commandLine[executionPointer.offset+1];
    AdvanceExecutionPointer();

    if(!executionPointer.skipForNext || instructionInfo[(int)instruction].nextStatement)
        instructionInfo[(int)instruction].do_execute(*this, parms);
}

void BasicMachine::Init()
{
    if (!instructionInfo.empty())
        return;

#define INSTRUCTION(n, p, e, l) instructionInfo.push_back({ n, mem_fn(&BasicMachine::p), mem_fn(&BasicMachine::e), mem_fn(&BasicMachine::l), false, false, false })
#define INSTRUCTION_NOPARMS(n, e) INSTRUCTION(n, ParseNoParms, e, ListNoParms)
#define INSTRUCTION_INTERNAL(n) INSTRUCTION(n, ParseNotAllowed, ExecuteNop, ListNoParms)
#define INSTRUCTION_IGNORE(n) instructionInfo.push_back({ n, nullptr, nullptr, nullptr })
#define SUPPRESS_COLON_BEFORE instructionInfo.back().suppressColonBefore = true;
#define SUPPRESS_COLON_AFTER instructionInfo.back().suppressColonAfter = true;
#define DATA_STATEMENT instructionInfo.back().dataStatement = true;
#define NEXT_STATEMENT instructionInfo.back().nextStatement = true;
#define IF_STATEMENT instructionInfo.back().ifStatement = true;
    INSTRUCTION("", ParseLet, ExecuteLet, ListLet); // This must be the first one in the list
    INSTRUCTION("", ParseGoto, ExecuteGoto, ListGoto); // and this must be the second one
    INSTRUCTION_IGNORE(":");
    INSTRUCTION_INTERNAL("TO");
    INSTRUCTION_INTERNAL("STEP");
    INSTRUCTION_INTERNAL("THEN");
    INSTRUCTION_NOPARMS("BYE", ExecuteBye);
    INSTRUCTION_NOPARMS("CLS", ExecuteCls);
    INSTRUCTION("DATA", ParseData, ExecuteData, ListData); DATA_STATEMENT;
    INSTRUCTION("DEF", ParseDef, ExecuteDef, ListDef);
    INSTRUCTION("DIM", ParseDim, ExecuteDim, ListDim);
    INSTRUCTION("ELSE", ParseElse, ExecuteElse, ListNoParms); SUPPRESS_COLON_BEFORE; SUPPRESS_COLON_AFTER;
    INSTRUCTION_NOPARMS("END", ExecuteEnd);
    INSTRUCTION("FOR", ParseFor, ExecuteFor, ListFor);
    INSTRUCTION("GOTO", ParseGoto, ExecuteGoto, ListGoto);
    INSTRUCTION("GOSUB", ParseGosub, ExecuteGosub, ListGosub);
    INSTRUCTION("IF", ParseIf, ExecuteIf, ListIf); IF_STATEMENT;  SUPPRESS_COLON_AFTER;
    INSTRUCTION("INPUT", ParseInput, ExecuteInput, ListInput);
    INSTRUCTION("LET", ParseLet, ExecuteLet, ListLet);
    INSTRUCTION("LIST", ParseList, ExecuteList, ListList);
    INSTRUCTION("LOAD", ParseLoad, ExecuteLoad, ListLoad);
    INSTRUCTION_NOPARMS("NEW", ExecuteNew);
    INSTRUCTION("NEXT", ParseNext, ExecuteNext, ListNext); NEXT_STATEMENT;
    INSTRUCTION("ON", ParseOn, ExecuteOn, ListOn);
    INSTRUCTION("PRINT", ParsePrint, ExecutePrint, ListPrint);
    INSTRUCTION("READ", ParseRead, ExecuteRead, ListRead);
    INSTRUCTION("REM", ParseRem, ExecuteNop, ListRem);
    INSTRUCTION_NOPARMS("RUN", ExecuteRun);
    INSTRUCTION("RESTORE", ParseRestore, ExecuteRestore, ListRestore);
    INSTRUCTION_NOPARMS("RETURN", ExecuteReturn);
    INSTRUCTION("SAVE", ParseSave, ExecuteSave, ListSave);
    INSTRUCTION_NOPARMS("STOP", ExecuteEnd);
    INSTRUCTION("RANDOMIZE", ParseRandomize, ExecuteRandomize, ListRandomize);
    INSTRUCTION_NOPARMS("DUMPVARS", ExecuteDumpVars);
#undef INSTRUCTION
#undef INSTRUCITON_NOPARMS
#undef INSTRUCTION_INTERNAL
#undef SUPPRESS_COLON_BEFORE
#undef SUPPRESS_COLON_AFTER
#undef DATA_STATEMENT
#undef NEXT_STATEMENT
#undef IF_STATEMENT

#define FUNCTION(n, e) functionInfo.push_back({n, mem_fn(&BasicMachine::e)})
    FUNCTION("ABS", ComputeABS);
    FUNCTION("ASC", ComputeASC);
    FUNCTION("ATN", ComputeATN);
    FUNCTION("CHR$", ComputeCHR);
    FUNCTION("COS", ComputeCOS);
    FUNCTION("EXP", ComputeEXP);
    FUNCTION("INT", ComputeINT);
    FUNCTION("LEFT$", ComputeLEFT);
    FUNCTION("LEN", ComputeLEN);
    FUNCTION("LOG", ComputeLOG);
    FUNCTION("MID$", ComputeMID);
    FUNCTION("RND", ComputeRND);
    FUNCTION("RIGHT$", ComputeRIGHT);
    FUNCTION("SGN", ComputeSGN);
    FUNCTION("SIN", ComputeSIN);
    FUNCTION("SQR", ComputeSQR);
    FUNCTION("STR$", ComputeSTR);
    FUNCTION("TAB", ComputeTAB);
    FUNCTION("TAN", ComputeTAN);
    FUNCTION("VAL", ComputeVAL);
#undef FUNCTION

#define OPERATOR(n, e, p) operatorInfo.push_back({ n, mem_fn(&BasicMachine::e), p, false, false, false})
#define RIGHT_ASSOC operatorInfo.back().rightAssociative = true;
#define SEPARATOR operatorInfo.back().separator = true;
#define UNARY_NEXT operatorInfo.back().unaryNext = true;
#define UNARY operatorInfo.back().unary = true;
    OPERATOR(",", ComputeComma, 10); SEPARATOR;
    OPERATOR(";", ComputeSemicolon, 10); SEPARATOR;
    OPERATOR("+", ComputeAdd, 4); UNARY_NEXT;
    OPERATOR("+", ComputeUnaryPlus, 9);  UNARY; RIGHT_ASSOC;
    OPERATOR("-", ComputeSubtract, 4); UNARY_NEXT;
    OPERATOR("-", ComputeUnaryMinus, 9); UNARY; RIGHT_ASSOC;
    OPERATOR("*", ComputeMultiply, 5);
    OPERATOR("/", ComputeDivide, 5);
    OPERATOR("^", ComputePower, 6); //RIGHT_ASSOC;
    OPERATOR("<=", ComputeLessOrEqual, 3);
    OPERATOR(">=", ComputeGreaterOrEqual, 3);
    OPERATOR("<>", ComputeNotEqual, 3);
    OPERATOR(">", ComputeGreater, 3); // These two have to go after compound ops like <=
    OPERATOR("<", ComputeLess, 3);
    OPERATOR("=", ComputeEqual, 3);
    OPERATOR("AND", ComputeAnd, 2);
    OPERATOR("OR", ComputeOr, 2);
    OPERATOR("NOT", ComputeNot, 1); UNARY; RIGHT_ASSOC;
#undef OPERATOR
#undef RIGHT_ASSOC
#undef SEPARATOR
#undef UNARY_NEXT
#undef UNARY

#define SYSTEMVAR(n, g, s) systemVarInfo.push_back({n, mem_fn(&BasicMachine::g), mem_fn(&BasicMachine::s)})
    SYSTEMVAR("INKEY$", GetVarInkey, SetProtectedVar);
    SYSTEMVAR("TIME$", GetVarTime, SetProtectedVar);
#undef SYSTEMVAR

    inErrorCondition = false;
}

// The main system loop
void BasicMachine::Run()
{
    if (instructionInfo.empty())
    {
        ErrorCondition("The system is not ready");
        return;
    }

    srand((int)time(nullptr));

    printPos = 0;
    suppressPrompt = false;

    executionPointer.lineNum = kCommandLine;
    executionPointer.offset = 0;
    executionPointer.cachedStatement = program.end();
    executionPointer.skipForNext = false;
    commandLine.clear();

    readPointer.lineNum = kCommandLine;
    readPointer.offset = 0;
    readPointer.cachedStatement = program.end();
    readPointer.itemOffset = -1;
    readPointer.limit = 0;

    while (executionPointer.lineNum != kShutdown)
    {
        inErrorCondition = false;
        // If line number is kCommandLine, the command line is being processed, otherwise the program is running. kShutdown
        // will trigger the system shutdown.
        if (executionPointer.lineNum > kCommandLine)
        {
            // A program line may have a few commands, check if we need to proceed to the next line or there is something left still.
            tStatement& statement = executionPointer.cachedStatement->second;
            if (executionPointer.offset >= statement.size())
            {
                if (++executionPointer.cachedStatement != program.end())
                {
                    executionPointer.lineNum = executionPointer.cachedStatement->first;
                    executionPointer.offset = 0;
                }
                else
                {
                    // Reaching the actual end of the program should be identical to the END statement
                    ExecuteEnd(nullptr);
                }
            }
            else
                ExecuteAtPC();
        }
        else
        {
            // If current command line is empty, get a new one and start executing. Note that there is a possibility of multiple statements
            // on the command line, this is supported as well.
            if (commandLine.empty())
            {
                string inLine = GetUserInput();
                const char* ptr = inLine.c_str();
                auto input = ParseCommandLine(ptr);
                if (inErrorCondition)
                {
                    puts(inLine.c_str());
                    for (int i = ptr - inLine.c_str(); i > 0; --i)
                        putchar(' ');
                    putchar('^'); putchar('\n');
                    continue;
                }

                suppressPrompt = false;

                if (input.first > kCommandLine)
                {
                    if (input.second.empty())
                        program.erase(input.first);
                    else
                        program[input.first] = move(input.second);

                    if (readPointer.lineNum == input.first)
                    {
                        readPointer.lineNum = kCommandLine;
                        readPointer.offset = 0;
                        readPointer.cachedStatement = program.begin();
                        readPointer.itemOffset = -1;
                        readPointer.limit = 0;
                    }
                    suppressPrompt = true; // Don't show "Ok" after every line when typing in the program code
                }
                else
                {
                    commandLine = move(input.second);
                    executionPointer.offset = 0;
                    executionPointer.skipForNext = false;
                }
            }
            else
            {
                if(executionPointer.offset < commandLine.size())
                    ExecuteAtPC();
                else
                {
                    commandLine.clear();
                    executionPointer.offset = 0;
                    executionPointer.skipForNext = false;
                }
            }
        }
    }
}

int main()
{
    BasicMachine basicMachine;

    basicMachine.Init();
    basicMachine.Run();
    puts("Bye!");

    return 1;
}
