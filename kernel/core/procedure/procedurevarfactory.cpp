/*****************************************************************************
    NumeRe: Framework fuer Numerische Rechnungen
    Copyright (C) 2017  Erik Haenel et al.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/


#include "procedurevarfactory.hpp"
#include "procedure.hpp"
#include "../../kernel.hpp"

#define FLAG_EXPLICIT 1
#define FLAG_INLINE 2

// Constructor
ProcedureVarFactory::ProcedureVarFactory()
{
    init();
}

// General constructor from a procedure instance
ProcedureVarFactory::ProcedureVarFactory(Procedure* _procedure, const string& sProc, unsigned int currentProc)
{
    init();
    _currentProcedure = _procedure;

    sProcName = replaceProcedureName(sProc);
    nth_procedure = currentProc;
}

// Destructor. Will free up all memory
ProcedureVarFactory::~ProcedureVarFactory()
{
    reset();
}

// This member function is the initializer function.
void ProcedureVarFactory::init()
{
    _currentProcedure = nullptr;

    // Get the addresses of the kernel objects
    _parserRef = &NumeReKernel::getInstance()->getParser();
    _dataRef = &NumeReKernel::getInstance()->getData();
    _optionRef = &NumeReKernel::getInstance()->getSettings();
    _functionRef = &NumeReKernel::getInstance()->getDefinitions();
    _outRef = &NumeReKernel::getInstance()->getOutput();
    _pDataRef = &NumeReKernel::getInstance()->getPlottingData();
    _scriptRef = &NumeReKernel::getInstance()->getScript();

    sProcName = "";
    nth_procedure = 0;

    sArgumentMap = nullptr;
    sLocalVars = nullptr;
    sLocalStrings = nullptr;
    sLocalTables = nullptr;

    dLocalVars = nullptr;

    nArgumentMapSize = 0;
    nLocalVarMapSize = 0;
    nLocalStrMapSize = 0;
    nLocalTableSize = 0;
}

// This member function will reset the object, i.e. free
// up the allocated memory. Can be used instead of deleting
// the object, however due to design decisions, this object
// is heap-allocated and recreated for each procedure
void ProcedureVarFactory::reset()
{
    if (nArgumentMapSize)
    {
        for (size_t i = 0; i < nArgumentMapSize; i++)
            delete[] sArgumentMap[i];

        delete[] sArgumentMap;
        sArgumentMap = nullptr;
        nArgumentMapSize = 0;
    }

    if (nLocalVarMapSize)
    {
        for (size_t i = 0; i < nLocalVarMapSize; i++)
        {
            if (_parserRef)
                _parserRef->RemoveVar(sLocalVars[i][1]);

            delete[] sLocalVars[i];
        }

        delete[] sLocalVars;
        delete[] dLocalVars;
        sLocalVars = nullptr;
        dLocalVars = nullptr;
        nLocalVarMapSize = 0;
    }

    if (nLocalStrMapSize)
    {
        for (size_t i = 0; i < nLocalStrMapSize; i++)
        {
            if (_dataRef)
                _dataRef->removeStringVar(sLocalStrings[i][1]);

            delete[] sLocalStrings[i];
        }

        delete[] sLocalStrings;
        sLocalStrings = nullptr;
        nLocalStrMapSize = 0;
    }

    if (nLocalTableSize)
    {
        for (size_t i = 0; i < nLocalTableSize; i++)
        {
            if (_dataRef)
                _dataRef->deleteCache(sLocalTables[i][1]);

            delete[] sLocalTables[i];
        }

        delete[] sLocalTables;
        sLocalTables = nullptr;
        nLocalTableSize = 0;
    }
}

// replaces path characters etc.
string ProcedureVarFactory::replaceProcedureName(string sProcedureName)
{
    for (size_t i = 0; i < sProcedureName.length(); i++)
    {
        if (sProcedureName[i] == ':' || sProcedureName[i] == '\\' || sProcedureName[i] == '/' || sProcedureName[i] == ' ')
            sProcedureName[i] = '_';
    }

    return sProcedureName;
}

// This private member function counts the number of elements
// in the passed string list. This is used to determine the
// size of the to be allocated data object
unsigned int ProcedureVarFactory::countVarListElements(const string& sVarList)
{
    int nParenthesis = 0;
    unsigned int nElements = 1;

    // Count every comma, which is not part of a parenthesis and
    // also not between two quotation marks
    for (unsigned int i = 0; i < sVarList.length(); i++)
    {
        if (sVarList[i] == '(' && !isInQuotes(sVarList, i))
            nParenthesis++;

        if (sVarList[i] == ')' && !isInQuotes(sVarList, i))
            nParenthesis--;

        if (sVarList[i] == ',' && !nParenthesis && !isInQuotes(sVarList, i))
            nElements++;
    }

    return nElements;
}

// This private member function checks, whether the keywords "var",
// "str" or "tab" are used in the current argument and throws and
// exception, if this is the case.
void ProcedureVarFactory::checkKeywordsInArgument(const string& sArgument, const string& sArgumentList, unsigned int nCurrentIndex)
{
    string sCommand = findCommand(sArgument).sString;

    // Was a keyword used as a argument?
    if (sCommand == "var" || sCommand == "tab" || sCommand == "str")
    {
        // Free up memory
        for (unsigned int j = 0; j <= nCurrentIndex; j++)
            delete[] sArgumentMap[j];

        delete[] sArgumentMap;
        nArgumentMapSize = 0;

        NumeReDebugger& _debugger = NumeReKernel::getInstance()->getDebugger();
        // Gather all information in the debugger and throw
        // the exception
        _debugger.gatherInformations(this, sArgumentList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());
        _debugger.throwException(SyntaxError(SyntaxError::WRONG_ARG_NAME, sArgumentList, SyntaxError::invalid_position, sCommand));
    }
}

// This member function will create the procedure arguments for the
// current procedure.
map<string,string> ProcedureVarFactory::createProcedureArguments(string sArgumentList, string sArgumentValues)
{
    map<string,string> mVarMap;
    NumeReDebugger& _debugger = NumeReKernel::getInstance()->getDebugger();

    if (!sArgumentList.length() && sArgumentValues.length())
    {
        if (_optionRef->getUseDebugger())
            _debugger.popStackItem();

        throw SyntaxError(SyntaxError::TOO_MANY_ARGS, sArgumentList, SyntaxError::invalid_position);
    }

    if (sArgumentList.length())
    {
        if (!validateParenthesisNumber(sArgumentList))
        {
            _debugger.gatherInformations(this, sArgumentList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());
            _debugger.throwException(SyntaxError(SyntaxError::UNMATCHED_PARENTHESIS, sArgumentList, SyntaxError::invalid_position));
        }

        // Get the number of argument of this procedure
        nArgumentMapSize = countVarListElements(sArgumentList);
        sArgumentMap = new string*[nArgumentMapSize];

        // Decode the argument list
        for (unsigned int i = 0; i < nArgumentMapSize; i++)
        {
            sArgumentMap[i] = new string[2];
            sArgumentMap[i][0] = getNextArgument(sArgumentList);
            StripSpaces(sArgumentMap[i][0]);

            checkKeywordsInArgument(sArgumentMap[i][0], sArgumentList, i);

            // Fill in the value of the argument by either
            // using the default value or the passed value
            if (i < nArgumentMapSize-1 && sArgumentValues.length() && sArgumentValues.find(',') != string::npos)
            {
                sArgumentMap[i][1] = getNextArgument(sArgumentValues);
                StripSpaces(sArgumentMap[i][1]);

                checkKeywordsInArgument(sArgumentMap[i][1], sArgumentList, i);

                if (sArgumentMap[i][1].length() && sArgumentMap[i][0].find('=') != string::npos)
                {
                    sArgumentMap[i][0].erase(sArgumentMap[i][0].find('='));
                    StripSpaces(sArgumentMap[i][0]);
                }
                else if (!sArgumentMap[i][1].length() && sArgumentMap[i][0].find('=') != string::npos)
                {
                    sArgumentMap[i][1] = sArgumentMap[i][0].substr(sArgumentMap[i][0].find('=')+1);
                    sArgumentMap[i][0].erase(sArgumentMap[i][0].find('='));
                    StripSpaces(sArgumentMap[i][0]);
                    StripSpaces(sArgumentMap[i][1]);
                }
                else if (!sArgumentMap[i][1].length() && sArgumentMap[i][0].find('=') == string::npos)
                {
                    string sErrorToken = sArgumentMap[i][0];

                    for (unsigned int j = 0; j <= i; j++)
                        delete[] sArgumentMap[j];

                    delete[] sArgumentMap;
                    nArgumentMapSize = 0;

                    if (_optionRef->getUseDebugger())
                        _debugger.popStackItem();

                    throw SyntaxError(SyntaxError::MISSING_DEFAULT_VALUE, sArgumentList, sErrorToken, sErrorToken);
                }
            }
            else if (sArgumentValues.length())
            {
                sArgumentMap[i][1] = sArgumentValues;
                sArgumentValues = "";
                StripSpaces(sArgumentMap[i][1]);

                checkKeywordsInArgument(sArgumentMap[i][1], sArgumentList, i);

                if (sArgumentMap[i][0].find('=') != string::npos)
                {
                    sArgumentMap[i][0] = sArgumentMap[i][0].substr(0,sArgumentMap[i][0].find('='));
                    StripSpaces(sArgumentMap[i][0]);
                }
            }
            else if (!sArgumentValues.length())
            {
                if (sArgumentMap[i][0].find('=') != string::npos)
                {
                    sArgumentMap[i][1] = sArgumentMap[i][0].substr(sArgumentMap[i][0].find('=')+1);
                    sArgumentMap[i][0].erase(sArgumentMap[i][0].find('='));
                    StripSpaces(sArgumentMap[i][0]);
                    StripSpaces(sArgumentMap[i][1]);
                }
                else
                {
                    string sErrorToken = sArgumentMap[i][0];

                    for (unsigned int j = 0; j <= i; j++)
                        delete[] sArgumentMap[j];

                    delete[] sArgumentMap;
                    nArgumentMapSize = 0;

                    if (_optionRef->getUseDebugger())
                        _debugger.popStackItem();

                    throw SyntaxError(SyntaxError::MISSING_DEFAULT_VALUE, sArgumentList, sErrorToken, sErrorToken);
                }
            }
        }

        // Evaluate procedure calls and the parentheses of the
        // passed tables
        for (unsigned int i = 0; i < nArgumentMapSize; i++)
        {
            if (sArgumentMap[i][1].find('$') != string::npos && sArgumentMap[i][1].find('(') != string::npos)
            {
                if (_currentProcedure->getProcedureFlags() & FLAG_INLINE)
                {
                    _debugger.gatherInformations(this, sArgumentList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());
                    _debugger.throwException(SyntaxError(SyntaxError::INLINE_PROCEDURE_IS_NOT_INLINE, sArgumentList, SyntaxError::invalid_position));
                }

                int nReturn = _currentProcedure->procedureInterface(sArgumentMap[i][1], *_parserRef, *_functionRef, *_dataRef, *_outRef, *_pDataRef, *_scriptRef, *_optionRef, nth_procedure);

                if (nReturn == -1)
                {
                    _debugger.gatherInformations(this, sArgumentList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());

                    _debugger.throwException(SyntaxError(SyntaxError::PROCEDURE_ERROR, sArgumentList, SyntaxError::invalid_position));
                }
                else if (nReturn == -2)
                    sArgumentMap[i][1] = "false";
            }
            else if (sArgumentMap[i][0].length() > 2 && sArgumentMap[i][0].substr(sArgumentMap[i][0].length()-2) == "()")
            {
                sArgumentMap[i][0].pop_back();

                if (sArgumentMap[i][1].find('(') != string::npos)
                    sArgumentMap[i][1].erase(sArgumentMap[i][1].find('('));
            }
        }

        for (unsigned int i = 0; i < nArgumentMapSize; i++)
        {
            mVarMap[sArgumentMap[i][0]] = sArgumentMap[i][1];
        }
    }

    return mVarMap;
}

// This member function will create the local numerical variables
// for the current procedure.
void ProcedureVarFactory::createLocalVars(string sVarList)
{
    // already declared the local vars?
    if (nLocalVarMapSize)
        return;

    if (!_currentProcedure)
        return;

    NumeReDebugger& _debugger = NumeReKernel::getInstance()->getDebugger();

    // Get the number of declared variables
    nLocalVarMapSize = countVarListElements(sVarList);

    sLocalVars = new string*[nLocalVarMapSize];
    dLocalVars = new double[nLocalVarMapSize];

    // Decode the variable list
    for (unsigned int i = 0; i < nLocalVarMapSize; i++)
    {
        sLocalVars[i] = new string[2];
        sLocalVars[i][0] = getNextArgument(sVarList, true);

        // Fill in the value of the variable by either
        // using the default or the explicit passed value
        if (sLocalVars[i][0].find('=') != string::npos)
        {
            string sVarValue = sLocalVars[i][0].substr(sLocalVars[i][0].find('=')+1);

            if (sVarValue.find('$') != string::npos && sVarValue.find('(') != string::npos)
            {
                if (_currentProcedure->getProcedureFlags() & FLAG_INLINE)
                {
                    _debugger.gatherInformations(sLocalVars, i, dLocalVars, sLocalStrings, nLocalStrMapSize, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sVarList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());

                    for (unsigned int j = 0; j <= i; j++)
                    {
                        if (j < i)
                            _parserRef->RemoveVar(sLocalVars[j][1]);

                        delete[] sLocalVars[j];
                    }

                    delete[] sLocalVars;
                    nLocalVarMapSize = 0;

                    _debugger.throwException(SyntaxError(SyntaxError::INLINE_PROCEDURE_IS_NOT_INLINE, sVarList, SyntaxError::invalid_position));
                }

                try
                {
                    int nReturn = _currentProcedure->procedureInterface(sVarValue, *_parserRef, *_functionRef, *_dataRef, *_outRef, *_pDataRef, *_scriptRef, *_optionRef, nth_procedure);

                    if (nReturn == -1)
                    {
                        throw SyntaxError(SyntaxError::PROCEDURE_ERROR, sVarList, SyntaxError::invalid_position);
                    }
                    else if (nReturn == -2)
                        sVarValue = "false";
                }
                catch (...)
                {
                    _debugger.gatherInformations(sLocalVars, i, dLocalVars, sLocalStrings, nLocalStrMapSize, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sVarList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());
                    for (unsigned int j = 0; j <= i; j++)
                    {
                        if (j < i)
                            _parserRef->RemoveVar(sLocalVars[j][1]);

                        delete[] sLocalVars[j];
                    }

                    delete[] sLocalVars;
                    nLocalVarMapSize = 0;

                    _debugger.showError(current_exception());
                    throw;
                }
            }

            try
            {
                if (containsStrings(sVarValue) || _dataRef->containsStringVars(sVarValue))
                {
                    string sTemp;

                    if (!parser_StringParser(sVarValue, sTemp, *_dataRef, *_parserRef, *_optionRef, true))
                    {
                        _debugger.gatherInformations(sLocalVars, i, dLocalVars, sLocalStrings, nLocalStrMapSize, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sVarList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());
                        for (unsigned int j = 0; j <= i; j++)
                        {
                            if (j < i)
                                _parserRef->RemoveVar(sLocalVars[j][1]);

                            delete[] sLocalVars[j];
                        }

                        delete[] sLocalVars;
                        nLocalVarMapSize = 0;
                        _debugger.throwException(SyntaxError(SyntaxError::STRING_ERROR, sVarList, SyntaxError::invalid_position));
                    }
                }

                if (sVarValue.find("data(") != string::npos || _dataRef->containsCacheElements(sVarValue))
                {
                    getDataElements(sVarValue, *_parserRef, *_dataRef, *_optionRef);
                }

                for (unsigned int j = 0; j < i; j++)
                {
                    unsigned int nPos = 0;

                    while (sVarValue.find(sLocalVars[j][0], nPos) != string::npos)
                    {
                        nPos = sVarValue.find(sLocalVars[j][0], nPos);

                        if (((nPos+sLocalVars[j][0].length()+1 > sVarValue.length() && checkDelimiter(sVarValue.substr(nPos-1, sLocalVars[j][0].length()+1) + " "))
                            || (nPos+sLocalVars[j][0].length()+1 <= sVarValue.length() && checkDelimiter(sVarValue.substr(nPos-1, sLocalVars[j][0].length()+2))))
                            && (!isInQuotes(sVarValue, nPos, true)
                                || isToCmd(sVarValue, nPos)))
                        {
                            sVarValue.replace(nPos, sLocalVars[j][0].length(), sLocalVars[j][1]);
                            nPos += sLocalVars[j][1].length();
                        }
                        else
                            nPos += sLocalVars[j][0].length();
                    }
                }

                _parserRef->SetExpr(sVarValue);
                sLocalVars[i][0] = sLocalVars[i][0].substr(0,sLocalVars[i][0].find('='));
                dLocalVars[i] = _parserRef->Eval();
            }
            catch (...)
            {
                _debugger.gatherInformations(sLocalVars, i, dLocalVars, sLocalStrings, nLocalStrMapSize, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sVarList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());

                for (unsigned int j = 0; j <= i; j++)
                {
                    if (j < i)
                        _parserRef->RemoveVar(sLocalVars[j][1]);

                    delete[] sLocalVars[j];
                }

                delete[] sLocalVars;
                nLocalVarMapSize = 0;

                _debugger.showError(current_exception());
                throw;
            }
        }
        else
            dLocalVars[i] = 0.0;

        StripSpaces(sLocalVars[i][0]);
        sLocalVars[i][1] = "_~"+sProcName+"_"+toString((int)nth_procedure)+"_"+sLocalVars[i][0];

        _parserRef->DefineVar(sLocalVars[i][1], &dLocalVars[i]);
    }
}

// This member function will create the local string variables
// for the current procedure.
void ProcedureVarFactory::createLocalStrings(string sStringList)
{
    // already declared the local vars?
    if (nLocalStrMapSize)
        return;

    if (!_currentProcedure)
        return;

    NumeReDebugger& _debugger = NumeReKernel::getInstance()->getDebugger();

    // Get the number of declared variables
    nLocalStrMapSize = countVarListElements(sStringList);
    sLocalStrings = new string*[nLocalStrMapSize];

    // Decode the variable list
    for (unsigned int i = 0; i < nLocalStrMapSize; i++)
    {
        sLocalStrings[i] = new string[3];
        sLocalStrings[i][0] = getNextArgument(sStringList, true);

        // Fill in the value of the variable by either
        // using the default or the explicit passed value
        if (sLocalStrings[i][0].find('=') != string::npos)
        {
            string sVarValue = sLocalStrings[i][0].substr(sLocalStrings[i][0].find('=')+1);

            if (sVarValue.find('$') != string::npos && sVarValue.find('(') != string::npos)
            {
                if (_currentProcedure->getProcedureFlags() & FLAG_INLINE)
                {

                    _debugger.gatherInformations(sLocalVars, nLocalVarMapSize, dLocalVars, sLocalStrings, i, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sStringList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());
                    for (unsigned int j = 0; j <= i; j++)
                    {
                        if (j < i)
                            _dataRef->removeStringVar(sLocalStrings[j][1]);

                        delete[] sLocalStrings[j];
                    }

                    delete[] sLocalStrings;
                    nLocalStrMapSize = 0;

                    _debugger.throwException(SyntaxError(SyntaxError::INLINE_PROCEDURE_IS_NOT_INLINE, sStringList, SyntaxError::invalid_position));
                }

                try
                {
                    int nReturn = _currentProcedure->procedureInterface(sVarValue, *_parserRef, *_functionRef, *_dataRef, *_outRef, *_pDataRef, *_scriptRef, *_optionRef, nth_procedure);

                    if (nReturn == -1)
                    {
                        throw SyntaxError(SyntaxError::PROCEDURE_ERROR, sStringList, SyntaxError::invalid_position);
                    }
                    else if (nReturn == -2)
                        sVarValue = "false";
                }
                catch (...)
                {
                    _debugger.gatherInformations(sLocalVars, nLocalVarMapSize, dLocalVars, sLocalStrings, i, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sStringList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());

                    for (unsigned int j = 0; j <= i; j++)
                    {
                        if (j < i)
                            _dataRef->removeStringVar(sLocalStrings[j][1]);

                        delete[] sLocalStrings[j];
                    }

                    delete[] sLocalStrings;
                    nLocalStrMapSize = 0;

                    _debugger.showError(current_exception());
                    throw;
                }
            }

            try
            {
                for (unsigned int j = 0; j < i; j++)
                {
                    unsigned int nPos = 0;

                    while (sVarValue.find(sLocalStrings[j][0], nPos) != string::npos)
                    {
                        nPos = sVarValue.find(sLocalStrings[j][0], nPos);

                        if (((nPos+sLocalStrings[j][0].length()+1 > sVarValue.length() && checkDelimiter(sVarValue.substr(nPos-1, sLocalStrings[j][0].length()+1) + " "))
                            || (nPos+sLocalStrings[j][0].length()+1 <= sVarValue.length() && checkDelimiter(sVarValue.substr(nPos-1, sLocalStrings[j][0].length()+2))))
                            && (!isInQuotes(sVarValue, nPos, true)
                                || isToCmd(sVarValue, nPos)))
                        {
                            sVarValue.replace(nPos, sLocalStrings[j][0].length(), sLocalStrings[j][1]);
                            nPos += sLocalStrings[j][1].length();
                        }
                        else
                            nPos += sLocalStrings[j][0].length();
                    }
                }

                if (containsStrings(sVarValue) || _dataRef->containsStringVars(sVarValue))
                {
                    string sTemp;

                    if (!parser_StringParser(sVarValue, sTemp, *_dataRef, *_parserRef, *_optionRef, true))
                    {
                        _debugger.gatherInformations(sLocalVars, nLocalVarMapSize, dLocalVars, sLocalStrings, i, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sStringList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());
                        _debugger.throwException(SyntaxError(SyntaxError::STRING_ERROR, sStringList, SyntaxError::invalid_position));
                    }
                }

                sLocalStrings[i][0] = sLocalStrings[i][0].substr(0,sLocalStrings[i][0].find('='));
                sLocalStrings[i][2] = sVarValue;
            }
            catch (...)
            {
                for (unsigned int j = 0; j <= i; j++)
                {
                    if (j < i)
                        _dataRef->removeStringVar(sLocalStrings[j][1]);

                    delete[] sLocalStrings[j];
                }

                delete[] sLocalStrings;
                nLocalStrMapSize = 0;
                throw;
            }
        }
        else
            sLocalStrings[i][2] = "";

        StripSpaces(sLocalStrings[i][0]);
        sLocalStrings[i][1] = "_~"+sProcName+"_"+toString((int)nth_procedure)+"_"+sLocalStrings[i][0];

        try
        {
            _dataRef->setStringValue(sLocalStrings[i][1], sLocalStrings[i][2]);
        }
        catch (...)
        {
            _debugger.gatherInformations(sLocalVars, nLocalVarMapSize, dLocalVars, sLocalStrings, i, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sStringList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());

            for (unsigned int j = 0; j <= i; j++)
            {
                if (j < i)
                    _dataRef->removeStringVar(sLocalStrings[j][1]);

                delete[] sLocalStrings[j];
            }

            delete[] sLocalStrings;
            nLocalStrMapSize = 0;

            _debugger.showError(current_exception());
            throw;
        }
    }
}

// This member function will create the local tables
// for the current procedure.
void ProcedureVarFactory::createLocalTables(string sTableList)
{
    // already declared the local vars?
    if (nLocalTableSize)
        return;

    if (!_currentProcedure)
        return;

    // Get the number of declared variables
    nLocalTableSize = countVarListElements(sTableList);
    sLocalTables = new string*[nLocalTableSize];

    // Decode the variable list
    for (unsigned int i = 0; i < nLocalTableSize; i++)
    {
        sLocalTables[i] = new string[2];
        sLocalTables[i][0] = getNextArgument(sTableList, true);

        if (sLocalTables[i][0].find('(') != string::npos)
            sLocalTables[i][0].erase(sLocalTables[i][0].find('('));

        StripSpaces(sLocalTables[i][0]);
        sLocalTables[i][1] = "_~"+sProcName+"_"+toString((int)nth_procedure)+"_"+sLocalTables[i][0];

        try
        {
            _dataRef->addCache(sLocalTables[i][1], *_optionRef);
        }
        catch (...)
        {
            NumeReKernel::getInstance()->getDebugger().gatherInformations(sLocalVars, nLocalVarMapSize, dLocalVars, sLocalStrings, i, sLocalTables, nLocalTableSize, sArgumentMap, nArgumentMapSize, sTableList, _currentProcedure->getCurrentProcedureName(), _currentProcedure->GetCurrentLine());

            for (unsigned int j = 0; j <= i; j++)
            {
                if (j < i)
                    _dataRef->deleteCache(sLocalTables[j][1]);

                delete[] sLocalTables[j];
            }

            delete[] sLocalTables;
            nLocalTableSize = 0;

            NumeReKernel::getInstance()->getDebugger().showError(current_exception());
            throw;
        }
    }
}

// This private member function will resolve the argument
// calls in the passed procedure command line
string ProcedureVarFactory::resolveArguments(string sProcedureCommandLine)
{
    if (!nArgumentMapSize)
        return sProcedureCommandLine;

    for (unsigned int i = 0; i < nArgumentMapSize; i++)
    {
        unsigned int nPos = 0;

        while (sProcedureCommandLine.find(sArgumentMap[i][0].substr(0, sArgumentMap[i][0].length() - (sArgumentMap[i][0].back() == '(')), nPos) != string::npos)
        {
            nPos = sProcedureCommandLine.find(sArgumentMap[i][0].substr(0, sArgumentMap[i][0].length() - (sArgumentMap[i][0].back() == '(')), nPos);

            if ((sProcedureCommandLine[nPos-1] == '~' && sProcedureCommandLine[sProcedureCommandLine.find_last_not_of('~',nPos-1)] != '#')
                || (sArgumentMap[i][0].back() != '(' && sProcedureCommandLine[nPos+sArgumentMap[i][0].length()] == '('))
            {
                nPos += sArgumentMap[i][0].length();
                continue;
            }

            if (checkDelimiter(sProcedureCommandLine.substr(nPos-1, sArgumentMap[i][0].length()+1+(sArgumentMap[i][0].back() != '(')))
                && (!isInQuotes(sProcedureCommandLine, nPos, true)
                    || isToCmd(sProcedureCommandLine, nPos)))
            {
                sProcedureCommandLine.replace(nPos, sArgumentMap[i][0].length()-(sArgumentMap[i][0].back() == '('), sArgumentMap[i][1]);
                nPos += sArgumentMap[i][1].length();
            }
            else
                nPos += sArgumentMap[i][0].length() - (sArgumentMap[i][0].back() == '(');
        }
    }

    return sProcedureCommandLine;
}

// This private member function will resolve the numerical variable
// calls in the passed procedure command line
string ProcedureVarFactory::resolveLocalVars(string sProcedureCommandLine)
{
    if (!nLocalVarMapSize)
        return sProcedureCommandLine;

    for (unsigned int i = 0; i < nLocalVarMapSize; i++)
    {
        unsigned int nPos = 0;

        while (sProcedureCommandLine.find(sLocalVars[i][0], nPos) != string::npos)
        {
            nPos = sProcedureCommandLine.find(sLocalVars[i][0], nPos);

            if ((sProcedureCommandLine[nPos-1] == '~' && sProcedureCommandLine[sProcedureCommandLine.find_last_not_of('~',nPos-1)] != '#')
                || sProcedureCommandLine[nPos+sLocalVars[i][0].length()] == '(')
            {
                nPos += sLocalVars[i][0].length();
                continue;
            }

            if (checkDelimiter(sProcedureCommandLine.substr(nPos-1, sLocalVars[i][0].length()+2))
                && (!isInQuotes(sProcedureCommandLine, nPos, true)
                    || isToCmd(sProcedureCommandLine, nPos)))
            {
                sProcedureCommandLine.replace(nPos, sLocalVars[i][0].length(), sLocalVars[i][1]);
                nPos += sLocalVars[i][1].length();
            }
            else
                nPos += sLocalVars[i][0].length();
        }
    }

    return sProcedureCommandLine;
}

// This private member function will resolve the string variable
// calls in the passed procedure command line
string ProcedureVarFactory::resolveLocalStrings(string sProcedureCommandLine)
{
    if (!nLocalStrMapSize)
        return sProcedureCommandLine;

    for (unsigned int i = 0; i < nLocalStrMapSize; i++)
    {
        unsigned int nPos = 0;

        while (sProcedureCommandLine.find(sLocalStrings[i][0], nPos) != string::npos)
        {
            nPos = sProcedureCommandLine.find(sLocalStrings[i][0], nPos);

            if ((sProcedureCommandLine[nPos-1] == '~' && sProcedureCommandLine[sProcedureCommandLine.find_last_not_of('~', nPos-1)] != '#')
                || sProcedureCommandLine[nPos+sLocalStrings[i][0].length()] == '(')
            {
                nPos += sLocalStrings[i][0].length();
                continue;
            }

            if (checkDelimiter(sProcedureCommandLine.substr(nPos-1, sLocalStrings[i][0].length()+2), true)
                && (!isInQuotes(sProcedureCommandLine, nPos, true) || isToCmd(sProcedureCommandLine, nPos)))
            {
                sProcedureCommandLine.replace(nPos, sLocalStrings[i][0].length(), sLocalStrings[i][1]);
                nPos += sLocalStrings[i][1].length();
            }
            else
                nPos += sLocalStrings[i][0].length();
        }
    }

    return sProcedureCommandLine;
}

// This private member function will resolve the table
// calls in the passed procedure command line
string ProcedureVarFactory::resolveLocalTables(string sProcedureCommandLine)
{
    if (!nLocalTableSize)
        return sProcedureCommandLine;

    for (unsigned int i = 0; i < nLocalTableSize; i++)
    {
        unsigned int nPos = 0;

        while (sProcedureCommandLine.find(sLocalTables[i][0], nPos) != string::npos)
        {
            nPos = sProcedureCommandLine.find(sLocalTables[i][0], nPos);

            if ((sProcedureCommandLine[nPos-1] == '~' && sProcedureCommandLine[sProcedureCommandLine.find_last_not_of('~', nPos-1)] != '#')
                || sProcedureCommandLine[nPos-1] == '$')
            {
                nPos += sLocalTables[i][0].length();
                continue;
            }

            if (checkDelimiter(sProcedureCommandLine.substr(nPos-1, sLocalTables[i][0].length()+2), true)
                && (!isInQuotes(sProcedureCommandLine, nPos, true) || isToCmd(sProcedureCommandLine, nPos)))
            {
                sProcedureCommandLine.replace(nPos, sLocalTables[i][0].length(), sLocalTables[i][1]);
                nPos += sLocalTables[i][1].length();
            }
            else
                nPos += sLocalTables[i][0].length();
        }
    }

    return sProcedureCommandLine;
}


