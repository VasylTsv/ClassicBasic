#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>

#include "Basic.h"

#include <chrono>
#include <thread>

// Note that right now neither Execute nor List do any data validation, assuming that Parse is always producing valid data.

// BYE
void BasicMachine::ExecuteBye(const byte* parms)
{
    // This will immediately terminate the execution loop
    executionPointer.lineNum = kShutdown;
    stack.clear();
    program.clear();
    commandLine.clear();
}

// CLS
void BasicMachine::ExecuteCls(const byte* parms)
{
    // This is a weird trick that just recently started working on Windows.
    printf("\033c");
}

// DATA {number|string}[,...]
bool BasicMachine::ParseData(tStatement& result, const char*& ptr)
{
    bool valid = true;
    do
    {
        valid = TryParseString(result, ptr) || TryParseNumber(result, ptr);
    } while (valid && IsNextSymbolDrop(ptr, ','));

    return valid;
}

void BasicMachine::ExecuteData(const byte* parms)
{
    // Executing DATA has no effect
}

string BasicMachine::ListData(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    result += ' ';
    int len = DecodeParmsLength(parms);
    const byte* limit = parms + len;
    int count = 0;
    while (parms < limit)
    {
        if(count++)
            result += ',';

        if (GetNextTokenType(parms) == TokenType::ttNumber)
            DecodeNumber(result, parms);
        else
            DecodeStringQuoted(result, parms);
    }
    return result;
}

// DEF FNname(name[,...])=expression
bool BasicMachine::ParseDef(tStatement& result, const char*& ptr)
{
    IgnoreSpaces(ptr);
    bool valid = TestMatch(ptr, "FN") && TryParseSymbol(result, ptr) == TokenType::ttUserFunction && IsNextSymbolDrop(ptr, '(');

    // Fun part: creating a temporary user function info block. The real one is only created on execution.
    // Only the parameters part is needed in that temporary context
    tUserFunctionInfo context;

    do
    {
        valid = valid && TryParseParameter(result, ptr, context);

    } while (valid && IsNextSymbolDrop(ptr, ','));

    valid = valid && IsNextSymbolDrop(ptr, ')') && IsNextSymbolDrop(ptr, '=') && TryParseExpression(result, ptr, &context);

    return valid;
}

void BasicMachine::ExecuteDef(const byte* parms)
{
    (void)DecodeParmsLength(parms);
    int ufIndex = DecodeUserFunction(parms);
    userFunctions[ufIndex].parms.clear();
    while (GetNextTokenType(parms) == TokenType::ttParameter)
    {
        string symbol;
        DecodeParameter(symbol, parms, userFunctions[ufIndex]);
    }
    const byte* e = parms;
    ++e;
    int exprLength = DecodeParmsLength(e) + 1 + SizeOfParmsLength();
    userFunctions[ufIndex].body.clear();
    userFunctions[ufIndex].body.insert(userFunctions[ufIndex].body.end(), parms, parms + exprLength);;
}

string BasicMachine::ListDef(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    result += ' ';
    (void)DecodeParmsLength(parms);
    DecodeUserFunction(result, parms);
    tUserFunctionInfo context; // Temporary context so the expression will be decoded with correct parameters
    result += '(';
    int count = 0;
    while (GetNextTokenType(parms) == TokenType::ttParameter)
    {
        if (count++)
            result += ',';
        DecodeParameter(result, parms, context);
    }
    result += ")=";
    DecodeExpression(result, parms, &context);
    return result;
}

// DIM {name(ushort[,...])}[,...]
// This dialect supports a very significant syntax extension: the array dimensions are expressions. It is
// actually easier to do it that way in this architecture. It also does not put any limitations on number of
// dimensions (besides the maximum line length and expression complexity).
bool BasicMachine::ParseDim(tStatement& result, const char*& ptr)
{
    IgnoreSpaces(ptr);

    bool valid = true;
    while (valid)
    {
        auto tt = TryParseSymbol(result, ptr);
        if (tt == TokenType::ttArray)
        {
            valid = valid && IsNextSymbolDrop(ptr, '(');
            valid = valid && TryParseExpression(result, ptr);
        }
        else
            valid = tt == TokenType::ttVariable;
        if (valid && !IsNextSymbolDrop(ptr, ','))
            break;
    }

    return valid;
}

