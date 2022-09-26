#include "Basic.h"

bool BasicMachine::EndOfExpression(const char*& ptr)
{
    // An expression may end in a few ways: end of the line, closing brace (end of a nested expression), or one of the keywords (including colon)
    IgnoreSpaces(ptr);
    if (*ptr == 0 || IsNextSymbolDrop(ptr, ')'))
        return true;
    for (const auto& keyword : instructionInfo)
        if (TestMatch(ptr, keyword.name))
            return true;

    return false;
}

BasicMachine::TokenType BasicMachine::TryParseNextToken(tStatement& s, const char*& ptr, const tUserFunctionInfo* context)
{
    if (EndOfExpression(ptr))
        return TokenType::ttNone;

    // Braces are treated as nested expressions
    if (IsNextSymbolDrop(ptr, '('))
    {
        if (TryParseExpression(s, ptr, context))
            return TokenType::ttExpression;
        else
            return TokenType::ttNone;
    }

    if (TryParseOperation(s, ptr)) // this has to be before TryParseNumber as it can distinguish between unary and binary minus
        return TokenType::ttOp;

    if (TryParseNumber(s, ptr))
        return TokenType::ttNumber;

    if (TryParseString(s, ptr))
        return TokenType::ttString;

    return TryParseSymbol(s, ptr, context);
}

bool BasicMachine::TryParseOperation(tStatement& s, const char*& ptr)
{
    IgnoreSpaces(ptr);
    auto it = find_if(operatorInfo.begin(), operatorInfo.end(), [&ptr](const auto& e) { return MatchWithSpaces(ptr, e.name); });
    if (it != operatorInfo.end())
    {
        s.push_back((byte)TokenType::ttOp);
        s.push_back((byte)(it - operatorInfo.begin()));
        return true;
    }
    return false;
}

void BasicMachine::DecodeToken(string& s, const byte*& parms, const tUserFunctionInfo* context) const
{
    switch (GetNextTokenType(parms))
    {
    case TokenType::ttNumber:   DecodeNumber(s, parms); return;
    case TokenType::ttString:   DecodeStringQuoted(s, parms); return;
    case TokenType::ttOp:       DecodeOperation(s, parms); return;
    case TokenType::ttVariable: DecodeVariable(s, parms); return;
    case TokenType::ttArray:    DecodeArray(s, parms); return;
    case TokenType::ttSystemVar: DecodeSysVar(s, parms); return;
    case TokenType::ttFunction: DecodeFunction(s, parms); return;
    case TokenType::ttUserFunction: DecodeUserFunction(s, parms); return;
    case TokenType::ttExpression:  s += '('; DecodeExpression(s, parms);  s += ')'; return;
    case TokenType::ttParameterRef: DecodeParameterRef(s, parms, *context); return;
    }
}

void BasicMachine::DecodeOperation(string& s, const byte*& parms)
{
    const char* op = operatorInfo[DecodeOperation(parms)].name;
    if (isalpha(*op))
        s += ' ';
    s += op;
    if (isalpha(*op))
        s += ' ';
}

int BasicMachine::DecodeOperation(const byte*& parms)
{
    if (*parms++ != (byte)TokenType::ttOp)
        return -1;
    return (int)*parms++;
}

void BasicMachine::DecodeFunction(string& s, const byte*& parms)
{
    s += functionInfo[DecodeFunction(parms)].name;
}

int BasicMachine::DecodeFunction(const byte*& parms)
{
    if (*parms++ != (byte)TokenType::ttFunction)
        return -1;
    return (int)*parms++;
}

