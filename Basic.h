// Basic.h
// BASIC Programming Language interpreter by Vasyl Tsvirkunov

#include <cstddef>
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <variant>

using namespace std;

class BasicMachine
{
    // Build configuration (may become runtime options)
    static const bool bAnsiFor = false;

    // Main machine elements
    typedef vector<byte> tStatement;
    typedef short int tLineNumber;
    static const tLineNumber kCommandLine = -1;
    static const tLineNumber kShutdown = -2;
    typedef map<tLineNumber, tStatement> tProgram; // Remember, map is sorted by key
    struct tExecutionPointer
    {
        tLineNumber lineNum;
        size_t offset;
        tProgram::iterator cachedStatement;
        bool skipForNext;
    };
    typedef vector<tExecutionPointer> tStack;

    tExecutionPointer executionPointer;
    tProgram program;
    tStatement commandLine;
    tStack stack;

    // Stack for FOR loop. Each element contains the variable index, limit, step, and execution point for the
    // beginning of the loop (the next command after FOR).
    typedef vector<tuple<unsigned short, float, float, tExecutionPointer>> tLoopStack;
    tLoopStack loopStack;

    // During parsing IF and THEN/ELSE are linked using this stack. Every time IF is parsed, it gets two slots reserved
    // for clauses and offset to those slots is pushed to this stack. THEN marks the first slot on the top of the
    // stack and ELSE marks the second slot. There is no ENDIF in this dialect, otherwise that one would drop the top
    // of the stack. Missing THEN would assume executing the next instruction (IF GOTO construct), missing ELSE should
    // be either end of the line if THEN is present or one instruction in otherwise. It is a bit complex but should
    // implement a valid logic flow with sufficient validation.
    vector<size_t> parseIfStack;

    // Tracking the last parsed line number to allow the continuation
    tLineNumber lastLineNum;

    // PRINT has a peculiar feature - TAB pseudo-function that requires tracking of the horizontal position of the
    // current character.
    int printPos;

    struct tInstructionInfo
    {
        const char* name;
        function<bool(BasicMachine&, tStatement&, const char*&)> do_parse;
        function<void(BasicMachine&, const byte*)> do_execute;
        function<string(const BasicMachine&, const byte*)> do_list;
        bool suppressColonBefore; // This flag is used to suppress colons around THEN and ELSE
        bool suppressColonAfter;
        bool dataStatement; // To distinguish DATA
        bool nextStatement; // To distinguish NEXT (FOR does scan ahead)
        bool ifStatement;   // To distinguish IF (so skip statement can skip over it
    };

    static vector<tInstructionInfo> instructionInfo;

    // Both the program and the command line are tokenized at the parsing stage. Most tokens consist of
    // one byte of the token type and one byte of index in the table. ttVariable has two bytes of index
    // allowing more than 256 variables; string and expression have length encoded as one byte followed
    // by actual contents. ttNone does not have anything besides the tag itself.
    // Some token type have very limited context - e.g., ttParameter can appear only in one place in DEF
    // statement.
    // Potential optimization here would be to collapse the tag and the index in one byte for some types.
    enum class TokenType : int
    {
        ttNone,
        ttNumber,   // generic floating point number
        ttString,   // string constant
        ttOp,       // operation (only in expressions)
        ttVariable, // The following five types are all identified by TryParseSymbol depending on name,
        ttArray,    // presense of FN at the beginning, or following parenthesis.
        ttSystemVar,
        ttFunction,
        ttUserFunction,
        ttExpression,
        ttParameter,
        ttParameterRef
    };

    // Special pseudo-value types.
    // There is a function TAB that can only be used in a context of PRINT
    // and cannot be combined with anything else. It is using a special type for that.
    struct tTab
    {
        int offset;
        explicit tTab(int n) : offset(n) {}
    };

    // Another pseudo-type is separator, currently either comma or semicolon
    struct tSeparator
    {
        char kind;
        explicit tSeparator(char c) : kind(c) {}
    };