void BasicMachine::ExecuteDim(const byte* parms)
{
    int len = DecodeParmsLength(parms);
    const byte* limit = parms + len;
    while (!inErrorCondition && parms < limit)
    {
        if (GetNextTokenType(parms) == TokenType::ttArray)
        {
            int arIndex = DecodeArray(parms);
            auto val = EvaluateExpression(parms);
            ArrayCreate((byte)arIndex, val);
        }
        else
            DecodeVariable(parms);
        // TODO: Need to figure out the exact semantics of DIM A
    }
}

string BasicMachine::ListDim(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    result += ' ';
    int len = DecodeParmsLength(parms);
    const byte* limit = parms + len;
    int count = 0;
    while (parms < limit)
    {
        if (count++)
            result += ',';
        if(GetNextTokenType(parms) == TokenType::ttArray)
        {
            DecodeArray(result, parms);
            result += '(';
            DecodeExpression(result, parms);
            result += ')';
        }
        else
        {
            DecodeVariable(result, parms);
        }
    }
    return result;
}

// ELSE
bool BasicMachine::ParseElse(tStatement& result, const char*& ptr)
{
    if (!parseIfStack.empty())
    {
        *(size_t*)&result[parseIfStack.back()] = result.size(); // points at the next instruction
        parseIfStack.pop_back();
        return true;
    }
    return false;
}

void BasicMachine::ExecuteElse(const byte* parms)
{
    (void)DecodeParmsLength(parms);
    if (executionPointer.offset < executionPointer.cachedStatement->second.size())
    {
        // Skipping over IF and other statements differ
        if (instructionInfo[(int)*parms].ifStatement)
        {
            ++parms;
            (void)DecodeParmsLength(parms);
            size_t offsetElse = *(size_t*)parms;
            if (offsetElse == 0)
                executionPointer.offset = executionPointer.cachedStatement->second.size(); // IF with no ELSE, just skip to the end of the statement
            else
                executionPointer.offset = offsetElse - 2; // Pass to ELSE in that IF so a chained ELSE IF will also work
        }
        else
        {
            ++parms;
            int length = DecodeParmsLength(parms);
            executionPointer.offset += length + 2; // TODO: hardcoded opcode/length size
        }
    }
}

// END
void BasicMachine::ExecuteEnd(const byte* parms)
{
    executionPointer.lineNum = kCommandLine;
    executionPointer.offset = 0;
    executionPointer.cachedStatement = program.end();
    executionPointer.skipForNext = false;

    readPointer.lineNum = kCommandLine;
    readPointer.offset = 0;
    readPointer.cachedStatement = program.end();
    readPointer.itemOffset = -1;
    readPointer.limit = 0;

    commandLine.clear();
    stack.clear();
    loopStack.clear();
}

// FOR name=expression TO expression [STEP expression]
bool BasicMachine::ParseFor(tStatement& result, const char*& ptr)
{
    bool valid = TryParseSymbol(result, ptr) == TokenType::ttVariable && IsNextSymbolDrop(ptr, '=') && TryParseExpression(result, ptr) &&
        Match(ptr, "TO") && TryParseExpression(result, ptr);

    if(valid)
    {
        if (Match(ptr, "STEP"))
            valid = valid && TryParseExpression(result, ptr);
        else
            result.push_back((byte)TokenType::ttNone);
    }

    return valid;
}

void BasicMachine::ExecuteFor(const byte* parms)
{
    (void)DecodeParmsLength(parms);
    unsigned short index = (unsigned short)DecodeVariable(parms);
    auto initVal = EvaluateExpression(parms);
    if (initVal.size() == 1 && holds_alternative<float>(initVal[0]))
    {
        vars[index].value = initVal[0];
        float initial = get<float>(initVal[0]);
        float limit = get<float>(EvaluateExpression(parms)[0]);
        float step = GetNextTokenType(parms) == TokenType::ttNone ? (float)1.0 : get<float>(EvaluateExpression(parms)[0]);
        if (bAnsiFor && (initial - limit) * step > 0)
            executionPointer.skipForNext = true;
        loopStack.push_back({ index, limit, step, executionPointer });
    }
    else
        ErrorCondition("Malformed FOR loop");
}

string BasicMachine::ListFor(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    (void)DecodeParmsLength(parms);
    result += ' ';
    DecodeVariable(result, parms);
    result += '=';
    DecodeExpression(result, parms);
    result += " TO ";
    DecodeExpression(result, parms);
    if (GetNextTokenType(parms) != TokenType::ttNone)
    {
        result += " STEP ";
        DecodeExpression(result, parms);
    }

    return result;
}

