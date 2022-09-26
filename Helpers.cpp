#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>

#include "Basic.h"

void BasicMachine::ErrorCondition(const char* description)
{
    // Do not produce multiple error messages on the same line
    if (inErrorCondition)
        return;

    if (executionPointer.lineNum > kCommandLine)
        printf("%s on line %d\n", description, executionPointer.lineNum);
    else
        puts(description);

    executionPointer.lineNum = kCommandLine;
    executionPointer.offset = 0;
    executionPointer.cachedStatement = program.end();
    executionPointer.skipForNext = false;
    commandLine.clear();
    stack.clear();

    inErrorCondition = true;
}


void BasicMachine::AdvanceExecutionPointer()
{
    const byte* lengthPtr = executionPointer.lineNum > kCommandLine ? &executionPointer.cachedStatement->second[executionPointer.offset + 1] : &commandLine[executionPointer.offset + 1];
    executionPointer.offset += DecodeParmsLength(lengthPtr) + 1 + SizeOfParmsLength();
}

void BasicMachine::IgnoreSpaces(const char*& ptr)
{
    while (*ptr && (*ptr == ' ' || *ptr == '\t'))
        ++ptr;
}

BasicMachine::tLineNumber BasicMachine::GetLineNum(const char*& ptr)
{
    IgnoreSpaces(ptr);

    short lineNum = 0;
    if (isdigit(*ptr))
    {
        while (*ptr && isdigit(*ptr))
        {
            if (lineNum > 32767 / 10)
            {
                return -1;
            }

            lineNum = lineNum * 10 + (*ptr - '0');
            ++ptr;
        }
        return lineNum;
    }
    return kCommandLine;
}

bool BasicMachine::TryParseLineNum(tStatement& s, const char*& ptr)
{
    IgnoreSpaces(ptr);
    if (isdigit(*ptr))
    {
        EncodeLineNum(s, GetLineNum(ptr));
        return true;
    }
    return false;
}

bool BasicMachine::TryParseString(tStatement& s, const char*& ptr)
{
    IgnoreSpaces(ptr);
    if (*ptr && *ptr == '"')
    {
        s.push_back((byte)TokenType::ttString);
        size_t off = ReserveParmsLength(s);
        ++ptr;
        while (*ptr)
        {
            if (*ptr == '"')
            {
                ++ptr;
                break;
            }
            else
            {
                s.push_back((byte)*ptr++);
            }
        }
        return EncodeParmsLength(s, off);
    }
    else
        return false;
}

bool BasicMachine::TryParseWord(tStatement& s, const char*& ptr)
{
    IgnoreSpaces(ptr);
    if (*ptr)
    {
        s.push_back((byte)TokenType::ttString);
        size_t off = ReserveParmsLength(s);
        while (*ptr && *ptr != ' ')
        {
            s.push_back((byte)*ptr++);
        }
        return EncodeParmsLength(s, off);
    }
    else
        return false;
}

bool BasicMachine::TryParseNumber(tStatement& s, const char*& ptr)
{
    IgnoreSpaces(ptr);
    if (*ptr && (isdigit(*ptr) || ((*ptr == '-' || *ptr == '.') && isdigit(*(ptr + 1)))))
    {
        int sign = 1;
        int mantissa = 0;
        int impl_exponent = 0;
        int exponent = 0;
        int signexp = 1;
        if (*ptr == '-')
        {
            sign = -1;
            ++ptr;
        }
        while (isdigit(*ptr))
        {
            if (mantissa > INT_MAX / 10)
                return false;
            mantissa = mantissa * 10 + (*ptr++ - '0');
        }
        if (*ptr == '.')
        {
            ++ptr;
            while (isdigit(*ptr))
            {
                if (mantissa > INT_MAX / 10)
                    return false;
                mantissa = mantissa * 10 + (*ptr++ - '0');
                --impl_exponent;
            }
        }
        if (toupper(*ptr) == 'E')
        {
            ++ptr;
            if (*ptr == '-')
            {
                signexp = -1;
                ++ptr;
            }
            while (isdigit(*ptr))
            {
                if (exponent > INT_MAX / 10)
                    return false;
                exponent = exponent * 10 + (*ptr++ - '0');
            }
        }

        s.push_back((byte)TokenType::ttNumber);

        float fract = (float)(mantissa * pow(10, signexp * exponent + impl_exponent)) * sign;
        for (int i = 0; i < 4; ++i)
            s.push_back(*((byte*)&fract + i));
        return true;
    }
    return false;
}