// Note that parsing will not validate the expression in any way - that will be done on execution.
bool BasicMachine::TryParseExpression(tStatement& s, const char*& ptr, const tUserFunctionInfo* context)
{
    size_t restoreSize = s.size();
    s.push_back((byte)TokenType::ttExpression);
    int off = ReserveParmsLength(s);

    TokenType prevToken;
    if ((prevToken = TryParseNextToken(s, ptr, context)) == TokenType::ttNone) // there should be at least one token
    {
        s.resize(restoreSize);
        return false;
    }

    bool valid = true;
    if (prevToken == TokenType::ttOp)
    {
        // If the first token is an operator, it must be an unary one
        if (!operatorInfo[(int)s.back()].unary && !operatorInfo[(int)s.back()].unaryNext)
            valid = false;
    }

    TokenType curToken;
    while (valid && (curToken = TryParseNextToken(s, ptr, context)) != TokenType::ttNone)
    {
        // Catching the most basic syntax errors in expressions:
        // - an operator cannot follow another operator unless it is unary
        // - anything can immediately follow a string (thanks MS for PRINT)
        // - string can follow anything (ditto)
        // - expression can follow anything but a number or another expression
        // - in all other cases a non-operator should be followed by an operator

        if (prevToken == TokenType::ttOp && curToken == TokenType::ttOp)
        {
            if (!operatorInfo[(int)s.back()].unary && !operatorInfo[(int)s.back()].unaryNext)
                valid = false;
        }
        else if (prevToken == TokenType::ttString || curToken == TokenType::ttString)
        {
        }
        else if (curToken == TokenType::ttExpression && prevToken != TokenType::ttExpression && prevToken != TokenType::ttNumber)
        {
        }
        else if (prevToken != TokenType::ttOp && curToken != TokenType::ttOp)
        {
            valid = false;
        }
        prevToken = curToken;
    }
    if (!valid || (prevToken == TokenType::ttOp && !operatorInfo[(int)s.back()].separator)) // An expression cannot end with an operator either
    {
        ErrorCondition("Syntax error");
        return false;
    }
    if (!EncodeParmsLength(s, off))
    {
        ErrorCondition("The expression is too complex");
        return false;
    }
    return true;
}

void BasicMachine::DecodeExpression(string& s, const byte*& parms, const tUserFunctionInfo* context) const
{
    ++parms; // skip token type
    int length = DecodeParmsLength(parms);
    const byte* limit = parms + length;
    while (parms < limit)
        DecodeToken(s, parms, context);
}

BasicMachine::tValue BasicMachine::EvaluateNumber(const byte*& parms)
{
    return DecodeNumber(parms);
}

BasicMachine::tValue BasicMachine::EvaluateString(const byte*& parms)
{
    string val;
    DecodeString(val, parms);
    return val;
}

BasicMachine::tValue BasicMachine::EvaluateVariable (const byte*& parms, const byte* limit)
{
    return vars[DecodeVariable(parms)].value;
}

BasicMachine::tValue BasicMachine::EvaluateParameterRef(const byte*& parms, const byte* limit, const tUserFunctionInfo* context)
{
    return context->parms[DecodeParameterRef(parms, *context)].value;
}

BasicMachine::tValue BasicMachine::EvaluateArray(const byte*& parms, const byte* limit, const tUserFunctionInfo* context)
{
    int index = DecodeArray(parms);
    return ArrayGet((byte)index, EvaluateExpression(parms, context));
}

BasicMachine::tValue BasicMachine::EvaluateSysVar(const byte*& parms, const byte* limit)
{
    int varCode = DecodeSysVar(parms);
    if (systemVarInfo[varCode].do_eval != nullptr)
            return systemVarInfo[varCode].do_eval(*this);
    return tValue();
}

BasicMachine::tValue BasicMachine::EvaluateFunction(const byte*& parms, const byte* limit, const tUserFunctionInfo* context)
{
    int functionCode = DecodeFunction(parms);
    if (parms < limit)
    {
        auto arg = EvaluateExpression(parms, context);

        if (functionInfo[functionCode].do_eval != nullptr)
            return functionInfo[functionCode].do_eval(*this, arg);
    }
    return tValue();
}

BasicMachine::tValue BasicMachine::EvaluateUserFunction(const byte*& parms, const byte* limit, const tUserFunctionInfo* parentContext)
{
    auto& context = userFunctions[DecodeUserFunction(parms)];

    if (context.body.empty())
    {
        ErrorCondition("Undefined user function");
        return tValue();
    }

    // Note - the FN call needs to be evaluated in current context but the result will essentially become the downstream context for
    // the function call itself
    auto args = EvaluateExpression(parms, parentContext);

    auto store = context.parms; // Keep previous values

    if (args.size() == 2*context.parms.size()-1)
    {
        for (size_t i = 0; i < context.parms.size(); ++i)
        {
            if (context.parms[i].value.index() != args[2 * i].index() ||
                (i > 0 && !holds_alternative<tSeparator>(args[2*i-1])))
            {
                ErrorCondition("Bad argument type in user function");
                return tValue();
            }

            context.parms[i].value = args[2 * i];
        }

        const byte* body = context.body.data();
        auto result = EvaluateExpression(body, &context);
        context.parms = store;
        if (result.size() != 1)
            ErrorCondition("Bad expression in user function");
        else
            return result[0];
    }
    else
    {
        ErrorCondition("Bad number of parameters");
    }
    return tValue();
}

BasicMachine::tValue BasicMachine::EvaluateSubexpression(const byte*& parms, const tUserFunctionInfo* context) // An expression in braces; cannot be composite
{
    auto val = EvaluateExpression(parms, context);
    if (val.size() == 1)
        return val[0];
    else
    {
        ErrorCondition("Malformed expression");
        return tValue();
    }
}