// GOTO ushort. This dialect supports a shortcut - standalone ushort is interpreted as GOTO in program only
bool BasicMachine::ParseGoto(tStatement& result, const char*& ptr)
{
    return TryParseLineNum(result, ptr);
}


void BasicMachine::ExecuteGoto(const byte* parms)
{
    (void)DecodeParmsLength(parms);
    executionPointer.lineNum = DecodeLineNum(parms);
    executionPointer.offset = 0;
    executionPointer.cachedStatement = program.find(executionPointer.lineNum);
    if (executionPointer.cachedStatement == program.end())
        ErrorCondition("GOTO - line not found");
}

string BasicMachine::ListGoto(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    if(!result.empty())
        result += ' ';
    (void)DecodeParmsLength(parms);
    DecodeLineNum(result, parms);
    return result;
}

// GOSUB ushort
bool BasicMachine::ParseGosub(tStatement& result, const char*& ptr)
{
    return TryParseLineNum(result, ptr);
}

void BasicMachine::ExecuteGosub(const byte* parms)
{
    stack.push_back(executionPointer);

    (void)DecodeParmsLength(parms);
    executionPointer.lineNum = DecodeLineNum(parms);
    executionPointer.offset = 0;
    executionPointer.cachedStatement = program.find(executionPointer.lineNum);
    if (executionPointer.cachedStatement == program.end())
        ErrorCondition("GOSUB - line not found");
}

string BasicMachine::ListGosub(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    result += ' ';
    (void)DecodeParmsLength(parms);
    DecodeLineNum(result, parms);
    return result;
}

// IF expression {THEN|GOTO}. THEN is dropped, GOTO is not
bool BasicMachine::ParseIf(tStatement& result, const char*& ptr)
{
    // Start with reference to ELSE - it may be used by the statement skipping code
    parseIfStack.push_back(result.size());
    result.insert(result.end(), sizeof(executionPointer.offset), (byte)0);

    bool valid = TryParseExpression(result, ptr);

    if (valid)
    {
        if (Match(ptr, "THEN"))
            result.push_back((byte)0);
        else if (TestMatch(ptr, "GOTO"))
            result.push_back((byte)1);
        else
            valid = false;
    }

    return valid;
}

void BasicMachine::ExecuteIf(const byte* parms)
{
    (void)DecodeParmsLength(parms);
    size_t offsetElse = *(size_t*)parms;
    parms += sizeof(executionPointer.offset);

    auto val = EvaluateExpression(parms);
    ++parms; // Skip "THEN/GOTO" tag
    if (val.size() == 1)
    {
        bool cond = false;
        if (holds_alternative<float>(val[0]))
            cond = get<float>(val[0]) != 0.0f;
        else if (holds_alternative<string>(val[0]))
            cond = get<string>(val[0]).length() > 0;
        else
            ErrorCondition("Bad IF expression");

        if (!cond)
        {
            if (offsetElse)
                executionPointer.offset = offsetElse;
            else
                executionPointer.offset = SIZE_MAX;
        }
    }
    else
        ErrorCondition("Bad IF expression");
}

string BasicMachine::ListIf(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    result += ' ';
    int len = DecodeParmsLength(parms);
    parms += sizeof(executionPointer.offset); // Skip over ELSE offset
    DecodeExpression(result, parms);
    if(*parms == (byte)0)
        result += " THEN";
    return result;
}

// INPUT [string{,|;}]lvalue[,...]
bool BasicMachine::ParseInput(tStatement& result, const char*& ptr)
{
    bool valid = true;
    if (TryParseString(result, ptr))
    {
        if (IsNextSymbolDrop(ptr, ','))
            result.push_back((byte)0);
        else if (IsNextSymbolDrop(ptr, ';'))
            result.push_back((byte)1);
        else
            valid = false;
    }

    if (valid)
    {
        do
        {
            auto t = TryParseSymbol(result, ptr);
            valid = valid && (t == TokenType::ttVariable || t == TokenType::ttArray);
            if (valid && t == TokenType::ttArray && IsNextSymbolDrop(ptr, '('))
                valid = valid && TryParseExpression(result, ptr);
        } while (valid && IsNextSymbolDrop(ptr, ','));
    }

    return valid;
}

