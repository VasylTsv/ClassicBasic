#define _CRT_SECURE_NO_WARNINGS
#include "Basic.h"

BasicMachine::tValue BasicMachine::ComputeABS(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return (float)fabs(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeASC(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<string>(arg[0]))
        return (float)get<string>(arg[0])[0];
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeATN(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return atanf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeCHR(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
    {
        string result{ (char)get<float>(arg[0]) };
        return result;
    }
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeCOS(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return cosf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeEXP(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return expf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeINT(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return floorf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeLEFT(const tExpressionValue& arg) const
{
    if (arg.size() == 3 && holds_alternative<string>(arg[0]) && holds_alternative<float>(arg[2]))
    {
        int len = get<string>(arg[0]).length();
        return get<string>(arg[0]).substr(0, min((int)get<float>(arg[2]), len));
    }
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeLEN(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<string>(arg[0]))
        return (float)get<string>(arg[0]).length();
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeLOG(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return logf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeMID(const tExpressionValue& arg) const
{
    bool valid = (arg.size() == 3 || arg.size() == 5) && holds_alternative<string>(arg[0]) && holds_alternative<float>(arg[2]);

    if (valid)
    {
        int len = get<string>(arg[0]).length();
        int from = min(len, (int)get<float>(arg[2])) - 1;

        int count = len - from;

        if (arg.size() == 5)
        {
            valid = valid && holds_alternative<float>(arg[4]);
            if(valid)
                count = min(len - from, (int)get<float>(arg[4]));
        }

        if(valid)
            return get<string>(arg[0]).substr(from, count);
    }

    return tError();
}

BasicMachine::tValue BasicMachine::ComputeRND(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return ((float)rand())/RAND_MAX;
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeRIGHT(const tExpressionValue& arg) const
{
    if (arg.size() == 3 && holds_alternative<string>(arg[0]) && holds_alternative<float>(arg[2]))
    {
        int len = get<string>(arg[0]).length();
        return get<string>(arg[0]).substr(max(0, len-(int)get<float>(arg[2])), string::npos);
    }
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeSGN(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return get<float>(arg[0]) < 0 ? (float)-1 : (float)1;
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeSIN(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return sinf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeSQR(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return sqrtf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeSTR(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
    {
        char buf[30];
        sprintf(buf, "%g", get<float>(arg[0]));
        return string(buf);
    }
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeTAB(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
    {
        return tTab((int)get<float>(arg[0]));
    }
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeTAN(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<float>(arg[0]))
        return tanf(get<float>(arg[0]));
    else
        return tError();
}

BasicMachine::tValue BasicMachine::ComputeVAL(const tExpressionValue& arg) const
{
    if (arg.size() == 1 && holds_alternative<string>(arg[0]))
        return (float)atof(get<string>(arg[0]).c_str());
    else
        return tError();
}