    struct tError
    {
        const char* message;
        explicit tError(const char* m = nullptr) : message(m) {}
    };

    // There are two types of values - numbers and strings. Both can exist in one or two-dimensional arrays.
    // An improvement would be to handle numbers as integer and floats separately for performance but
    // in this version only the float type is used.
    // Additionally, expressions evaluate to vectors of values (comma or semicolon separates the subexpressions
    // which may be of different types but to keep the distinction between different separators, those themselves
    // produce values, e.g., "1,3" will end up {1,',',3}). For that tSeparator type is used. Finally, there
    // is tTab which is used only to communicate between TAB and PRINT.
    // There is also a special type tError to signify failed expression calculations.
    typedef variant<float, string, tSeparator, tTab, tError> tValue;
    typedef vector<tValue> tExpressionValue;

    // Variables, arrays, and user functions - three types of dynamic values associated with the BASIC program.
    // The actual elements are allocated at the parsing stage and only cleared with NEW.
    struct tVarInfo
    {
        string name;
        tValue value;
    };
    vector<tVarInfo> vars;

    struct tArrayInfo
    {
        string name;
        vector<short> dimensions;
        vector<tValue> value;
    };
    vector<tArrayInfo> arrays;

    struct tUserFunctionInfo
    {
        string name;
        vector<tVarInfo> parms;
        tStatement body;
    };
    vector<tUserFunctionInfo> userFunctions;

    // Support for arrays
    int ExpressionToIndex(byte ar, const tExpressionValue& val);
    void ArrayDefaultCreate(byte ar);
    bool ArrayCreate(byte ar, const tExpressionValue& dims);
    const tValue& ArrayGet(byte ar, const tExpressionValue& index);
    bool ArraySet(byte ar, const tExpressionValue& index, const tValue& val);

    // Reset all variabled to a default state
    void ResetVars();

    // Support for DATA/READ/RESTORE. It acts as a separate execution pointer scanning for DATA statements but
    // it also needs to keep the position within a statement.
    struct tReadPointer : public tExecutionPointer
    {
        int itemOffset;
        int limit;
    };

    tReadPointer readPointer;

    bool GetNextDataItem(tValue& val);
    bool ScanForNextDataItem();

    // Dispatchers for expression elements - functions, operators, special variables. This way the implementation
    // can be easily extended. These are the static elements of the BASIC language, shared between the machine
    // instances.
    struct tFunctionInfo
    {
        const char* name;
        function<tValue(const BasicMachine&, const tExpressionValue&)> do_eval;
    };

    static vector<tFunctionInfo> functionInfo;

    struct tOperatorInfo
    {
        const char* name;
        function<void(const BasicMachine&, tExpressionValue&)> do_eval;
        int precedence;
        bool rightAssociative;
        bool separator;
        bool unaryNext;
        bool unary;
    };

    static vector<tOperatorInfo> operatorInfo;

    struct tSystemVarInfo
    {
        const char* name;
        function<const tValue&(BasicMachine&)> do_eval;
        function<bool(BasicMachine&, const tValue&)> do_set;
    };

    static vector<tSystemVarInfo> systemVarInfo;

    // Main error handling. Calling it will terminate program execution and/or parsing, returning to the command line.
    bool inErrorCondition;
    void ErrorCondition(const char* description);

    // Called before each statement to check for keyboard interrupt. The value may be retrieved on the next
    // statement by INKEY$. As a bonus, it clears the buffer so INPUT does not get ghost presses
    char lastKey;
    char TestKeyboard();

    static void IgnoreSpaces(const char*& ptr);
    