void BasicMachine::ExecuteInput(const byte* parms)
{
    printPos = 0;

    int len = DecodeParmsLength(parms);
    const byte* limit = parms + len;

    bool query = true;

    while (query)
    {
        query = false;

        if (GetNextTokenType(parms) == TokenType::ttString)
        {
            string prompt;
            DecodeString(prompt, parms);
            printf(prompt.c_str());

            if (*parms++ == (byte)1)
                putchar('?');
        }
        else
            putchar('?');

        char buffer[256];
        gets_s(buffer, 256);
        const char* ptr = buffer;

        vector<string> items;
        items.push_back("");
        bool inQuotedString = false;
        while (char c = *ptr++)
        {
            // Trying to handle comma separated input with possibly quoted strings
            if (c == '"')
            {
                if (items.back().empty() && !inQuotedString)
                    inQuotedString = true;
                else if (*ptr == '"') // Doubled quote mark is interpreted as a quote
                {
                    ++ptr;
                    items.back().push_back(c);
                }
                else if (inQuotedString)
                    inQuotedString = false;
            }
            else if (c == ',' && !inQuotedString)
                items.push_back("");
            else
                items.back().push_back(c);
        }

        size_t index = 0;
        while (parms < limit)
        {
            if (index >= items.size())
            {
                puts("?Redo from start"); // Actual text from MS BASIC
                query = true;
                break;
            }

            string name;
            if (GetNextTokenType(parms) == TokenType::ttArray)
            {
                int arIndex = DecodeArray(parms);
                if (holds_alternative<string>(arrays[arIndex].value[0]))
                    ArraySet((byte)arIndex, EvaluateExpression(parms), items[index]);
                else
                    ArraySet((byte)arIndex, EvaluateExpression(parms), (float)atof(items[index].c_str()));
            }
            else
            {
                int varIndex = DecodeVariable(parms);

                if (holds_alternative<float>(vars[varIndex].value))
                    vars[varIndex].value = (float)atof(items[index].c_str());
                else
                    vars[varIndex].value = items[index];
            }
            ++index;
        }
    }
}

string BasicMachine::ListInput(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    result += ' ';
    int len = DecodeParmsLength(parms);
    const byte* limit = parms + len;
    if(GetNextTokenType(parms) == TokenType::ttString)
    {
        DecodeStringQuoted(result, parms);
        if (*parms++ == (byte)0)
            result += ',';
        else
            result += ';';
    }

    int count = 0;
    while (parms < limit)
    {
        if (count++)
            result += ',';
        if (GetNextTokenType(parms) == TokenType::ttArray)
        {
            DecodeArray(result, parms);
            result += '(';
            DecodeExpression(result, parms);
            result += ')';
        }
        else
        {
            DecodeVariable(result, parms);
        }
    }
    return result;
}

// LET lvalue=expression
bool BasicMachine::ParseLet(tStatement& result, const char*& ptr)
{
    IgnoreSpaces(ptr);
    for (const auto& i : instructionInfo)
    {
        if (TestMatch(ptr, i.name))
        {
            ErrorCondition("Variable name cannot start with a keyword");
            return false;
        }
    }

    bool valid = true;
    auto t = TryParseSymbol(result, ptr);
    if (t == TokenType::ttVariable || t == TokenType::ttArray)
    {
        if (t == TokenType::ttArray && IsNextSymbolDrop(ptr, '(')) // array indexes follow, this will parse them as an expression until the closing brace
        {
            valid = valid && TryParseExpression(result, ptr);
        }

        if (IsNextSymbolDrop(ptr, '='))
        {
            valid = valid && TryParseExpression(result, ptr);
        }
        else
            valid = false;;
    }
    else
        valid = false;

    return valid;
}

void BasicMachine::ExecuteLet(const byte* parms)
{
    (void)DecodeParmsLength(parms);

    string name;
    if (GetNextTokenType(parms) == TokenType::ttArray)
    {
        int arIndex = DecodeArray(parms);
        tExpressionValue index = EvaluateExpression(parms);
        tExpressionValue val = EvaluateExpression(parms);
        if (val.size() == 1)
            ArraySet((byte)arIndex, index, val[0]);
        else
            ErrorCondition("Bad assignment value");
    }
    else
    {
        int index = DecodeVariable(parms);

        tExpressionValue val = EvaluateExpression(parms);
        if (val.size() == 1 && vars[index].value.index() == val[0].index())
            vars[index].value = val[0];
        else
            ErrorCondition("Bad assignment value");
    }
}

string BasicMachine::ListLet(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    if(!result.empty())
        result += ' ';
    (void)DecodeParmsLength(parms);

    if (GetNextTokenType(parms) == TokenType::ttArray)
    {
        DecodeArray(result, parms);
        result += '(';
        DecodeExpression(result, parms);
        result += ')';
    }
    else
    {
        DecodeVariable(result, parms);
    }

    result += '=';
    DecodeExpression(result, parms);
    return result;
}

