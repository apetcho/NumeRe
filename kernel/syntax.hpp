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




#ifndef SYNTAX_HPP
#define SYNTAX_HPP

#include <string>
#include <vector>
#include <map>
#include <fstream>

using namespace std;

/////////////////////////////////////////////////
/// \brief A simple structure to define a syntax
/// block for folding, etc.
/////////////////////////////////////////////////
struct SyntaxBlockDefinition
{
    string startWord;
    string endWord;
    string middleWord1;
    string middleWord2;
};



/////////////////////////////////////////////////
/// \brief This class contains all needed
/// keywords to highlight their occurences
/// correspondingly. It will pass them to the
/// NumeReEditor instances. For the terminal,
/// this class acts as lexer. It will also
/// provide autocompletion lists to be used by
/// both, the NumeReEditor instances and the
/// terminal.
/////////////////////////////////////////////////
class NumeReSyntax
{
    private:
        vector<string> vNSCRCommands;
        vector<string> vNPRCCommands;
        vector<string> vOptions;
        vector<string> vFunctions;
        vector<string> vMethods;
        vector<string> vConstants;
        vector<string> vSpecialValues;
        vector<string> vOperators;
        vector<string> vDocKeyWords;
        vector<SyntaxBlockDefinition> vBlockDefs;
        vector<string> vMatlabKeyWords;
        vector<string> vMatlabFunctions;
        vector<string> vCppKeyWords;
        vector<string> vCppFunctions;
        vector<string> vTeXKeyWords;

        vector<string> vProcedureTree;

        string sSingleOperators;
        map<string, int> mAutoCompList;
        map<string, int> mAutoCompListMATLAB;
        map<string, int> mAutoCompListCPP;
        map<string, int> mAutoCompListTeX;

        string sPath;

        string constructString(const vector<string>& vVector) const;
        vector<string> splitString(string sString);
        vector<SyntaxBlockDefinition> splitDefs(string sDefString);
        bool matchItem(const vector<string>& vVector, const string& sString);
    public:
        enum SyntaxColors
        {
            SYNTAX_COMMAND = 1,
            SYNTAX_OPTION,
            SYNTAX_FUNCTION,
            SYNTAX_CONSTANT,
            SYNTAX_SPECIALVAL,
            SYNTAX_STRING,
            SYNTAX_STD,
            SYNTAX_OPERATOR,
            SYNTAX_PROCEDURE,
            SYNTAX_NUMBER,
            SYNTAX_NPRC_COMMAND,
            SYNTAX_METHODS
        };
        NumeReSyntax();
        NumeReSyntax(const string& _sPath);

        void loadSyntax(const string& _sPath = "");
        void addPlugins(const vector<string>& vPlugins);
        void setProcedureTree(const vector<string>& vTree);
        string getCommands() const
            {return constructString(vNSCRCommands);}
        string getNPRCCommands() const
            {return constructString(vNPRCCommands);}
        string getOptions() const
            {return constructString(vOptions);}
        string getFunctions() const
            {return constructString(vFunctions);}
        string getMethods() const
            {return constructString(vMethods);}
        string getConstants() const
            {return constructString(vConstants);}
        string getSpecial() const
            {return constructString(vSpecialValues);}
        string getOperators() const
            {return constructString(vOperators);}
        string getDocKeyWords() const
            {return constructString(vDocKeyWords);}
        string getBlockDefs() const;
        string getMatlab() const
            {return constructString(vMatlabKeyWords);}
        string getMatlabFunctions() const
            {return constructString(vMatlabFunctions);}
        string getCpp() const
            {return constructString(vCppKeyWords);}
        string getCppFunctions() const
            {return constructString(vCppFunctions);}
        string getAutoCompList(string sFirstChars, string sType = "NSCR");
        string getAutoCompListMATLAB(string sFirstChars);
        string getAutoCompListCPP(string sFirstChars);
        string getAutoCompListTeX(string sFirstChars);
        string getProcAutoCompList(string sFirstChars, string sBaseNameSpace, string sSelectedNameSpace);
        string getNameSpaceAutoCompList(string sFirstChars);

        string highlightLine(const string& sCommandLine);
        string highlightError(const string& sCommandLine);
        string highlightWarning(const string& sCommandLine);

        vector<SyntaxBlockDefinition> getFullBlockDefs() const
            {return vBlockDefs;}

};
#endif // SYNTAX_HPP