    // Short unsigned number, used for line numbers and array dimensions. 0 to 32767.
    static tLineNumber GetLineNum(const char*& ptr);
    static void EncodeLineNum(tStatement& s, tLineNumber num);
    static bool TryParseLineNum(tStatement& s, const char*& ptr);
    static short DecodeLineNum(const byte*& parms);
    static void DecodeLineNum(string& s, const byte*& parms);
    // Quoted string (DecodeString produces the string without quotes)
    static bool TryParseString(tStatement& s, const char*& ptr);
    static bool TryParseWord(tStatement& s, const char*& ptr); // Special case to handle unquoted string alternative in some commands
    static void DecodeString(string& s, const byte*& parms);
    static void DecodeStringQuoted(string& s, const byte*& parms);
    // Generic number, float packed in four bytes
    static bool TryParseNumber(tStatement& s, const char*& ptr);
    static void DecodeNumber(string& s, const byte*& parms);
    static float DecodeNumber(const byte*& parms);
    // Symbol, a string valid for a variable name
    TokenType TryParseSymbol(tStatement& s, const char*& ptr, const tUserFunctionInfo* context = nullptr);
    bool TryParseParameter(tStatement& s, const char*& ptr, tUserFunctionInfo& context);
    void DecodeVariable(string& s, const byte*& parms) const;
    int DecodeVariable(const byte*& parms) const;
    void DecodeArray(string& s, const byte*& parms) const;
    int DecodeArray(const byte*& parms) const;
    void DecodeParameter(string& s, const byte*& parms, tUserFunctionInfo& context) const;
    void DecodeParameterRef(string& s, const byte*& parms, const tUserFunctionInfo& context) const;
    int DecodeParameterRef(const byte*& parms, const tUserFunctionInfo& context) const;
    static void DecodeSysVar(string& s, const byte*& parms);
    static int DecodeSysVar(const byte*& parms);
    static void DecodeFunction(string& s, const byte*& parms);
    static int DecodeFunction(const byte*& parms);
    void DecodeUserFunction(string& s, const byte*& parms) const;
    int DecodeUserFunction(const byte*& parms) const;

    // Expressions
    static bool Match(const char*& ptr, const char* pattern);
    static bool MatchWithSpaces(const char*& ptr, const char* pattern); // Ignore whitespace in the input stream, currently used only for operators
    static bool IsNextSymbolKeep(const char*& ptr, char symbol);
    static bool IsNextSymbolDrop(const char*& ptr, char symbol);

    static const char* ParmsToName(const byte* parms);

    static TokenType GetNextTokenType(const byte* parms);

    static size_t ReserveParmsLength(tStatement& s);
    static bool EncodeParmsLength(tStatement& s, size_t where);
    static int DecodeParmsLength(const byte*& parms);
    static int SizeOfParmsLength();

    static bool EndOfExpression(const char*& ptr);
    static bool TestMatch(const char* ptr, const char* pattern);
    TokenType TryParseNextToken(tStatement& s, const char*& ptr, const tUserFunctionInfo* context = nullptr);
    static bool TryParseOperation(tStatement& s, const char*& ptr);
    void DecodeToken(string& s, const byte*& parms, const tUserFunctionInfo* context = nullptr) const; // This should not be called outside of expression
    static void DecodeOperation(string& s, const byte*& parms);
    static int DecodeOperation(const byte*& parms);

    string ListStatement(tLineNumber lineNum, const tStatement& statement);

    bool TryParseExpression(tStatement& s, const char*& ptr, const tUserFunctionInfo* context = nullptr);
    void DecodeExpression(string& s, const byte*& parms, const tUserFunctionInfo* context = nullptr) const;

    static tValue EvaluateNumber(const byte*& parms);
    static tValue EvaluateString(const byte*& parms);
    tValue EvaluateVariable(const byte*& parms, const byte* limit);
    tValue EvaluateArray(const byte*& parms, const byte* limit, const tUserFunctionInfo* context = nullptr);
    tValue EvaluateSysVar(const byte*& parms, const byte* limit);
    tValue EvaluateFunction(const byte*& parms, const byte* limit, const tUserFunctionInfo* context = nullptr);
    tValue EvaluateUserFunction(const byte*& parms, const byte* limit, const tUserFunctionInfo* parentContext = nullptr);
    tValue EvaluateParameterRef(const byte*& parms, const byte* limit, const tUserFunctionInfo* context = nullptr);
    tValue EvaluateSubexpression(const byte*& parms, const tUserFunctionInfo* context = nullptr);
    tExpressionValue EvaluateExpression(const byte*& parms, const tUserFunctionInfo* context = nullptr);