BasicMachine::tExpressionValue BasicMachine::EvaluateExpression(const byte*& parms, const tUserFunctionInfo* context)
{
    // Shunting Yard algorithm-lite - no need to care about parentheses or functions
    ++parms;
    int length = DecodeParmsLength(parms);
    const byte* limit = parms + length;

    tExpressionValue result;

    vector<int> opStack;

    TokenType lastTokenType = TokenType::ttNone;

    while (parms < limit)
    {
        TokenType currentTokenType = GetNextTokenType(parms);
        switch (currentTokenType)
        {
        case TokenType::ttNumber: result.push_back(EvaluateNumber(parms)); break;
        case TokenType::ttString: result.push_back(EvaluateString(parms)); break;
        case TokenType::ttExpression: result.push_back(EvaluateSubexpression(parms, context)); break;
        case TokenType::ttVariable: result.push_back(EvaluateVariable(parms, limit)); break;
        case TokenType::ttArray: result.push_back(EvaluateArray(parms, limit, context)); break;
        case TokenType::ttSystemVar: result.push_back(EvaluateSysVar(parms, limit)); break;
        case TokenType::ttFunction: result.push_back(EvaluateFunction(parms, limit, context)); break;
        case TokenType::ttUserFunction: result.push_back(EvaluateUserFunction(parms, limit, context)); break;
        case TokenType::ttParameterRef: result.push_back(EvaluateParameterRef(parms, limit, context)); break;

        case TokenType::ttOp:
            {
                // Not testing for leading operator as the parser will catch that
                int op = DecodeOperation(parms);

                // Convert binary +/- into unary
                if (lastTokenType == TokenType::ttOp || lastTokenType == TokenType::ttNone)
                {
                    if (operatorInfo[op].unaryNext)
                        ++op;
                }

                if (operatorInfo[op].separator) // comma or semicolon complete the current component of the expression
                {
                    while (!opStack.empty())
                    {
                        ComputeOperator(result, opStack.back());
                        opStack.pop_back();
                    }
                    ComputeOperator(result, op);
                    break;
                }

                if (operatorInfo[op].rightAssociative)
                {
                    while (!opStack.empty() && operatorInfo[opStack.back()].precedence > operatorInfo[op].precedence)
                    {
                        ComputeOperator(result, opStack.back());
                        opStack.pop_back();
                    }
                }
                else
                {
                    while (!opStack.empty() && operatorInfo[opStack.back()].precedence >= operatorInfo[op].precedence)
                    {
                        ComputeOperator(result, opStack.back());
                        opStack.pop_back();
                    }
                }
                opStack.push_back(op);
            }
            break;
        default:
            ErrorCondition("Bad expression");
            return result;
        }
        lastTokenType = currentTokenType;
    }

    // Not checking trailing operator as the parser will catch that
    while (!opStack.empty())
    {
        ComputeOperator(result, opStack.back());
        opStack.pop_back();
    }

    for (const auto& v : result)
        if (holds_alternative<tError>(v))
        {
            const char* message = get<tError>(v).message;
            if (message == nullptr)
                ErrorCondition("Bad expression");
            else
                ErrorCondition(message);
        }

    return result;
}

void BasicMachine::ComputeComma(tExpressionValue& val) const
{
    val.push_back(tSeparator(','));
}

void BasicMachine::ComputeSemicolon(tExpressionValue& val) const
{
    val.push_back(tSeparator(';'));
}

void BasicMachine::ComputeAdd(tExpressionValue& val) const
{
    if (val.size() > 1 && val.back().index() == val[val.size() - 2].index())
    {
        if (holds_alternative<float>(val.back()))
        {
            float b = get<float>(val.back());
            val.pop_back();
            float a = get<float>(val.back());
            val.pop_back();
            val.push_back(a + b);
            return;
        }
        else if (holds_alternative<string>(val.back()))
        {
            string b = get<string>(val.back());
            val.pop_back();
            string a = get<string>(val.back());
            val.pop_back();
            val.push_back(a + b);
            return;
        }
    }
    val.push_back(tError());
}

void BasicMachine::ComputeSubtract(tExpressionValue& val) const
{
    float a, b;
    if (PrepareMath(val, a, b))
        val.push_back(a - b);
    else
        val.push_back(tError());
}

void BasicMachine::ComputeMultiply(tExpressionValue& val) const
{
    float a, b;
    if (PrepareMath(val, a, b))
        val.push_back(a * b);
    else
        val.push_back(tError());
}