// LIST [ushort[{,,-}ushort]]
// TODO: Incomplete semantics
bool BasicMachine::ParseList(tStatement& result, const char*& ptr)
{
    if(TryParseLineNum(result, ptr))
        if(IsNextSymbolDrop(ptr, ',') || IsNextSymbolDrop(ptr, '-'))
            TryParseLineNum(result, ptr);
    return true;
}

void BasicMachine::ExecuteList(const byte* parms)
{
    short lineFrom = 0;
    short lineTo = 32767;
    int len = DecodeParmsLength(parms);
    if (len)
    {
        lineFrom = DecodeLineNum(parms);
        if (len > 2)
            lineTo = DecodeLineNum(parms);
        else
            lineTo = lineFrom;
    }

    auto from = program.lower_bound(lineFrom);
    auto to = program.upper_bound(lineTo);

    for (auto it = from; it != to; ++it)
    {
        puts(ListStatement(it->first, it->second).c_str());
    }
}

string BasicMachine::ListList(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    int len = DecodeParmsLength(parms);
    if (len)
    {
        DecodeLineNum(result, parms);
        if (len > 2)
            DecodeLineNum(result, parms);
    }
    return result;
}

// LOAD string. In this dialect the string does not have to be quoted
bool BasicMachine::ParseLoad(tStatement& result, const char*& ptr)
{
    return TryParseString(result, ptr) || TryParseWord(result, ptr);
}

void BasicMachine::ExecuteLoad(const byte* parms)
{
    (void)DecodeParmsLength(parms);

    string fname;
    DecodeString(fname, parms);

    FILE* fin = fopen(fname.c_str(), "rt");
    if (fin)
    {
        ExecuteNew(parms);
        while (!feof(fin))
        {
            char buff[257];
            buff[0] = 0;
            fgets(buff, 257, fin);
            if (buff[strlen(buff) - 1] == '\n')
                buff[strlen(buff) - 1] = 0;

            if (strlen(buff) == 0)
                continue;

            const char* ptr = buff;
            auto parsed = ParseCommandLine(ptr);
            
            if (inErrorCondition)
            {
                puts(buff);
                for (int i = ptr - buff; i > 0; --i)
                    putchar(' ');
                putchar('^'); putchar('\n');
                break;
            }

            if (parsed.first > kCommandLine)
                program[parsed.first] = parsed.second;
            else
            {
                ErrorCondition("Invalid line in the source file");
                break;
            }
        }

        fclose(fin);
    }
    else
    {
        ErrorCondition("Cannot open file to LOAD");
    }
}

string BasicMachine::ListLoad(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    (void)DecodeParmsLength(parms);
    result += ' ';
    DecodeStringQuoted(result, parms);
    return result;
}

// NEW
void BasicMachine::ExecuteNew(const byte* parms)
{
    if (executionPointer.lineNum != kCommandLine)
        ExecuteEnd(nullptr);

    program.clear();
    stack.clear();
    loopStack.clear();
    userFunctions.clear();
    vars.clear();
    arrays.clear();
}

// NEXT [name[,...]]
bool BasicMachine::ParseNext(tStatement& result, const char*& ptr)
{
    auto t = TryParseSymbol(result, ptr);
    if (t != TokenType::ttVariable && t != TokenType::ttNone)
        return false;
    while (IsNextSymbolDrop(ptr, ','))
        if (TryParseSymbol(result, ptr) != TokenType::ttVariable)
            return false;
    return true;
}