    // User input
    bool suppressPrompt;
    string GetUserInput();
    pair<tLineNumber, tStatement> ParseCommandLine(const char*& ptr);

    // Go to the next statement within the same line
    void AdvanceExecutionPointer();

    // Execute current statement
    void ExecuteAtPC();

    // Instructions. Each instruction needs three functions to be implemented: to parse, to execute, and to list. Parse and
    // list must be consistent enought that output of list passed to parse produces the original data. Parse should validate
    // as much of the syntax as possible. List can make an assumption that the data is correct; Execute can assume the general
    // correctness of data layout but it needs to deal with runtime problem. Also, expressions are not evaluated during parsing
    // so they may fail to parse during execution (this may change in the future).
    void ExecuteBye(const byte* parms);

    void ExecuteCls(const byte* parms);

    bool ParseData(tStatement& result, const char*& ptr);
    void ExecuteData(const byte* parms);
    string ListData(const byte* parms) const;

    bool ParseDef(tStatement& result, const char*& ptr);
    void ExecuteDef(const byte* parms);
    string ListDef(const byte* parms) const;

    bool ParseDim(tStatement& result, const char*& ptr);
    void ExecuteDim(const byte* parms);
    string ListDim(const byte* parms) const;

    bool ParseElse(tStatement& result, const char*& ptr);
    void ExecuteElse(const byte* parms);

    void ExecuteEnd(const byte* parms);

    bool ParseFor(tStatement& result, const char*& ptr);
    void ExecuteFor(const byte* parms);
    string ListFor(const byte* parms) const;

    bool ParseGoto(tStatement& result, const char*& ptr);
    void ExecuteGoto(const byte* parms);
    string ListGoto(const byte* parms) const;

    bool ParseGosub(tStatement& result, const char*& ptr);
    void ExecuteGosub(const byte* parms);
    string ListGosub(const byte* parms) const;

    bool ParseIf(tStatement& result, const char*& ptr);
    void ExecuteIf(const byte* parms);
    string ListIf(const byte* parms) const;

    bool ParseInput(tStatement& result, const char*& ptr);
    void ExecuteInput(const byte* parms);
    string ListInput(const byte* parms) const;

    bool ParseLet(tStatement& result, const char*& ptr);
    void ExecuteLet(const byte* parms);
    string ListLet(const byte* parms) const;

    bool ParseList(tStatement& result, const char*& ptr);
    void ExecuteList(const byte* parms);
    string ListList(const byte* parms) const;

    bool ParseLoad(tStatement& result, const char*& ptr);
    void ExecuteLoad(const byte* parms);
    string ListLoad(const byte* parms) const;

    bool ParseNext(tStatement& result, const char*& ptr);
    void ExecuteNext(const byte* parms);
    string ListNext(const byte* parms) const;

    void ExecuteNew(const byte* parms);

    bool ParseOn(tStatement& result, const char*& ptr);
    void ExecuteOn(const byte* parms);
    string ListOn(const byte* parms) const;

    bool ParsePrint(tStatement& result, const char*& ptr);
    void ExecutePrint(const byte* parms);
    string ListPrint(const byte* parms) const;

    bool ParseRandomize(tStatement& result, const char*& ptr);
    void ExecuteRandomize(const byte* parms);
    string ListRandomize(const byte* parms) const;

    bool ParseRead(tStatement& result, const char*& ptr);
    void ExecuteRead(const byte* parms);
    string ListRead(const byte* parms) const;

