#define _CRT_SECURE_NO_WARNINGS
#include "Basic.h"
#include <time.h>

int BasicMachine::ExpressionToIndex(byte ar, const tExpressionValue& val)
{
    const auto& arInfo = arrays[(int)ar];
    if (val.size() != arInfo.dimensions.size() * 2 - 1)
        return -1;
    int index = 0;
    for (size_t i = 0; i < arInfo.dimensions.size(); ++i)
    {
        if (!holds_alternative<float>(val[2 * i]) || (i > 0 && !holds_alternative<tSeparator>(val[2 * i - 1])))
            return -1;
        if ((int)get<float>(val[2 * i]) > arInfo.dimensions[i])
            return -1;
        index = index * arInfo.dimensions[i] + (int)get<float>(val[2 * i]);
    }
    return index;
}

void BasicMachine::ArrayDefaultCreate(byte ar)
{
    tExpressionValue dims;
    dims.push_back(float(10.0));
    ArrayCreate(ar, dims);
}

bool BasicMachine::ArrayCreate(byte ar, const tExpressionValue& dims)
{
    tArrayInfo& ai = arrays[(int)ar];
    if ((dims.size() & 1) == 0)
        return false;

    ai.dimensions.clear();

    int size = 1;
    for (size_t i = 0; i < dims.size(); i += 2)
    {
        if (!holds_alternative<float>(dims[i]) || (i > 0 && !holds_alternative<tSeparator>(dims[i - 1])))
            return false;
        ai.dimensions.push_back((int)(get<float>(dims[i])) + 1);
        size *= ai.dimensions.back();
    }
    ai.value.clear();
    if (ai.name.back() == '$')
        ai.value.resize(size, string());
    else
        ai.value.resize(size, float(0.0));
    return true;
}

const BasicMachine::tValue& BasicMachine::ArrayGet(byte ar, const tExpressionValue& index)
{
    int i = ExpressionToIndex(ar, index);
    if (i >= 0)
        return arrays[(int)ar].value[i];
    ErrorCondition("Bad array index");
    return arrays[0].value[0];
}

bool BasicMachine::ArraySet(byte ar, const tExpressionValue& index, const tValue& val)
{
    int i = ExpressionToIndex(ar, index);
    if (i >= 0)
    {
        if (arrays[(int)ar].value[i].index() == val.index())
        {
            arrays[(int)ar].value[i] = val;
            return true;
        }
        else
            ErrorCondition("Bad value type");
    }
    else
        ErrorCondition("Bad array index");
    return false;
}


void BasicMachine::ResetVars()
{
    for (auto& v : vars)
        if (holds_alternative<float>(v.value))
            v.value = float(0.0);
        else
            v.value = string();

    for (size_t i = 0; i < arrays.size(); ++i)
        ArrayDefaultCreate((byte)i);

    for (auto& u : userFunctions)
        u.body.clear();
}

bool BasicMachine::GetNextDataItem(tValue& val)
{
    if (!ScanForNextDataItem())
    {
        ErrorCondition("No DATA available");
        return false;
    }

    const byte* dataPtr = readPointer.cachedStatement->second.data() + readPointer.offset + readPointer.itemOffset;
    const byte* dataNow = dataPtr;
    if (GetNextTokenType(dataPtr) == TokenType::ttNumber)
        val = EvaluateNumber(dataPtr);
    else
        val = EvaluateString(dataPtr);
    readPointer.itemOffset += (dataPtr - dataNow);

    return true;
}

bool BasicMachine::ScanForNextDataItem()
{
    // Possible conditions at this point:
    // itemOffset == -1 - this is the first call to READ after RESTORE, need to find the first item
    // cachedStatement is at the program.end() - no more data
    // itemOffset >= limit - reached the end of the statement, scan for the next one

    if (readPointer.cachedStatement == program.end())
        return false;

    if (readPointer.itemOffset == -1 || readPointer.itemOffset >= readPointer.limit)
    {
        // Scan from the first statement on this line or from the next one
        if (readPointer.itemOffset == -1)
            readPointer.offset = 0;
        else
        {
            const byte* lengthPtr = &readPointer.cachedStatement->second[readPointer.offset + 1];
            readPointer.offset += DecodeParmsLength(lengthPtr) + 1 + SizeOfParmsLength();
        }

        while(readPointer.cachedStatement != program.end())
        {
            if (readPointer.offset >= readPointer.cachedStatement->second.size())
            {
                // Reached the end of the line, skip to next
                if (++readPointer.cachedStatement == program.end())
                    break;
                readPointer.lineNum = readPointer.cachedStatement->first;
                readPointer.offset = 0;
            }
            else
            {
                if (instructionInfo[(int)readPointer.cachedStatement->second[readPointer.offset]].dataStatement)
                {
                    // If this is a DATA statement
                    readPointer.itemOffset = 1 + SizeOfParmsLength(); // skipping instruction code and length
                    const byte* lengthPtr = &readPointer.cachedStatement->second[readPointer.offset + 1];
                    readPointer.limit = DecodeParmsLength(lengthPtr) + 1 + SizeOfParmsLength();
                    break;
                }
                else
                {
                    // Skip to the next statement on this line
                    const byte* lengthPtr = &readPointer.cachedStatement->second[readPointer.offset + 1];
                    readPointer.offset += DecodeParmsLength(lengthPtr) + 1 + SizeOfParmsLength();
                }
            }
        }
    }

    return readPointer.cachedStatement != program.end();
}

const BasicMachine::tValue& BasicMachine::GetVarInkey()
{
    static tValue val;
    if (lastKey)
        val = move(string(1, lastKey));
    else
        val = string();
    return val;
}

const BasicMachine::tValue& BasicMachine::GetVarTime()
{
    // Many contemporary systems did not have persistent clock, so TIME$ generally returned the uptime.
    // For the same reason DATE$ was not commonly available
    static tValue val;
    time_t timer;
    time(&timer);
    tm* timeinfo = localtime(&timer);
    char buffer[10];
    sprintf(buffer, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    val = buffer;
    return val;
}

bool BasicMachine::SetProtectedVar(const tValue& val)
{
    ErrorCondition("Cannot set protected variable");
    return false;
}