void BasicMachine::ExecuteNext(const byte* parms)
{
    if (!loopStack.empty())
    {
        const byte* instrPtr = parms - 1; // Size of the instruction code

        int len = DecodeParmsLength(parms);
        const byte* limit = parms + len;

        do
        {
            int index;
            if (parms < limit)
                index = DecodeVariable(parms);
            else
                index = get<0>(loopStack.back());

            // If we are scanning for the appropriate NEXT
            if (executionPointer.skipForNext)
            {
                if (index == get<0>(loopStack.back()))
                {
                    executionPointer.skipForNext = false;
                    loopStack.pop_back();
                }
                return;
            }

            while (!loopStack.empty() && index != get<0>(loopStack.back()))
                loopStack.pop_back();

            if (!loopStack.empty())
            {
                float val = get<float>(vars[index].value);
                float limit = get<1>(loopStack.back());
                float step = get<2>(loopStack.back());
                val += step;
                vars[index].value = val;
                if ((val - limit) * step <= 0)
                {
                    const auto& newExecutionPointer = get<3>(loopStack.back());

                    // Detect a delay loop, one without any operators
                    short oldLineNum = executionPointer.lineNum;
                    size_t oldLineOffset = executionPointer.offset - (parms - instrPtr);

                    if (newExecutionPointer.lineNum == oldLineNum && newExecutionPointer.offset == oldLineOffset)
                    {
                        // Loop without a body, must be a delay loop, no other reason for that in code
                        int loops = step == 0 ? 1 : (int)((limit - val + step) / step);
                        if (loops > 0)
                        {
                            this_thread::sleep_for(chrono::milliseconds(loops));
                            loopStack.pop_back();
                            vars[index].value = val + loops * step;
                        }
                    }
                    else
                    {
                        // Go back to the beginning of the loop
                        executionPointer = newExecutionPointer;
                        return;
                    }
                }
                else
                    loopStack.pop_back();
            }
            else
            {
                ErrorCondition("NEXT without FOR");
                break;
            }

        } while (parms < limit);
    }
    else
        ErrorCondition("NEXT without FOR");
}

string BasicMachine::ListNext(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    int length = DecodeParmsLength(parms);
    const byte* limit = parms + length;
    if (length)
    {
        result += ' ';
        DecodeVariable(result, parms);
        while (parms < limit)
        {
            result += ',';
            DecodeVariable(result, parms);
        }
    }
    return result;
}

// ON expression GOTO|GOSUB ushort[,...]
bool BasicMachine::ParseOn(tStatement& result, const char*& ptr)
{
    bool valid = TryParseExpression(result, ptr);
    if (valid)
    {
        IgnoreSpaces(ptr);
        if (Match(ptr, "GOTO"))
            result.push_back((byte)0);
        else if (Match(ptr, "GOSUB"))
            result.push_back((byte)1);
        else
            valid = false;

        valid = valid && TryParseLineNum(result, ptr);
        while (valid && IsNextSymbolDrop(ptr, ','))
            valid = valid && TryParseLineNum(result, ptr);
    }
    return valid;
}

void BasicMachine::ExecuteOn(const byte* parms)
{
    int length = DecodeParmsLength(parms);
    const byte* limit = parms + length;
    auto val = EvaluateExpression(parms);
    if (val.size() == 1 && holds_alternative<float>(val[0]))
    {
        // GOSUB vs GOTO logic differs just by this one line
        if(*parms++ == (byte)1)
            stack.push_back(executionPointer);

        // Get the proper index and make sure there is an entry for it after GOTO/GOSUB
        int index = (int)get<float>(val[0]) - 1;
        if (index >= 0 && index < (limit - parms) / 2)
        {
            for (; index; --index)
                DecodeLineNum(parms);

            // Execute GOTO logic
            executionPointer.lineNum = DecodeLineNum(parms);
            executionPointer.offset = 0;
            executionPointer.cachedStatement = program.find(executionPointer.lineNum);
            if (executionPointer.cachedStatement == program.end())
                ErrorCondition("ON - line not found");
        }
        // The out of range value will cause the execution to continue (some documents refer to error condition on negative values)
    }
    else
        ErrorCondition("Bad expression in ON");
}

string BasicMachine::ListOn(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    int length = DecodeParmsLength(parms);
    const byte* limit = parms + length;
    result += ' ';
    DecodeExpression(result, parms);
    if (*parms++ == (byte)1)
        result += " GOSUB ";
    else
        result += " GOTO ";
    int count = 0;
    while (parms < limit)
    {
        if (count++)
            result += ',';
        DecodeLineNum(result, parms);
    }
    return result;
}

// PRINT [expression[[{,|;|}]expression]...]
bool BasicMachine::ParsePrint(tStatement& result, const char*& ptr)
{
    // As separators are part of the expression, this one becomes very simple
    TryParseExpression(result, ptr);
    return true;
}