void BasicMachine::ComputeDivide(tExpressionValue& val) const
{
    float a, b;
    if (PrepareMath(val, a, b))
    {
        if (b == 0.0)
        {
            val.push_back(tError("Division by zero"));
            return;
        }

        val.push_back(a / b);
    }
}

void BasicMachine::ComputePower(tExpressionValue& val) const
{
    float a, b;
    if (PrepareMath(val, a, b))
        val.push_back(pow(a, b));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeLessOrEqual(tExpressionValue& val) const
{
    auto res = ComputeCompare(val);
    if (res.second)
        val.push_back((float)(res.first <= 0));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeGreaterOrEqual(tExpressionValue& val) const
{
    auto res = ComputeCompare(val);
    if (res.second)
        val.push_back((float)(res.first >= 0));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeNotEqual(tExpressionValue& val) const
{
    auto res = ComputeCompare(val);
    if (res.second)
        val.push_back((float)(res.first != 0));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeLess(tExpressionValue& val) const
{
    auto res = ComputeCompare(val);
    if (res.second)
        val.push_back((float)(res.first < 0));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeGreater(tExpressionValue& val) const
{
    auto res = ComputeCompare(val);
    if (res.second)
        val.push_back((float)(res.first > 0));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeEqual(tExpressionValue& val) const
{
    auto res = ComputeCompare(val);
    if (res.second)
        val.push_back((float)(res.first == 0));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeAnd(tExpressionValue& val) const
{
    bool a, b;
    if (PrepareLogical(val, a, b))
        val.push_back((float)(a && b));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeOr(tExpressionValue& val) const
{
    bool a, b;
    if (PrepareLogical(val, a, b))
        val.push_back((float)(a || b));
    else
        val.push_back(tError());
}

void BasicMachine::ComputeNot(tExpressionValue& val) const
{
    if (val.size() > 0)
    {
        if (holds_alternative<float>(val.back()))
        {
            auto a = get<float>(val.back());
            val.pop_back();
            val.push_back((float)(a == 0.0f));
        }
        else if (holds_alternative<string>(val.back()))
        {
            auto a = get<string>(val.back());
            val.pop_back();
            val.push_back((float(a.size() == 0)));
        }
        else
            val.push_back(tError());
    }
    else
    {
        val.push_back(tError());
    }
}

void BasicMachine::ComputeUnaryPlus(tExpressionValue& val) const
{
    if (val.size() == 0)
        val.push_back(tError());
}

void BasicMachine::ComputeUnaryMinus(tExpressionValue& val) const
{
    if (val.size() > 0 && holds_alternative<float>(val.back()))
    {
        auto a = get<float>(val.back());
        val.pop_back();
        val.push_back(-a);
    }
    else
    {
        val.push_back(tError());
    }
}

pair<int, bool> BasicMachine::ComputeCompare(tExpressionValue& val) const
{
    if (val.size() > 1 && val.back().index() == val[val.size() - 2].index())
    {
        if (holds_alternative<float>(val.back()))
        {
            float b = get<float>(val.back());
            val.pop_back();
            float a = get<float>(val.back());
            val.pop_back();
            return { (a < b) ? -1 : (a > b) ? 1 : 0, true };
        }
        else if (holds_alternative<string>(val.back()))
        {
            string b = get<string>(val.back());
            val.pop_back();
            string a = get<string>(val.back());
            val.pop_back();
            return { a.compare(b), true };
        }
    }
    return { 0, false };
}

bool BasicMachine::PrepareMath(tExpressionValue& val, float& a, float& b) const
{
    if (val.size() > 1 && holds_alternative<float>(val.back()) && holds_alternative<float>(val[val.size() - 2]))
    {
        b = get<float>(val.back());
        val.pop_back();
        a = get<float>(val.back());
        val.pop_back();
        return true;
    }
    else
        return false;
}

bool BasicMachine::PrepareLogical(tExpressionValue& val, bool& a, bool& b) const
{
    if (val.size() > 1)
    {
        if (holds_alternative<float>(val.back()))
            b = get<float>(val.back()) != 0.0f;
        else if (holds_alternative<string>(val.back()))
            b = get<string>(val.back()).length() > 0;
        else
            return false;
        val.pop_back();
        if (holds_alternative<float>(val.back()))
            a = get<float>(val.back()) != 0.0f;
        else if (holds_alternative<string>(val.back()))
            a = get<string>(val.back()).length() > 0;
        else
            return false;
        val.pop_back();
        return true;
    }
    return false;
}

void BasicMachine::ComputeOperator(tExpressionValue& val, int code) const
{
    if (operatorInfo[code].do_eval != nullptr)
        operatorInfo[code].do_eval(*this, val);
}