void BasicMachine::DecodeString(string& s, const byte*& parms)
{
    ++parms; // skip token type
    int len = (int)*parms++;
    for (int i = 0; i < len; ++i)
        s += (char)*parms++;
}

void BasicMachine::DecodeStringQuoted(string& s, const byte*& parms)
{
    s += '"';
    DecodeString(s, parms);
    s += '"';
}


void BasicMachine::DecodeNumber(string& s, const byte*& parms)
{
    char buf[20];
    sprintf(buf, "%g", DecodeNumber(parms));
    s += buf;
}

float BasicMachine::DecodeNumber(const byte*& parms)
{
    ++parms;
    float res = *(float*)parms;
    parms += 4;
    return res;
}

void BasicMachine::EncodeLineNum(tStatement& s, tLineNumber num)
{
    s.push_back((byte)(num & 255));
    s.push_back((byte)(num >> 8));
}

BasicMachine::tLineNumber BasicMachine::DecodeLineNum(const byte*& parms)
{
    short result = *(short*)parms;
    parms += 2;
    return result;
}

void BasicMachine::DecodeLineNum(string& s, const byte*& parms)
{
    char buf[10];
    sprintf(buf, "%hd", *(short*)(parms));
    s += buf;
    parms += 2;
}


BasicMachine::TokenType BasicMachine::TryParseSymbol(tStatement& s, const char*& ptr, const tUserFunctionInfo* context)
{
    IgnoreSpaces(ptr);
    if (isalpha(*ptr))
    {
        string symbol;
        while (isalnum(*ptr) || *ptr == '$')
        {
            char c = toupper(*ptr++);
            symbol.push_back(c);
            if (c == '$')
                break;
            // Special case - some interpreters allowed space after FN
            if (symbol.length() == 2 && symbol.compare("FN") == 0)
                IgnoreSpaces(ptr);
        }

        if (IsNextSymbolKeep(ptr, '('))
        {
            if (symbol.size() > 2 && symbol[0] == 'F' && symbol[1] == 'N' && isalnum(symbol[2]))
            {
                int index;
                auto itU = find_if(userFunctions.begin(), userFunctions.end(), [&symbol](const auto& e) { return symbol.compare(e.name) == 0; });
                if (itU == userFunctions.end())
                {
                    index = userFunctions.size();
                    if (index >= 256)
                    {
                        ErrorCondition("Too many user functions");
                        return TokenType::ttNone;
                    }

                    userFunctions.push_back({ symbol });
                }
                else
                    index = itU - userFunctions.begin();
                s.push_back((byte)TokenType::ttUserFunction);
                s.push_back((byte)index);
                return TokenType::ttUserFunction;
            }

            auto itF = find_if(functionInfo.begin(), functionInfo.end(), [&symbol](const auto& e) { return symbol.compare(e.name) == 0; });
            if (itF != functionInfo.end())
            {
                s.push_back((byte)TokenType::ttFunction);
                s.push_back((byte)(itF - functionInfo.begin()));
                return TokenType::ttFunction;
            }

            s.push_back((byte)TokenType::ttArray);
            int index;
            auto itA = find_if(arrays.begin(), arrays.end(), [&symbol](const auto& e) { return symbol.compare(e.name) == 0; });
            if (itA == arrays.end())
            {
                index = arrays.size();
                if (index >= 256)
                {
                    ErrorCondition("Too many arrays");
                    return TokenType::ttNone;
                }
                arrays.push_back({ symbol });
                ArrayDefaultCreate((byte)index);
            }
            else
                index = itA - arrays.begin();
            s.push_back((byte)index);
            return TokenType::ttArray;
        }
        else
        {
            auto itS = find_if(systemVarInfo.begin(), systemVarInfo.end(), [&symbol](const auto& e) { return symbol.compare(e.name) == 0; });
            if (itS != systemVarInfo.end())
            {
                s.push_back((byte)TokenType::ttSystemVar);
                s.push_back((byte)(itS - systemVarInfo.begin()));
                return TokenType::ttSystemVar;
            }

            if (context)
            {
                auto itP = find_if(context->parms.begin(), context->parms.end(), [&symbol](const auto& e) { return symbol.compare(e.name) == 0; });
                if (itP != context->parms.end())
                {
                    s.push_back((byte)TokenType::ttParameterRef);
                    s.push_back((byte)(itP - context->parms.begin()));
                    return TokenType::ttParameterRef;
                }
            }

            s.push_back((byte)TokenType::ttVariable);
            int index;
            auto itV = find_if(vars.begin(), vars.end(), [&symbol](const auto& e) { return symbol.compare(e.name) == 0; });
            if (itV == vars.end())
            {
                index = vars.size();
                if (index >= 65536) // Very unlikely...
                {
                    ErrorCondition("Too many variables");
                    return TokenType::ttNone;
                }
                if (symbol.back() == '$')
                    vars.push_back({ symbol, string() });
                else
                    vars.push_back({ symbol, float(0.0) });
            }
            else
                index = itV - vars.begin();
            s.push_back((byte)(index & 0xff));
            s.push_back((byte)(index >> 8));
            return TokenType::ttVariable;
        }
    }
    return TokenType::ttNone;
}