void BasicMachine::ExecutePrint(const byte* parms)
{
    if (DecodeParmsLength(parms) > 0)
    {
        auto val{ EvaluateExpression(parms) };

        if (inErrorCondition)
            return;

        bool separated = true;
        int prev = 2;

        string buffer;
        char numBuff[20];

        for (auto v : val)
        {
            if (holds_alternative<tSeparator>(v))
            {
                char c = get<tSeparator>(v).kind;
                if (c == ',')
                {
                    int offset = 8 - (printPos % 8);
                    printPos += offset;
                    for (; offset; --offset)
                        buffer += ' ';
                }
            }
            else
            {
                if (holds_alternative<float>(v))
                {
                    if (get<float>(v) >= 0)
                    {
                        buffer += ' ';
                        ++printPos;
                    }
                    sprintf(numBuff, "%g ", get<float>(v));
                    buffer += numBuff;
                    printPos += strlen(numBuff);
                }
                else if (holds_alternative<string>(v))
                {
                    buffer += get<string>(v);
                    printPos += get<string>(v).length();
                }
                else if (holds_alternative<tTab>(v))
                {
                    printPos %= 80;
                    int offset = get<tTab>(v).offset - printPos;
                    if (offset > 0)
                    {
                        printPos += offset;
                        for (; offset; --offset)
                            buffer += ' ';
                    }
                }
            }
            separated = holds_alternative<tSeparator>(v);
        }

        printf(buffer.c_str());

        // Add semicolon or colon at the end of the argument list to prevent moving to the next line
        if (holds_alternative<tSeparator>(val.back()))
            return;
    }

    printPos = 0;
    puts("");
}

string BasicMachine::ListPrint(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    if (DecodeParmsLength(parms) > 0)
    {
        result += ' ';
        DecodeExpression(result, parms);
    }
    return result;
}

// READ lvalue[,...]
bool BasicMachine::ParseRead(tStatement& result, const char*& ptr)
{
    bool valid = true;
    do
    {
        auto t = TryParseSymbol(result, ptr);
        valid = valid && (t == TokenType::ttVariable || t == TokenType::ttArray);
        if (t == TokenType::ttArray && IsNextSymbolDrop(ptr, '('))
            valid = valid && TryParseExpression(result, ptr);
    } while (valid && IsNextSymbolDrop(ptr, ','));

    return valid;
}

void BasicMachine::ExecuteRead(const byte* parms)
{
    int len = DecodeParmsLength(parms);
    const byte* limit = parms + len;
    tValue val;
    while (parms < limit && GetNextDataItem(val))
    {
        string name;
        if (GetNextTokenType(parms) == TokenType::ttArray)
        {
            int arIndex = DecodeArray(parms);
            ArraySet((byte)arIndex, EvaluateExpression(parms), val);
        }
        else
        {
            int varIndex = DecodeVariable(parms);
 
            if (vars[varIndex].value.index() == val.index())
                vars[varIndex].value = val;
            else
                ErrorCondition("Bad data type");
        }
    }
}

string BasicMachine::ListRead(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    result += ' ';
    int len = DecodeParmsLength(parms);
    const byte* limit = parms + len;
    int count = 0;
    while (parms < limit)
    {
        if (count++)
            result += ',';
        if (GetNextTokenType(parms) == TokenType::ttArray)
        {
            DecodeArray(result, parms);
            result += '(';
            DecodeExpression(result, parms);
            result += ')';
        }
        else
        {
            DecodeVariable(result, parms);
        }
    }
    return result;
}

// REM<remainder of the line>
bool BasicMachine::ParseRem(tStatement& result, const char*& ptr)
{
    int len = strlen(ptr);
    len &= 255;
    for (; len; --len)
        result.push_back((byte)*ptr++);
    return true;
}

string BasicMachine::ListRem(const byte* parms) const
{
    string result{ ParmsToName(parms) }; // Note no space after REM, this is intentional
    int len = DecodeParmsLength(parms);
    for (int i = 0; i < len; ++i)
        result += (char)*parms++;
    return result;
}

// RESTORE [ushort]
bool BasicMachine::ParseRestore(tStatement& result, const char*& ptr)
{
    TryParseLineNum(result, ptr);
    return true;
}

void BasicMachine::ExecuteRestore(const byte* parms)
{
    unsigned short lineNum = kCommandLine;
    if (DecodeParmsLength(parms) > 0)
        lineNum = DecodeLineNum(parms);
    else if (!program.empty())
        lineNum = program.begin()->first;

    readPointer.lineNum = lineNum;
    readPointer.offset = 0;
    readPointer.cachedStatement = program.find(lineNum);
    readPointer.itemOffset = -1;
    readPointer.limit = 0;
    if (readPointer.cachedStatement == program.end())
        ErrorCondition("No DATA for RESTORE");
}

string BasicMachine::ListRestore(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    if (DecodeParmsLength(parms) > 0)
    {
        result += ' ';
        DecodeLineNum(result, parms);
    }
    return result;
}