    bool ParseRem(tStatement& result, const char*& ptr);
    string ListRem(const byte* parms) const;

    void ExecuteRun(const byte* parms);

    bool ParseRestore(tStatement& result, const char*& ptr);
    void ExecuteRestore(const byte* parms);
    string ListRestore(const byte* parms) const;

    void ExecuteReturn(const byte* parms);
    
    bool ParseSave(tStatement& result, const char*& ptr);
    void ExecuteSave(const byte* parms);
    string ListSave(const byte* parms) const;

    void ExecuteNop(const byte* parms);

    // Many instructions have no parameters, these are generic Parse/List functions
    bool ParseNoParms(tStatement& result, const char*& ptr);
    string ListNoParms(const byte* parms) const;

    // And there are some keywords that are parts of other instructions
    bool ParseNotAllowed(tStatement& result, const char*& ptr);

    // Extensions
    void ExecuteDumpVars(const byte* parms);

    // Functions
    tValue ComputeABS(const tExpressionValue& arg) const;
    tValue ComputeASC(const tExpressionValue& arg) const;
    tValue ComputeATN(const tExpressionValue& arg) const;
    tValue ComputeCHR(const tExpressionValue& arg) const;
    tValue ComputeCOS(const tExpressionValue& arg) const;
    tValue ComputeEXP(const tExpressionValue& arg) const;
    tValue ComputeINT(const tExpressionValue& arg) const;
    tValue ComputeLEFT(const tExpressionValue& arg) const;
    tValue ComputeLEN(const tExpressionValue& arg) const;
    tValue ComputeLOG(const tExpressionValue& arg) const;
    tValue ComputeMID(const tExpressionValue& arg) const;
    tValue ComputeRND(const tExpressionValue& arg) const;
    tValue ComputeRIGHT(const tExpressionValue& arg) const;
    tValue ComputeSGN(const tExpressionValue& arg) const;
    tValue ComputeSIN(const tExpressionValue& arg) const;
    tValue ComputeSQR(const tExpressionValue& arg) const;
    tValue ComputeSTR(const tExpressionValue& arg) const;
    tValue ComputeTAB(const tExpressionValue& arg) const;
    tValue ComputeTAN(const tExpressionValue& arg) const;
    tValue ComputeVAL(const tExpressionValue& arg) const;

    // Operators
    void ComputeComma(tExpressionValue& val) const;
    void ComputeSemicolon(tExpressionValue& val) const;
    void ComputeAdd(tExpressionValue& val) const;
    void ComputeSubtract(tExpressionValue& val) const;
    void ComputeMultiply(tExpressionValue& val) const;
    void ComputeDivide(tExpressionValue& val) const;
    void ComputePower(tExpressionValue& val) const;
    void ComputeLessOrEqual(tExpressionValue& val) const;
    void ComputeGreaterOrEqual(tExpressionValue& val) const;
    void ComputeNotEqual(tExpressionValue& val) const;
    void ComputeLess(tExpressionValue& val) const;
    void ComputeGreater(tExpressionValue& val) const;
    void ComputeEqual(tExpressionValue& val) const;
    void ComputeAnd(tExpressionValue& val) const;
    void ComputeOr(tExpressionValue& val) const;
    void ComputeNot(tExpressionValue& val) const;
    void ComputeUnaryPlus(tExpressionValue& val) const;
    void ComputeUnaryMinus(tExpressionValue& val) const;

    // Operator helpers
    pair<int, bool> ComputeCompare(tExpressionValue& val) const;
    bool PrepareLogical(tExpressionValue& val, bool& a, bool& b) const;
    bool PrepareMath(tExpressionValue& val, float& a, float& b) const;
    void ComputeOperator(tExpressionValue& val, int code) const;

    // System variables
    const tValue& GetVarInkey();
    const tValue& GetVarTime();
    bool SetProtectedVar(const tValue& val);

public:
    void Init();

    // The main system loop
    void Run();
};