bool BasicMachine::TryParseParameter(tStatement& s, const char*& ptr, tUserFunctionInfo& context)
{
    IgnoreSpaces(ptr);
    if (isalpha(*ptr))
    {
        string symbol;
        while (isalnum(*ptr) || *ptr == '$')
        {
            char c = toupper(*ptr++);
            symbol.push_back(c);
            if (c == '$')
                break;
        }

        // Note that parameters are tokenized as strings
        s.push_back((byte)TokenType::ttParameter);
        s.push_back((byte)symbol.size());
        for (auto c : symbol)
            s.push_back((byte)c);

        if (symbol.back() == '$')
            context.parms.push_back({ symbol, string() });
        else
            context.parms.push_back({ symbol, float(0.0) });
        return true;
    }

    return false;
}

void BasicMachine::DecodeVariable(string& s, const byte*& parms) const
{
    s += vars[DecodeVariable(parms)].name;
}

int BasicMachine::DecodeVariable(const byte*& parms) const
{
    if (*parms++ != (byte)TokenType::ttVariable)
        return -1;
    unsigned short index = (unsigned short)*parms++;
    index += (unsigned short)*parms++ << 8;
    return index;
}

// Pay attention here: DecodeParameter actually updates the context!
void BasicMachine::DecodeParameter(string& s, const byte*& parms, tUserFunctionInfo& context) const
{
    ++parms; // skip token type
    string symbol;
    int len = (int)*parms++;
    for (int i = 0; i < len; ++i)
        symbol += (char)*parms++;
    s += symbol;

    if (symbol.back() == '$')
        context.parms.push_back({ symbol, string() });
    else
        context.parms.push_back({ symbol, float(0.0) });
}

void BasicMachine::DecodeParameterRef(string& s, const byte*& parms, const tUserFunctionInfo& context) const
{
    s += context.parms[DecodeParameterRef(parms, context)].name;
}

int BasicMachine::DecodeParameterRef(const byte*& parms, const tUserFunctionInfo& context) const
{
    if (*parms++ != (byte)TokenType::ttParameterRef)
        return -1;
    return (int)*parms++;
}

void BasicMachine::DecodeArray(string& s, const byte*& parms) const
{
    s += arrays[DecodeArray(parms)].name;
}

int BasicMachine::DecodeArray(const byte*& parms) const
{
    if (*parms++ != (byte)TokenType::ttArray)
        return -1;
    return (int)*parms++;
}

void BasicMachine::DecodeSysVar(string& s, const byte*& parms)
{
    s += systemVarInfo[DecodeSysVar(parms)].name;
}

int BasicMachine::DecodeSysVar(const byte*& parms)
{
    if (*parms++ != (byte)TokenType::ttSystemVar)
        return -1;
    return (int)*parms++;
}