// RETURN
void BasicMachine::ExecuteReturn(const byte* parms)
{
    if (stack.empty())
        ErrorCondition("Stack underflow");
    else
    {
        executionPointer = stack.back();
        stack.pop_back();
    }
}

// RUN
void BasicMachine::ExecuteRun(const byte* parms)
{
    if (!program.empty())
    {
        executionPointer.cachedStatement = program.begin();
        executionPointer.lineNum = executionPointer.cachedStatement->first;
        executionPointer.offset = 0;
        executionPointer.skipForNext = false;

        readPointer.cachedStatement = executionPointer.cachedStatement;
        readPointer.lineNum = executionPointer.lineNum;
        readPointer.offset = executionPointer.offset;
        readPointer.itemOffset = -1;
        readPointer.limit = 0;

        ResetVars();
        loopStack.clear();
        stack.clear();
    }
}

// SAVE string. In this dialect the string does not have to be quoted
bool BasicMachine::ParseSave(tStatement& result, const char*& ptr)
{
    return TryParseString(result, ptr) || TryParseWord(result, ptr);
}

void BasicMachine::ExecuteSave(const byte* parms)
{
    (void)DecodeParmsLength(parms);

    string fname;
    DecodeString(fname, parms);

    FILE* fout = fopen(fname.c_str(), "wt");

    if (fout)
    {
        for (const auto& line : program)
        {
            fputs(ListStatement(line.first, line.second).c_str(), fout);
            fputs("\n", fout);
        }

        fclose(fout);
    }
    else
        ErrorCondition("Error opening file");
}

string BasicMachine::ListSave(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    (void)DecodeParmsLength(parms);
    result += ' ';
    DecodeStringQuoted(result, parms);
    return result;
}

// RANDOMIZE [expression]
bool BasicMachine::ParseRandomize(tStatement& result, const char*& ptr)
{
    TryParseExpression(result, ptr);
    return true;
}

void BasicMachine::ExecuteRandomize(const byte* parms)
{
    if (DecodeParmsLength(parms) > 0)
    {
        auto val = EvaluateExpression(parms);
        if (val.size() == 1 && holds_alternative<float>(val[0]))
        {
            srand((int)get<float>(val[0]));
            return;
        }
        else
            ErrorCondition("Bad argument for RANDOMIZE");
    }
    else
        srand((int)time(nullptr));
}

string BasicMachine::ListRandomize(const byte* parms) const
{
    string result{ ParmsToName(parms) };
    if (DecodeParmsLength(parms) > 0)
    {
        result += ' ';
        DecodeExpression(result, parms);
    }
    return result;
}


// Generic version for instructions without parameters
bool BasicMachine::ParseNoParms(tStatement& result, const char*& ptr)
{
    return true;
}

string BasicMachine::ListNoParms(const byte* parms) const
{
    return ParmsToName(parms);
}

bool BasicMachine::ParseNotAllowed(tStatement& result, const char*& ptr)
{
    return false;
}

// Generic version for non-executing instructions
void BasicMachine::ExecuteNop(const byte* parms)
{
}

// Extensions
// DUMPVARS
void BasicMachine::ExecuteDumpVars(const byte* parms)
{
    for (const auto& v : vars)
    {
        printf("%s = ", v.name.c_str());
        if (holds_alternative<float>(v.value))
            printf("%g\n", get<float>(v.value));
        else if (holds_alternative<string>(v.value))
            printf("\"%s\"\n", get<string>(v.value).c_str());
        else
            printf("???\n");
    }

    for (const auto& a : arrays)
    {
        string s;
        s += a.name;
        s += '(';
        for (size_t i = 0; i < a.dimensions.size(); ++i)
        {
            if (i > 0)
                s += ',';
            char buff[8];
            s += _itoa(a.dimensions[i]-1, buff, 10);
        }
        s += ')';
        puts(s.c_str());
    }

    for(const auto& f : userFunctions)
    {
        printf("%s", f.name.c_str());
        if (f.parms.size() > 0)
        {
            for (size_t i = 0; i < f.parms.size(); ++i)
                printf("%c%s", i ? ',' : '(', f.parms[i].name.c_str());
        }
        else
            putchar('(');
        printf(")=");
        if (f.body.empty())
            printf("<not set>\n");
        else
        {
            string body;
            const byte* parms = f.body.data();
            DecodeExpression(body, parms, &f);
            printf("%s\n", body.c_str());
        }
    }
}