void BasicMachine::DecodeUserFunction(string& s, const byte*& parms) const
{
    s += userFunctions[DecodeUserFunction(parms)].name;
}

int BasicMachine::DecodeUserFunction(const byte*& parms) const
{
    if (*parms++ != (byte)TokenType::ttUserFunction)
        return -1;
    return (int)*parms++;
}

bool BasicMachine::Match(const char*& ptr, const char* pattern)
{
    const char* testptr = ptr;
    const char* testpat = pattern;

    while (toupper(*testptr++) == *testpat++)
        if (*testpat == 0)
        {
            ptr = testptr;
            return true;
        }

    return false;
}

bool BasicMachine::MatchWithSpaces(const char*& ptr, const char* pattern)
{
    const char* testptr = ptr;
    const char* testpat = pattern;

    while (*testptr)
    {
        while (isspace(*testptr)) ++testptr;
        if (toupper(*testptr) == *testpat)
        {
            ++testptr; ++testpat;
            if (*testpat == 0)
            {
                ptr = testptr;
                return true;
            }
        }
        else
            break;
    }
    return false;
}

bool BasicMachine::TestMatch(const char* ptr, const char* pattern)
{
    const char* testptr = ptr;
    const char* testpat = pattern;

    while (toupper(*testptr++) == *testpat++)
        if (*testpat == 0)
            return true;

    return false;
}

bool BasicMachine::IsNextSymbolKeep(const char*& ptr, char symbol)
{
    IgnoreSpaces(ptr);
    return *ptr == symbol;
}

bool BasicMachine::IsNextSymbolDrop(const char*& ptr, char symbol)
{
    if (IsNextSymbolKeep(ptr, symbol))
    {
        ++ptr;
        return true;
    }
    return false;
}

BasicMachine::TokenType BasicMachine::GetNextTokenType(const byte* parms)
{
    return (TokenType)*parms;
}



const char* BasicMachine::ParmsToName(const byte* parms)
{
    return instructionInfo[(int)parms[-1]].name;
}

size_t BasicMachine::ReserveParmsLength(tStatement& s)
{
    size_t result{ s.size() };
    s.push_back((byte)0);
    return result;
}

bool BasicMachine::EncodeParmsLength(tStatement& s, size_t where)
{
    if (s.size() - where - 1 <= 255)
    {
        s[where] = (byte)(s.size() - where - 1);
        return true;
    }
    return false;
}

int BasicMachine::DecodeParmsLength(const byte*& parms)
{
    return (int)*parms++;
}

int BasicMachine::SizeOfParmsLength()
{
    return 1;
}

string BasicMachine::ListStatement(tLineNumber lineNum, const tStatement& statement)
{
    string result;
    char buffer[10];
    if (lineNum > kCommandLine)
    {
        result += _itoa(lineNum, buffer, 10);
        result += ' ';
    }

    size_t offset = 0;
    int counter = 0;
    bool prevSuppressColonAfter = false;
    while (offset < statement.size())
    {
        bool suppressColonBefore = instructionInfo[(int)statement[offset]].suppressColonBefore;
        if (counter++)
        {
            if (suppressColonBefore || prevSuppressColonAfter)
                result += ' ';
            else
                result += ':';
        }
        result += instructionInfo[(int)statement[offset]].do_list(*this, &statement[offset + 1]);
        prevSuppressColonAfter = instructionInfo[(int)statement[offset]].suppressColonAfter;
        const byte* lengthPtr = &statement[offset + 1];
        offset += DecodeParmsLength(lengthPtr) + 1 + SizeOfParmsLength();
    }

    return result;
}

char BasicMachine::TestKeyboard()
{
    if (executionPointer.lineNum == kCommandLine)
    {
        lastKey = 0;
        return 0;
    }

    // Do actual keyboard poll every few statements, otherwise short loops with INKEY$ will not work well
    static int counter = 0;
    if (counter)
    {
        --counter;
        return 0;
    }
    counter = 10;

    if (_kbhit())
        lastKey = (char)_getch();
    else
        lastKey = 0;

    return lastKey;
}
