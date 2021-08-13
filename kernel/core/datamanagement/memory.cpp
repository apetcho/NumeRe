/*****************************************************************************
    NumeRe: Framework fuer Numerische Rechnungen
    Copyright (C) 2018  Erik Haenel et al.

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

#include <memory>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_sort.h>

#include "memory.hpp"
#include "tablecolumnimpl.hpp"
#include "../../kernel.hpp"
#include "../io/file.hpp"
#include "../ui/error.hpp"
#include "../settings.hpp"
#include "../utils/tools.hpp"
#include "../version.h"
#include "../maths/resampler.h"

#define MAX_TABLE_SIZE 1e8
#define MAX_TABLE_COLS 1e4
#define DEFAULT_COL_TYPE ValueColumn

using namespace std;
extern Language _lang;



/////////////////////////////////////////////////
/// \brief Default constructor.
/////////////////////////////////////////////////
Memory::Memory()
{
    nCalcLines = -1;
    bSaveMutex = false;
    bIsSaved = true;
    nLastSaved = time(0);
}


/////////////////////////////////////////////////
/// \brief Specialized constructor to allocate a
/// defined table size.
///
/// \param _nCols size_t
///
/////////////////////////////////////////////////
Memory::Memory(size_t _nCols) : Memory()
{
    Allocate(_nCols);
}


/////////////////////////////////////////////////
/// \brief Memory class destructor, which will
/// free the allocated memory.
/////////////////////////////////////////////////
Memory::~Memory()
{
    clear();
}


/////////////////////////////////////////////////
/// \brief This member function is the Memory
/// class allocator. It will handle all memory
/// allocations.
///
/// \param _nNCols size_t
/// \param shrink bool
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::Allocate(size_t _nNCols, bool shrink)
{
    if (_nNCols > MAX_TABLE_COLS)
        throw SyntaxError(SyntaxError::TOO_LARGE_CACHE, "", SyntaxError::invalid_position);

    // We simply resize the number of columns. Note, that
    // this only affects the column count. The column themselves
    // are not automatically allocated
    memArray.resize(_nNCols);

    if (shrink)
    {
        // Iterate through the columns
        for (TblColPtr& col : memArray)
        {
            // If a column exist, call the shrink method
            if (col)
                col->shrink();
        }
    }

    return true;
}


/////////////////////////////////////////////////
/// \brief This member function creates the
/// column headlines, if they are empty.
///
/// \return void
///
/////////////////////////////////////////////////
void Memory::createTableHeaders()
{
    for (size_t j = 0; j < memArray.size(); j++)
    {
        if (!memArray[j])
            memArray[j].reset(new DEFAULT_COL_TYPE);

        if (!memArray[j]->m_sHeadLine.length())
            memArray[j]->m_sHeadLine = _lang.get("COMMON_COL") + "_" + toString(j+1);
    }
}


/////////////////////////////////////////////////
/// \brief This member function frees the
/// internally used memory block completely.
///
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::clear()
{
    memArray.clear();

    nCalcLines = -1;
    bIsSaved = false;
    bSaveMutex = false;

    return true;
}


/////////////////////////////////////////////////
/// \brief This member function will handle all
/// memory grow operations by doubling the base
/// size, which shall be incremented, as long as
/// it is smaller than the requested size.
///
/// \param _nLines size_t
/// \param _nCols size_t
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::resizeMemory(size_t _nLines, size_t _nCols)
{
    if (!Allocate(_nCols))
        return false;

    return true;
}


/////////////////////////////////////////////////
/// \brief This member function will return the
/// number of columns, which are currently
/// available in this table.
///
/// \param _bFull bool true, if the reserved
/// number of columns is requested, false if only
/// the non-empty ones are requested
/// \return size_t
///
/////////////////////////////////////////////////
size_t Memory::getCols(bool _bFull) const
{
    return memArray.size();
}


/////////////////////////////////////////////////
/// \brief This member function will return the
/// number of lines, which are currently
/// available in this table.
///
/// \param _bFull bool true, if the reserved
/// number of lines is requested, false if only
/// the non-empty ones are requested
/// \return size_t
///
/////////////////////////////////////////////////
size_t Memory::getLines(bool _bFull) const
{
    if (memArray.size())
    {
        if (nCalcLines != -1)
            return nCalcLines;

        size_t nReturn = 0;

        for (const TblColPtr& col : memArray)
        {
            if (col && col->size() > nReturn)
                nReturn = col->size();
        }

        // Cache the number of lines until invalidation
        // for faster access
        nCalcLines = nReturn;

        return nReturn;
    }

    return 0;
}


/////////////////////////////////////////////////
/// \brief Returns the overall used number of
/// bytes for this table.
///
/// \return size_t
///
/////////////////////////////////////////////////
size_t Memory::getSize() const
{
    size_t bytes = 0;

    for (const TblColPtr& col : memArray)
    {
        if (col)
            bytes += col->getBytes();
    }

    return bytes;
}


/////////////////////////////////////////////////
/// \brief This member function returns the
/// element stored at the selected position.
///
/// \param _nLine size_t
/// \param _nCol size_t
/// \return double
///
/////////////////////////////////////////////////
mu::value_type Memory::readMem(size_t _nLine, size_t _nCol) const
{
    if (memArray.size() > _nCol && memArray[_nCol])
        return memArray[_nCol]->getValue(_nLine);

    return NAN;
}


/////////////////////////////////////////////////
/// \brief This static helper function calculates
/// the average value respecting NaNs.
///
/// \param values const std::vector<mu::value_type>&
/// \return mu::value_type
///
/////////////////////////////////////////////////
static mu::value_type nanAvg(const std::vector<mu::value_type>& values)
{
    mu::value_type sum = 0.0;
    double c = 0.0;

    for (mu::value_type val : values)
    {
        if (!mu::isnan(val))
        {
            sum += val;
            c++;
        }
    }

    if (c)
        return sum / c;

    return sum;
}


/////////////////////////////////////////////////
/// \brief This member function returns a
/// (bilinearily) interpolated element at the
/// selected \c double positions.
///
/// \param _dLine double
/// \param _dCol double
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::readMemInterpolated(double _dLine, double _dCol) const
{
    if (isnan(_dLine) || isnan(_dCol))
        return NAN;

    // Find the base index
    int nBaseLine = intCast(_dLine) + (_dLine < 0 ? -1 : 0);
    int nBaseCol = intCast(_dCol) + (_dCol < 0 ? -1 : 0);

    // Get the decimal part of the double indices
    double x = _dLine - nBaseLine;
    double y = _dCol - nBaseCol;

    // Find the surrounding four entries
    mu::value_type f00 = readMem(nBaseLine, nBaseCol);
    mu::value_type f10 = readMem(nBaseLine+1, nBaseCol);
    mu::value_type f01 = readMem(nBaseLine, nBaseCol+1);
    mu::value_type f11 = readMem(nBaseLine+1, nBaseCol+1);

    // If all are NAN, return NAN
    if (mu::isnan(f00) && mu::isnan(f01) && mu::isnan(f10) && mu::isnan(f11))
        return NAN;

    // Get the average respecting NaNs
    mu::value_type dNanAvg = nanAvg({f00, f01, f10, f11});

    // Otherwise set NAN to zero
    f00 = mu::isnan(f00) ? dNanAvg : f00;
    f10 = mu::isnan(f10) ? dNanAvg : f10;
    f01 = mu::isnan(f01) ? dNanAvg : f01;
    f11 = mu::isnan(f11) ? dNanAvg : f11;

    //     f(0,0) (1-x) (1-y) + f(1,0) x (1-y) + f(0,1) (1-x) y + f(1,1) x y
    return f00*(1.0-x)*(1.0-y)    + f10*x*(1.0-y)    + f01*(1.0-x)*y    + f11*x*y;
}


/////////////////////////////////////////////////
/// \brief This member function returns the
/// elements stored at the selected positions.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return std::vector<mu::value_type>
///
/////////////////////////////////////////////////
std::vector<mu::value_type> Memory::readMem(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    std::vector<mu::value_type> vReturn;

    if ((_vLine.size() > 1 && _vCol.size() > 1) || !memArray.size())
        vReturn.push_back(NAN);
    else
    {
        for (size_t i = 0; i < _vLine.size(); i++)
        {
            for (size_t j = 0; j < _vCol.size(); j++)
            {
                if (_vCol[j] < 0 || _vCol[j] >= memArray.size() || !memArray[_vCol[j]])
                    vReturn.push_back(NAN);
                else
                    vReturn.push_back(memArray[_vCol[j]]->getValue(_vLine[i]));
            }
        }
    }

    return vReturn;
}

std::vector<double> Memory::readRealMem(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    std::vector<double> vReturn;

    if ((_vLine.size() > 1 && _vCol.size() > 1) || !memArray.size())
        vReturn.push_back(NAN);
    else
    {
        for (size_t i = 0; i < _vLine.size(); i++)
        {
            for (size_t j = 0; j < _vCol.size(); j++)
            {
                if (_vCol[j] < 0 || _vCol[j] >= memArray.size() || !memArray[_vCol[j]])
                    vReturn.push_back(NAN);
                else
                    vReturn.push_back(memArray[_vCol[j]]->getValue(_vLine[i]).real());
            }
        }
    }

    return vReturn;
}


/////////////////////////////////////////////////
/// \brief This member function extracts a range
/// of this table and returns it as a new Memory
/// instance.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return Memory*
///
/// \remark The caller gets ownership of the
/// returned Memory instance.
///
/////////////////////////////////////////////////
Memory* Memory::extractRange(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    Memory* _memCopy = new Memory();

    _vLine.setOpenEndIndex(getLines(false)-1);
    _vCol.setOpenEndIndex(getCols(false)-1);

    _memCopy->Allocate(_vCol.size());

    for (size_t j = 0; j < _vCol.size(); j++)
    {
        if (_vCol[j] >= 0 && _vCol[j] < memArray.size() && memArray[_vCol[j]])
            _memCopy->memArray[j].reset(memArray[_vCol[j]]->copy(_vLine));
    }

    return _memCopy;
}


/////////////////////////////////////////////////
/// \brief This member function will copy the
/// selected elements into the passed vector
/// instance. This member function avoids copies
/// of the vector instance by directly writing to
/// the target instance.
///
/// \param vTarget vector<mu::value_type>*
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return void
///
/////////////////////////////////////////////////
void Memory::copyElementsInto(vector<mu::value_type>* vTarget, const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    vTarget->clear();

    if ((_vLine.size() > 1 && _vCol.size() > 1) || !memArray.size())
        vTarget->resize(1, NAN);
    else
    {
        vTarget->resize(_vLine.size()*_vCol.size(), NAN);

        for (size_t i = 0; i < _vLine.size(); i++)
        {
            for (size_t j = 0; j < _vCol.size(); j++)
            {
                if (_vCol[j] < 0 || _vCol[j] >= memArray.size() || !memArray[_vCol[j]])
                    (*vTarget)[j + i * _vCol.size()] = NAN;
                else
                    (*vTarget)[j + i * _vCol.size()] = memArray[_vCol[j]]->getValue(_vLine[i]);
            }
        }
    }
}


/////////////////////////////////////////////////
/// \brief Returns true, if the element at the
/// selected positions is valid. Only checks
/// internally, if the value is not a NaN value.
///
/// \param _nLine size_t
/// \param _nCol size_t
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::isValidElement(size_t _nLine, size_t _nCol) const
{
    if (_nCol < memArray.size() && _nCol >= 0 && memArray[_nCol])
        return !mu::isnan(memArray[_nCol]->getValue(_nLine));

    return false;
}


/////////////////////////////////////////////////
/// \brief Returns true, if at least a single
/// valid value is available in this table.
///
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::isValid() const
{
    return getLines();
}


/////////////////////////////////////////////////
/// \brief This member function shrinks the table
/// memory to the smallest possible dimensions
/// reachable in powers of two.
///
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::shrink()
{
    if (!memArray.size())
        return true;

    // Shrink each column
    for (TblColPtr& col : memArray)
    {
        if (col)
            col->shrink();
    }

    // Remove possible unneeded columns
    for (int j = memArray.size()-1; j >= 0; j--)
    {
        if (memArray[j] && memArray[j]->size())
        {
            if (j < memArray.size()-1)
                memArray.resize(j+1);

            break;
        }
    }

    return true;
}


/////////////////////////////////////////////////
/// \brief Returns, whether the contents of the
/// current table are already saved into either
/// a usual file or into the cache file.
///
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::getSaveStatus() const
{
    return bIsSaved;
}


/////////////////////////////////////////////////
/// \brief Returns the table column headline for
/// the selected column. Will return a default
/// headline, if the column is empty or does not
/// exist.
///
/// \param _i size_t
/// \return std::string
///
/////////////////////////////////////////////////
std::string Memory::getHeadLineElement(size_t _i) const
{
    if (_i >= memArray.size() || !memArray[_i])
        return _lang.get("COMMON_COL") + "_" + toString((int)_i + 1) + " (leer)";
    else
        return memArray[_i]->m_sHeadLine;
}


/////////////////////////////////////////////////
/// \brief Returns the table column headlines for
/// the selected columns. Will return default
/// headlines for empty or non-existing columns.
///
/// \param _vCol const VectorIndex&
/// \return vector<string>
///
/////////////////////////////////////////////////
vector<string> Memory::getHeadLineElement(const VectorIndex& _vCol) const
{
    vector<string> vHeadLines;

    for (unsigned int i = 0; i < _vCol.size(); i++)
    {
        if (_vCol[i] < 0)
            continue;

        vHeadLines.push_back(getHeadLineElement(_vCol[i]));
    }

    return vHeadLines;
}


/////////////////////////////////////////////////
/// \brief Writes a new table column headline to
/// the selected column.
///
/// \param _i size_t
/// \param _sHead const std::string&
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::setHeadLineElement(size_t _i, const std::string& _sHead)
{
    if (_i >= memArray.size())
    {
        if (!resizeMemory(1, _i + 1))
            return false;
    }

    if (!memArray[_i])
        memArray[_i].reset(new DEFAULT_COL_TYPE);

    memArray[_i]->m_sHeadLine = _sHead;

    if (bIsSaved)
    {
        nLastSaved = time(0);
        bIsSaved = false;
    }

    return true;
}


/////////////////////////////////////////////////
/// \brief Returns the number of empty cells at
/// the end of the selected columns.
///
/// \param _i size_t
/// \return size_t
///
/////////////////////////////////////////////////
size_t Memory::getAppendedZeroes(size_t _i) const
{
    if (_i < memArray.size() && memArray[_i])
        return getLines() - memArray[_i]->size();

    return getLines();
}


/////////////////////////////////////////////////
/// \brief This member function returns the
/// number of lines needed for the table column
/// headline of the selected column.
///
/// \return size_t
///
/////////////////////////////////////////////////
size_t Memory::getHeadlineCount() const
{
    size_t nHeadlineCount = 1;

    // Get the dimensions of the complete headline (i.e. including possible linebreaks)
    for (const TblColPtr& col : memArray)
    {
        // No linebreak? Continue
        if (!col || col->m_sHeadLine.find("\\n") == std::string::npos)
            continue;

        size_t nLinebreak = 0;

        // Count all linebreaks
        for (size_t n = 0; n < col->m_sHeadLine.length() - 2; n++)
        {
            if (col->m_sHeadLine.substr(n, 2) == "\\n")
                nLinebreak++;
        }

        // Save the maximal number
        if (nLinebreak + 1 > nHeadlineCount)
            nHeadlineCount = nLinebreak + 1;
    }

    return nHeadlineCount;
}


/////////////////////////////////////////////////
/// \brief This member function writes the passed
/// value to the selected position. The table is
/// automatically enlarged, if necessary.
///
/// \param _nLine size_t
/// \param _nCol size_t
/// \param _dData const mu::value_type&
/// \return void
///
/////////////////////////////////////////////////
void Memory::writeData(size_t _nLine, size_t _nCol, const mu::value_type& _dData)
{
    if (!memArray.size() && mu::isnan(_dData))
        return;

    if (memArray.size() <= _nCol)
        resizeMemory(_nLine+1, _nCol+1);

    if (!memArray[_nCol])
        memArray[_nCol].reset(new ValueColumn);

    memArray[_nCol]->setValue(_nLine, _dData);

    if ((mu::isnan(_dData) || _nLine >= nCalcLines) && nCalcLines != -1)
        nCalcLines = -1;

    // --> Setze den Zeitstempel auf "jetzt", wenn der Memory eben noch gespeichert war <--
    if (bIsSaved)
    {
        nLastSaved = time(0);
        bIsSaved = false;
    }
}


/////////////////////////////////////////////////
/// \brief This member function writes a whole
/// array of data to the selected table range.
/// The table is automatically enlarged, if
/// necessary.
///
/// \param _idx Indices&
/// \param _dData mu::value_type*
/// \param _nNum unsigned int
/// \return void
///
/////////////////////////////////////////////////
void Memory::writeData(Indices& _idx, mu::value_type* _dData, unsigned int _nNum)
{
    int nDirection = LINES;

    if (_nNum == 1)
    {
        writeSingletonData(_idx, _dData[0]);
        return;
    }

    _idx.row.setOpenEndIndex(_idx.row.front() + _nNum - 1);
    _idx.col.setOpenEndIndex(_idx.col.front() + _nNum - 1);

    if (_idx.row.size() > 1)
        nDirection = COLS;
    else if (_idx.col.size() > 1)
        nDirection = LINES;

    for (size_t i = 0; i < _idx.row.size(); i++)
    {
        for (size_t j = 0; j < _idx.col.size(); j++)
        {
            if (nDirection == COLS)
            {
                if (_nNum > i)
                    writeData(_idx.row[i], _idx.col[j], _dData[i]);
            }
            else
            {
                if (_nNum > j)
                    writeData(_idx.row[i], _idx.col[j], _dData[j]);
            }
        }
    }
}


/////////////////////////////////////////////////
/// \brief This member function writes multiple
/// copies of a single value to a range in the
/// table. The table is automatically enlarged,
/// if necessary.
///
/// \param _idx Indices&
/// \param _dData const mu::value_type&
/// \return void
///
/////////////////////////////////////////////////
void Memory::writeSingletonData(Indices& _idx, const mu::value_type& _dData)
{
    _idx.row.setOpenEndIndex(std::max(_idx.row.front(), (long long int)getLines(false)) - 1);
    _idx.col.setOpenEndIndex(std::max(_idx.col.front(), (long long int)getCols(false)) - 1);

    for (size_t i = 0; i < _idx.row.size(); i++)
    {
        for (size_t j = 0; j < _idx.col.size(); j++)
        {
            writeData(_idx.row[i], _idx.col[j], _dData);
        }
    }
}


/////////////////////////////////////////////////
/// \brief This member function changes the saved
/// state to the passed value.
///
/// \param _bIsSaved bool
/// \return void
///
/////////////////////////////////////////////////
void Memory::setSaveStatus(bool _bIsSaved)
{
    bIsSaved = _bIsSaved;

    if (bIsSaved)
        nLastSaved = time(0);
}


/////////////////////////////////////////////////
/// \brief This member function returns the time-
/// point, where the table was saved last time.
///
/// \return long long int
///
/////////////////////////////////////////////////
long long int Memory::getLastSaved() const
{
    return nLastSaved;
}


/////////////////////////////////////////////////
/// \brief This member function is the interface
/// function for the Sorter class. It will pre-
/// evaluate the passed parameters and redirect
/// the control to the corresponding sorting
/// function.
///
/// \param i1 int
/// \param i2 int
/// \param j1 int
/// \param j2 int
/// \param sSortingExpression const std::string&
/// \return vector<int>
///
/////////////////////////////////////////////////
vector<int> Memory::sortElements(int i1, int i2, int j1, int j2, const std::string& sSortingExpression)
{
    if (!memArray.size())
        return vector<int>();

    bool bError = false;
    bool bReturnIndex = false;
    int nSign = 1;

    i1 = std::max(0, i1);
    j1 = std::max(0, j1);

    vector<int> vIndex;

    // Determine the sorting direction
    if (findParameter(sSortingExpression, "desc"))
        nSign = -1;

    if (i2 == -1)
        i2 = i1;

    if (j2 == -1)
        j2 = j1;

    // Prepare the sorting index
    for (int i = i1; i <= i2; i++)
        vIndex.push_back(i);

    // Evaluate, whether an index shall be returned
    // (instead of actual reordering the columns)
    if (findParameter(sSortingExpression, "index"))
        bReturnIndex = true;

    // Is a column group selected or do we actually
    // sort everything?
    if (!findParameter(sSortingExpression, "cols", '=') && !findParameter(sSortingExpression, "c", '='))
    {
        // Sort everything independently
        for (int i = j1; i <= j2; i++)
        {
            // Sort the current column
            if (!qSort(&vIndex[0], i2 - i1 + 1, i, 0, i2 - i1, nSign))
                throw SyntaxError(SyntaxError::CANNOT_SORT_CACHE, sSortingExpression, SyntaxError::invalid_position);

            // Abort after the first column, if
            // an index shall be returned
            if (bReturnIndex)
                break;

            // Actually reorder the column
            reorderColumn(vIndex, i1, i2, i);

            // Reset the sorting index
            for (int j = i1; j <= i2; j++)
                vIndex[j] = j;
        }
    }
    else
    {
        // Sort groups of columns (including
        // hierarchical sorting)
        string sCols = "";

        // Find the column group definition
        if (findParameter(sSortingExpression, "cols", '='))
            sCols = getArgAtPos(sSortingExpression, findParameter(sSortingExpression, "cols", '=') + 4);
        else
            sCols = getArgAtPos(sSortingExpression, findParameter(sSortingExpression, "c", '=') + 1);

        // As long as the column group definition
        // has a length
        while (sCols.length())
        {
            // Get a new column keys instance
            ColumnKeys* keys = evaluateKeyList(sCols, j2 - j1 + 1);

            // Ensure that we obtained an actual
            // instance
            if (!keys)
                throw SyntaxError(SyntaxError::CANNOT_SORT_CACHE,  sSortingExpression, SyntaxError::invalid_position);

            if (keys->nKey[1] == -1)
                keys->nKey[1] = keys->nKey[0] + 1;

            // Go through the group definition
            for (int j = keys->nKey[0]; j < keys->nKey[1]; j++)
            {
                // Sort the current key list level
                // independently
                if (!qSort(&vIndex[0], i2 - i1 + 1, j + j1, 0, i2 - i1, nSign))
                {
                    delete keys;
                    throw SyntaxError(SyntaxError::CANNOT_SORT_CACHE, sSortingExpression, SyntaxError::invalid_position);
                }

                // Subkey list: sort the subordinate group
                // depending on the higher-level key group
                if (keys->subkeys && keys->subkeys->subkeys)
                {
                    if (!sortSubList(&vIndex[0], i2 - i1 + 1, keys, i1, i2, j1, nSign, getCols(false)))
                    {
                        delete keys;
                        throw SyntaxError(SyntaxError::CANNOT_SORT_CACHE, sSortingExpression, SyntaxError::invalid_position);
                    }
                }

                // Break, if the index shall be returned
                if (bReturnIndex)
                    break;

                // Actually reorder the current column
                reorderColumn(vIndex, i1, i2, j + j1);

                // Obtain the subkey list
                ColumnKeys* subKeyList = keys->subkeys;

                // As long as a subkey list is available
                while (subKeyList)
                {
                    if (subKeyList->nKey[1] == -1)
                        subKeyList->nKey[1] = subKeyList->nKey[0] + 1;

                    // Reorder the subordinate key list
                    for (int _j = subKeyList->nKey[0]; _j < subKeyList->nKey[1]; _j++)
                        reorderColumn(vIndex, i1, i2, _j + j1);

                    // Find the next subordinate list
                    subKeyList = subKeyList->subkeys;
                }

                // Reset the sorting index for the next column
                for (int _j = i1; _j <= i2; _j++)
                    vIndex[_j] = _j;
            }

            // Free the occupied memory
            delete keys;

            if (bReturnIndex)
                break;
        }
    }

    // Number of lines might have changed
    nCalcLines = -1;

    // Increment each index value, if the index
    // vector shall be returned
    if (bReturnIndex)
    {
        for (int i = 0; i <= i2 - i1; i++)
            vIndex[i]++;
    }

    if (bIsSaved)
    {
        bIsSaved = false;
        nLastSaved = time(0);
    }

    if (bError || !bReturnIndex)
        return vector<int>();

    return vIndex;
}


/////////////////////////////////////////////////
/// \brief This member function simply reorders
/// the contents of the selected column using the
/// passed index vector.
///
/// \param vIndex const vector<int>&
/// \param i1 int
/// \param i2 int
/// \param j1 int
/// \return void
///
/////////////////////////////////////////////////
void Memory::reorderColumn(const vector<int>& vIndex, int i1, int i2, int j1)
{
    double* dSortVector = new double[i2 - i1 + 1];

    // Copy the sorted values to a buffer
    for (int i = 0; i <= i2 - i1; i++)
        dSortVector[i] = dMemTable[vIndex[i]][j1];

    // Copy the contents back to the table
    for (int i = 0; i <= i2 - i1; i++)
        dMemTable[i + i1][j1] = dSortVector[i];

    delete[] dSortVector;
}


/////////////////////////////////////////////////
/// \brief Override for the virtual Sorter class
/// member function. Returns 0, if both elements
/// are equal, -1 if element i is smaller than
/// element j and 1 otherwise.
///
/// \param i int
/// \param j int
/// \param col int
/// \return int
///
/////////////////////////////////////////////////
int Memory::compare(int i, int j, int col)
{
    if (dMemTable[i][col] == dMemTable[j][col])
        return 0;
    else if (dMemTable[i][col] < dMemTable[j][col])
        return -1;

    return 1;
}


/////////////////////////////////////////////////
/// \brief Override for the virtual Sorter class
/// member function. Returns true, if the
/// selected element is a valid value.
///
/// \param line int
/// \param col int
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::isValue(int line, int col)
{
    return !isnan(dMemTable[line][col]);
}


/////////////////////////////////////////////////
/// \brief Create a copy-efficient table object
/// from the data contents.
///
/// \param _sTable const string&
/// \return NumeRe::Table
///
/////////////////////////////////////////////////
NumeRe::Table Memory::extractTable(const string& _sTable, const VectorIndex& lines, const VectorIndex& cols)
{
    lines.setOpenEndIndex(getLines(false)-1);
    cols.setOpenEndIndex(getCols(false)-1);

    NumeRe::Table table(lines.size(), cols.size());

    table.setName(_sTable);

    for (size_t i = 0; i < lines.size(); i++)
    {
        for (size_t j = 0; j < cols.size(); j++)
        {
            if (!i)
                table.setHead(j, getHeadLineElement(cols[j]));

            table.setValue(i, j, readMem(lines[i], cols[j]));
        }
    }

    return table;
}


/////////////////////////////////////////////////
/// \brief Import data from a copy-efficient
/// table object. Completely replaces the
/// contents, which were in the internal storage
/// before.
///
/// \param _table NumeRe::Table
/// \return void
///
/////////////////////////////////////////////////
void Memory::importTable(NumeRe::Table _table, const VectorIndex& lines, const VectorIndex& cols)
{
    deleteBulk(lines, cols);

    lines.setOpenEndIndex(lines.front() + _table.getLines()-1);
    cols.setOpenEndIndex(cols.front() + _table.getCols()-1);

    resizeMemory(lines.max(), cols.max());

    for (size_t i = 0; i < _table.getLines(); i++)
    {
        if (i >= lines.size())
            break;

        for (size_t j = 0; j < _table.getCols(); j++)
        {
            if (j >= cols.size())
                break;

            // Use writeData() to automatically set all
            // other parameters
            writeData(lines[i], cols[j], _table.getValue(i, j));
        }
    }

    // Set the table heads, if they have
    // a non-zero length
    for (size_t j = 0; j < _table.getCols(); j++)
    {
        if (j >= cols.size())
                break;

        if (_table.getHead(j).length())
            sHeadLine[cols[j]] = _table.getHead(j);
    }
}


/////////////////////////////////////////////////
/// \brief This member function is used for
/// saving the contents of this memory page into
/// a file. The type of the file is selected by
/// the name of the file.
///
/// \param _sFileName string
/// \param sTableName const string&
/// \param nPrecision unsigned short
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::save(string _sFileName, const string& sTableName, unsigned short nPrecision)
{
    // Get an instance of the desired file type
    NumeRe::GenericFile<double>* file = NumeRe::getFileByType(_sFileName);

    // Ensure that a file was created
    if (!file)
        throw SyntaxError(SyntaxError::CANNOT_SAVE_FILE, _sFileName, SyntaxError::invalid_position, _sFileName);

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    // Set the dimensions and the generic information
    // in the file
    file->setDimensions(lines, cols);
    file->setColumnHeadings(sHeadLine, cols);
    file->setData(dMemTable, lines, cols);
    file->setTableName(sTableName);
    file->setTextfilePrecision(nPrecision);

    // If the file type is a NumeRe data file, then
    // we can also set the comment associated with
    // this memory page
    if (file->getExtension() == "ndat")
        static_cast<NumeRe::NumeReDataFile*>(file)->setComment("");

    // Try to write the data to the file. This might
    // either result in writing errors or the write
    // function is not defined for this file type
    try
    {
        if (!file->write())
            throw SyntaxError(SyntaxError::CANNOT_SAVE_FILE, _sFileName, SyntaxError::invalid_position, _sFileName);
    }
    catch (...)
    {
        delete file;
        throw;
    }

    // Delete the created file instance
    delete file;

    return true;
}


/////////////////////////////////////////////////
/// \brief This member function deletes a single
/// entry from the memory table.
///
/// \param _nLine long long int
/// \param _nCol long long int
/// \return void
///
/////////////////////////////////////////////////
void Memory::deleteEntry(long long int _nLine, long long int _nCol)
{
    if (_nLine < 0 || _nCol < 0)
        return;

    if (dMemTable)
    {
        if (!isnan(dMemTable[_nLine][_nCol]))
        {
            // Set the element to NaN
            dMemTable[_nLine][_nCol] = NAN;

            if (bIsSaved)
            {
                nLastSaved = time(0);
                bIsSaved = false;
            }

            // Count the appended zeroes
            for (long long int i = nLines - 1; i >= 0; i--)
            {
                if (!isnan(dMemTable[i][_nCol]))
                {
                    nAppendedZeroes[_nCol] = nLines - i - 1;
                    break;
                }

                if (!i && isnan(dMemTable[i][_nCol]))
                {
                    nAppendedZeroes[_nCol] = nLines;

                    // If the first element of this column was currently
                    // removed, reset its headline
                    if (!_nLine)
                    {
                        sHeadLine[_nCol] = _lang.get("COMMON_COL") + "_" + toString((int)_nCol + 1);

                        if (nWrittenHeadlines > _nCol)
                            nWrittenHeadlines = _nCol;
                    }
                }
            }

            nCalcCols = -1;
            nCalcLines = -1;

            if (!getLines(false) && !getCols(false))
                bValidData = false;
        }
    }
}


/////////////////////////////////////////////////
/// \brief This member function deletes a whole
/// range of entries from the memory table.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return void
///
/////////////////////////////////////////////////
void Memory::deleteBulk(const VectorIndex& _vLine, const VectorIndex& _vCol)
{
    bool bHasFirstLine = false;

    if (!Memory::getCols(false))
        return;

    _vLine.setOpenEndIndex(nLines-1);
    _vCol.setOpenEndIndex(nCols-1);

    // Delete the selected entries and detect,
    // whether any index value corresponds to
    // a first element in any column.
    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        if (!_vLine[i])
            bHasFirstLine = true;

        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vCol[j] >= nCols || _vCol[j] < 0 || _vLine[i] >= nLines || _vLine[i] < 0)
                continue;

            dMemTable[_vLine[i]][_vCol[j]] = NAN;
        }
    }

    if (bIsSaved)
    {
        bIsSaved = false;
        nLastSaved = time(0);
    }

    // Count the appended zeroes
    for (long long int j = nCols - 1; j >= 0; j--)
    {
        int currentcol = -1;

        // Detect, whether the current colum was in
        // the list of selected column indices
        for (size_t i = 0; i < _vCol.size(); i++)
        {
            if (_vCol[i] == j)
                currentcol = (int)i;
        }

        for (long long int i = nLines - 1; i >= 0; i--)
        {
            // Find the last valid value
            if (!isnan(dMemTable[i][j]))
            {
                nAppendedZeroes[j] = nLines - i - 1;
                break;
            }

            if (!i && isnan(dMemTable[i][j]))
            {
                nAppendedZeroes[j] = nLines;

                // If the current column was in the
                // list of selected column indices and the
                // first element of any column was part
                // of the range, reset the whole column
                if (currentcol >= 0 && bHasFirstLine)
                {
                    sHeadLine[j] = _lang.get("COMMON_COL") + "_" + toString((int)j + 1);

                    if (nWrittenHeadlines > j)
                        nWrittenHeadlines = j;
                }
            }
        }
    }

    nCalcCols = -1;
    nCalcLines = -1;

    if (!getLines(false) && !getCols(false))
        bValidData = false;
}


/////////////////////////////////////////////////
/// \brief This member function counts the number
/// of appended zeroes, i.e. the number of
/// invalid values, which are appended at the end
/// of the columns.
///
/// \return void
///
/////////////////////////////////////////////////
void Memory::countAppendedZeroes()
{
    for (long long int i = 0; i < nCols; i++)
    {
        nAppendedZeroes[i] = 0;

        for (long long int j = nLines-1; j >= 0; j--)
        {
            if (isnan(dMemTable[j][i]))
                nAppendedZeroes[i]++;
            else
                break;
        }
    }
}


/////////////////////////////////////////////////
/// \brief Implementation for the STD multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::std(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    mu::value_type dAvg = avg(_vLine, _vCol);
    mu::value_type dStd = 0.0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;
            else if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;
            else
                dStd += (dAvg - dMemTable[_vLine[i]][_vCol[j]]) * (dAvg - dMemTable[_vLine[i]][_vCol[j]]);
        }
    }

    return sqrt(dStd / (num(_vLine, _vCol) - 1.0));
}


/////////////////////////////////////////////////
/// \brief Implementation for the AVG multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::avg(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    return sum(_vLine, _vCol) / num(_vLine, _vCol);
}


/////////////////////////////////////////////////
/// \brief Implementation for the MAX multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::max(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    double dMax = NAN;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            if (isnan(dMax))
                dMax = dMemTable[_vLine[i]][_vCol[j]];

            if (dMax < dMemTable[_vLine[i]][_vCol[j]])
                dMax = dMemTable[_vLine[i]][_vCol[j]];
        }
    }

    return dMax;
}


/////////////////////////////////////////////////
/// \brief Implementation for the MIN multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::min(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    double dMin = NAN;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            if (isnan(dMin))
                dMin = dMemTable[_vLine[i]][_vCol[j]];

            if (dMin > dMemTable[_vLine[i]][_vCol[j]])
                dMin = dMemTable[_vLine[i]][_vCol[j]];
        }
    }

    return dMin;
}


/////////////////////////////////////////////////
/// \brief Implementation for the PRD multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::prd(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    mu::value_type dPrd = 1.0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            dPrd *= dMemTable[_vLine[i]][_vCol[j]];
        }
    }

    return dPrd;
}


/////////////////////////////////////////////////
/// \brief Implementation for the SUM multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::sum(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    mu::value_type dSum = 0.0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            dSum += dMemTable[_vLine[i]][_vCol[j]];
        }
    }

    return dSum;
}


/////////////////////////////////////////////////
/// \brief Implementation for the NUM multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::num(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return 0;
    int nInvalid = 0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                nInvalid++;
            else if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                nInvalid++;
        }
    }

    return (_vLine.size() * _vCol.size()) - nInvalid;
}


/////////////////////////////////////////////////
/// \brief Implementation for the AND multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::and_func(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return 0.0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    double dRetVal = NAN;

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dRetVal))
                dRetVal = 1.0;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]) || dMemTable[_vLine[i]][_vCol[j]] == 0)
                return 0.0;
        }
    }

    if (isnan(dRetVal))
        return 0.0;

    return 1.0;
}


/////////////////////////////////////////////////
/// \brief Implementation for the OR multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::or_func(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return 0.0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]) || dMemTable[_vLine[i]][_vCol[j]] != 0)
                return 1.0;
        }
    }

    return 0.0;
}


/////////////////////////////////////////////////
/// \brief Implementation for the XOR multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::xor_func(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return 0.0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    bool isTrue = false;

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]) || dMemTable[_vLine[i]][_vCol[j]] != 0)
            {
                if (!isTrue)
                    isTrue = true;
                else
                    return 0.0;
            }
        }
    }

    if (isTrue)
        return 1.0;

    return 0.0;
}


/////////////////////////////////////////////////
/// \brief Implementation for the CNT multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::cnt(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return 0;
    int nInvalid = 0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= nLines || _vCol[j] < 0 || _vCol[j] >= nCols)
                nInvalid++;
        }
    }

    return (_vLine.size() * _vCol.size()) - nInvalid;
}


/////////////////////////////////////////////////
/// \brief Implementation for the NORM multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::norm(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    mu::value_type dNorm = 0.0;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            dNorm += dMemTable[_vLine[i]][_vCol[j]] * dMemTable[_vLine[i]][_vCol[j]];
        }
    }

    return sqrt(dNorm);
}


/////////////////////////////////////////////////
/// \brief Implementation for the CMP multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \param dRef mu::value_type
/// \param _nType int
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::cmp(const VectorIndex& _vLine, const VectorIndex& _vCol, mu::value_type dRef, int _nType) const
{
    if (!bValidData)
        return NAN;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    enum
    {
        RETURN_VALUE = 1,
        RETURN_LE = 2,
        RETURN_GE = 4,
        RETURN_FIRST = 8
    };

    int nType = 0;

    double dKeep = dRef.real();
    int nKeep = -1;

    if (_nType > 0)
        nType = RETURN_GE;
    else if (_nType < 0)
        nType = RETURN_LE;

    switch (intCast(fabs(_nType)))
    {
        case 2:
            nType |= RETURN_VALUE;
            break;
        case 3:
            nType |= RETURN_FIRST;
            break;
        case 4:
            nType |= RETURN_FIRST | RETURN_VALUE;
            break;
    }

    for (long long int i = 0; i < _vLine.size(); i++)
    {
        for (long long int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols)
                continue;

            if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            if (dMemTable[_vLine[i]][_vCol[j]] == dRef.real())
            {
                if (nType & RETURN_VALUE)
                    return dMemTable[_vLine[i]][_vCol[j]];

                if (_vLine[0] == _vLine[_vLine.size() - 1])
                    return _vCol[j] + 1;

                return _vLine[i] + 1;
            }
            else if (nType & RETURN_GE && dMemTable[_vLine[i]][_vCol[j]] > dRef.real())
            {
                if (nType & RETURN_FIRST)
                {
                    if (nType & RETURN_VALUE)
                        return dMemTable[_vLine[i]][_vCol[j]];

                    if (_vLine[0] == _vLine[_vLine.size() - 1])
                        return _vCol[j] + 1;

                    return _vLine[i] + 1;
                }

                if (nKeep == -1 || dMemTable[_vLine[i]][_vCol[j]] < dKeep)
                {
                    dKeep = dMemTable[_vLine[i]][_vCol[j]];
                    if (_vLine[0] == _vLine[_vLine.size() - 1])
                        nKeep = _vCol[j];
                    else
                        nKeep = _vLine[i];
                }
                else
                    continue;
            }
            else if (nType & RETURN_LE && dMemTable[_vLine[i]][_vCol[j]] < dRef.real())
            {
                if (nType & RETURN_FIRST)
                {
                    if (nType & RETURN_VALUE)
                        return dMemTable[_vLine[i]][_vCol[j]];

                    if (_vLine[0] == _vLine[_vLine.size() - 1])
                        return _vCol[j] + 1;

                    return _vLine[i] + 1;
                }

                if (nKeep == -1 || dMemTable[_vLine[i]][_vCol[j]] > dKeep)
                {
                    dKeep = dMemTable[_vLine[i]][_vCol[j]];
                    if (_vLine[0] == _vLine[_vLine.size() - 1])
                        nKeep = _vCol[j];
                    else
                        nKeep = _vLine[i];
                }
                else
                    continue;
            }
        }
    }

    if (nKeep == -1)
        return NAN;
    else if (nType & RETURN_VALUE)
        return dKeep;
    else
        return nKeep + 1;
}


/////////////////////////////////////////////////
/// \brief Implementation for the MED multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::med(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    if (!bValidData)
        return NAN;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    vector<double> vData;

    vData.reserve(_vLine.size()*_vCol.size());

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols || isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            vData.push_back(dMemTable[_vLine[i]][_vCol[j]]);
        }
    }

    if (!vData.size())
        return NAN;

    size_t nCount = qSortDouble(&vData[0], vData.size());

    if (!nCount)
        return NAN;

    return gsl_stats_median_from_sorted_data(&vData[0], 1, nCount);
}


/////////////////////////////////////////////////
/// \brief Implementation for the PCT multi
/// argument function.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \param dPct mu::value_type
/// \return mu::value_type
///
/////////////////////////////////////////////////
mu::value_type Memory::pct(const VectorIndex& _vLine, const VectorIndex& _vCol, mu::value_type dPct) const
{
    if (!bValidData)
        return NAN;

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vLine.setOpenEndIndex(lines-1);
    _vCol.setOpenEndIndex(cols-1);

    vector<double> vData;

    vData.reserve(_vLine.size()*_vCol.size());

    if (dPct.real() >= 1 || dPct.real() <= 0)
        return NAN;

    for (unsigned int i = 0; i < _vLine.size(); i++)
    {
        for (unsigned int j = 0; j < _vCol.size(); j++)
        {
            if (_vLine[i] < 0 || _vLine[i] >= lines || _vCol[j] < 0 || _vCol[j] >= cols || isnan(dMemTable[_vLine[i]][_vCol[j]]))
                continue;

            vData.push_back(dMemTable[_vLine[i]][_vCol[j]]);
        }
    }

    if (!vData.size())
        return NAN;


    size_t nCount = qSortDouble(&vData[0], vData.size());

    if (!nCount)
        return NAN;

    return gsl_stats_quantile_from_sorted_data(&vData[0], 1, nCount, dPct.real());
}


/////////////////////////////////////////////////
/// \brief Implementation of the SIZE multi
/// argument function.
///
/// \param _vIndex const VectorIndex&
/// \param dir int Bitcomposition of AppDir values
/// \return vector<mu::value_type>
///
/////////////////////////////////////////////////
vector<mu::value_type> Memory::size(const VectorIndex& _vIndex, int dir) const
{
    if (!bValidData)
        return vector<mu::value_type>(2, 0.0);

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vIndex.setOpenEndIndex(dir & LINES ? lines-1 : cols-1);
    long long int nGridOffset = 2*((dir & GRID) != 0);

    // Handle simple things first
    if (dir == ALL)
        return vector<mu::value_type>({lines, cols});
    else if (dir == GRID)
        return vector<mu::value_type>({lines, cols-2});
    else if (dir & LINES)
    {
        // Compute the sizes of the table rows
        vector<mu::value_type> vSizes;

        for (size_t i = 0; i < _vIndex.size(); i++)
        {
            if (_vIndex[i] < 0 || _vIndex[i] >= lines)
                continue;

            for (long long int j = nCols-1; j >= 0; j--)
            {
                if (!isnan(dMemTable[_vIndex[i]][j]))
                {
                    vSizes.push_back(j+1 - nGridOffset);
                    break;
                }
            }
        }

        if (!vSizes.size())
            vSizes.push_back(NAN);

        return vSizes;
    }
    else if (dir & COLS)
    {
        // Compute the sizes of the table columns
        vector<mu::value_type> vSizes;

        for (size_t j = 0; j < _vIndex.size(); j++)
        {
            if (_vIndex[j] < nGridOffset || _vIndex[j] >= cols)
                continue;

            vSizes.push_back(nLines-getAppendedZeroes(_vIndex[j]));
        }

        if (!vSizes.size())
            vSizes.push_back(NAN);

        return vSizes;
    }

    return vector<mu::value_type>(2, 0.0);
}


/////////////////////////////////////////////////
/// \brief Implementation of the MINPOS multi
/// argument function.
///
/// \param _vIndex const VectorIndex&
/// \param dir int
/// \return vector<mu::value_type>
///
/////////////////////////////////////////////////
vector<mu::value_type> Memory::minpos(const VectorIndex& _vIndex, int dir) const
{
    if (!bValidData)
        return vector<mu::value_type>(1, NAN);

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vIndex.setOpenEndIndex(dir & COLS ? cols-1 : lines-1);
    long long int nGridOffset = 2*((dir & GRID) != 0);

    // A special case for the columns. We will compute the
    // results for ALL and GRID using the results for LINES
    if (dir & COLS)
    {
        vector<mu::value_type> vPos;

        for (size_t j = 0; j < _vIndex.size(); j++)
        {
            if (_vIndex[j] < nGridOffset || _vIndex[j] >= cols)
                continue;

            vPos.push_back(cmp(VectorIndex(0, VectorIndex::OPEN_END), VectorIndex(_vIndex[j]), min(VectorIndex(0, VectorIndex::OPEN_END), VectorIndex(_vIndex[j])), 0));
        }

        if (!vPos.size())
            vPos.push_back(NAN);

        return vPos;
    }

    vector<mu::value_type> vPos;
    double dMin = NAN;
    size_t pos = 0;

    // Compute the results for LINES and find as
    // well the global minimal value, which will be used
    // for GRID and ALL
    for (size_t i = 0; i < _vIndex.size(); i++)
    {
        if (_vIndex[i] < 0 || _vIndex[i] >= lines)
            continue;

        vPos.push_back(cmp(VectorIndex(_vIndex[i]), VectorIndex(nGridOffset, VectorIndex::OPEN_END), min(VectorIndex(_vIndex[i]), VectorIndex(nGridOffset, VectorIndex::OPEN_END)), 0));

        if (isnan(dMin) || dMin > dMemTable[_vIndex[i]][intCast(vPos.back())-1])
        {
            dMin = dMemTable[_vIndex[i]][intCast(vPos.back())-1];
            pos = i;
        }
    }

    if (!vPos.size())
        return vector<mu::value_type>(1, NAN);

    // Use the global minimal value for ALL and GRID
    if (dir == ALL || dir == GRID)
        return vector<mu::value_type>({_vIndex[pos]+1, vPos[pos]});

    return vPos;
}


/////////////////////////////////////////////////
/// \brief Implementation of the MAXPOS multi
/// argument function.
///
/// \param _vIndex const VectorIndex&
/// \param dir int
/// \return vector<mu::value_type>
///
/////////////////////////////////////////////////
vector<mu::value_type> Memory::maxpos(const VectorIndex& _vIndex, int dir) const
{
    if (!bValidData)
        return vector<mu::value_type>(1, NAN);

    long long int lines = getLines(false);
    long long int cols = getCols(false);

    _vIndex.setOpenEndIndex(dir & COLS ? cols-1 : lines-1);
    long long int nGridOffset = 2*((dir & GRID) != 0);

    // A special case for the columns. We will compute the
    // results for ALL and GRID using the results for LINES
    if (dir & COLS)
    {
        vector<mu::value_type> vPos;

        for (size_t j = 0; j < _vIndex.size(); j++)
        {
            if (_vIndex[j] < nGridOffset || _vIndex[j] >= cols)
                continue;

            vPos.push_back(cmp(VectorIndex(0, VectorIndex::OPEN_END), VectorIndex(_vIndex[j]), max(VectorIndex(0, VectorIndex::OPEN_END), VectorIndex(_vIndex[j])), 0));
        }

        if (!vPos.size())
            vPos.push_back(NAN);

        return vPos;
    }

    vector<mu::value_type> vPos;
    double dMax = NAN;
    size_t pos;

    // Compute the results for LINES and find as
    // well the global maximal value, which will be used
    // for GRID and ALL
    for (size_t i = 0; i < _vIndex.size(); i++)
    {
        if (_vIndex[i] < 0 || _vIndex[i] >= lines)
            continue;

        vPos.push_back(cmp(VectorIndex(_vIndex[i]), VectorIndex(nGridOffset, VectorIndex::OPEN_END), max(VectorIndex(_vIndex[i]), VectorIndex(nGridOffset, VectorIndex::OPEN_END)), 0));

        if (isnan(dMax) || dMax < dMemTable[_vIndex[i]][intCast(vPos.back())-1])
        {
            dMax = dMemTable[_vIndex[i]][intCast(vPos.back())-1];
            pos = i;
        }
    }

    if (!vPos.size())
        return vector<mu::value_type>(1, NAN);

    // Use the global maximal value for ALL and GRID
    if (dir == ALL || dir == GRID)
        return vector<mu::value_type>({_vIndex[pos]+1, vPos[pos]});

    return vPos;
}


/////////////////////////////////////////////////
/// \brief This method is the retouching main
/// method. It will redirect the control into the
/// specialized member functions.
///
/// \param _vLine VectorIndex
/// \param _vCol VectorIndex
/// \param Direction AppDir
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::retouch(VectorIndex _vLine, VectorIndex _vCol, AppDir Direction)
{
    bool bUseAppendedZeroes = false;

    if (!bValidData)
        return false;

    if (!_vLine.isValid() || !_vCol.isValid())
        return false;

    // Evaluate the indices
    if (_vLine.isOpenEnd())
        bUseAppendedZeroes = true;

    _vLine.setRange(0, nLines-1);
    _vCol.setRange(0, nCols-1);

    if ((Direction == ALL || Direction == GRID) && _vLine.size() < 4)
        Direction = LINES;

    if ((Direction == ALL || Direction == GRID) && _vCol.size() < 4)
        Direction = COLS;

    if (bUseAppendedZeroes)
    {
        long long int nMax = 0;

        for (size_t j = 0; j < _vCol.size(); j++)
        {
            if (nMax < nLines - nAppendedZeroes[_vCol[j]] - 1)
                nMax = nLines - nAppendedZeroes[_vCol[j]] - 1;
        }

        _vLine.setRange(0, nMax);
    }

    // Pre-evaluate the axis values in the GRID case
    if (Direction == GRID)
    {
        if (bUseAppendedZeroes)
        {
            if (!retouch(_vLine, VectorIndex(_vCol[0]), COLS) || !retouch(_vLine, VectorIndex(_vCol[1]), COLS))
                return false;
        }
        else
        {
            if (!retouch(_vLine, _vCol.subidx(0, 2), COLS))
                return false;
        }

        _vCol = _vCol.subidx(2);
    }

    // Redirect the control to the specialized member
    // functions
    if (Direction == ALL || Direction == GRID)
    {
        _vLine.linearize();
        _vCol.linearize();

        return retouch2D(_vLine, _vCol);
    }
    else
        return retouch1D(_vLine, _vCol, Direction);
}


/////////////////////////////////////////////////
/// \brief This member function retouches single
/// dimension data (along columns or rows).
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \param Direction AppDir
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::retouch1D(const VectorIndex& _vLine, const VectorIndex& _vCol, AppDir Direction)
{
    bool markModified = false;

    if (Direction == LINES)
    {
        for (size_t i = 0; i < _vLine.size(); i++)
        {
            for (size_t j = 0; j < _vCol.size(); j++)
            {
                if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                {
                    for (size_t _j = j; _j < _vCol.size(); _j++)
                    {
                        if (!isnan(dMemTable[_vLine[i]][_vCol[_j]]))
                        {
                            if (j)
                            {
                                for (size_t __j = j; __j < _j; __j++)
                                {
                                    dMemTable[_vLine[i]][_vCol[__j]] =
                                        (dMemTable[_vLine[i]][_vCol[_j]] - dMemTable[_vLine[i]][_vCol[j-1]]) / (double)(_j - j) * (double)(__j - j + 1) + dMemTable[_vLine[i]][_vCol[j-1]];
                                }

                                markModified = true;
                                break;
                            }
                            else if (_j+1 < _vCol.size())
                            {
                                for (size_t __j = j; __j < _j; __j++)
                                {
                                    dMemTable[_vLine[i]][_vCol[__j]] = dMemTable[_vLine[i]][_vCol[_j]];
                                }

                                markModified = true;
                                break;
                            }
                        }

                        if (j && _j+1 == _vCol.size() && isnan(dMemTable[_vLine[i]][_vCol[_j]]))
                        {
                            for (size_t __j = j; __j < _vCol.size(); __j++)
                            {
                                dMemTable[_vLine[i]][_vCol[__j]] = dMemTable[_vLine[i]][_vCol[j - 1]];
                            }

                            markModified = true;
                        }
                    }
                }
            }
        }
    }
    else if (Direction == COLS)
    {
        for (size_t j = 0; j < _vCol.size(); j++)
        {
            for (size_t i = 0; i < _vLine.size(); i++)
            {
                if (isnan(dMemTable[_vLine[i]][_vCol[j]]))
                {
                    for (size_t _i = i; _i < _vLine.size(); _i++)
                    {
                        if (!isnan(dMemTable[_vLine[_i]][_vCol[j]]))
                        {
                            if (i)
                            {
                                for (size_t __i = i; __i < _i; __i++)
                                {
                                    dMemTable[_vLine[__i]][_vCol[j]] =
                                        (dMemTable[_vLine[_i]][_vCol[j]] - dMemTable[_vLine[i-1]][_vCol[j]]) / (double)(_i - i) * (double)(__i - i + 1) + dMemTable[_vLine[i-1]][_vCol[j]];
                                }

                                markModified = true;
                                break;
                            }
                            else if (_i+1 < _vLine.size())
                            {
                                for (size_t __i = i; __i < _i; __i++)
                                {
                                    dMemTable[_vLine[__i]][_vCol[j]] = dMemTable[_vLine[_i]][_vCol[j]];
                                }

                                markModified = true;
                                break;
                            }
                        }

                        if (i  && _i+1 == _vLine.size() && isnan(dMemTable[_vLine[_i]][_vCol[j]]))
                        {
                            for (size_t __i = i; __i < _vLine.size(); __i++)
                            {
                                dMemTable[_vLine[__i]][_vCol[j]] = dMemTable[_vLine[i-1]][_vCol[j]];
                            }

                            markModified = true;
                        }
                    }
                }
            }
        }
    }

    if (markModified && bIsSaved)
    {
        bIsSaved = false;
        nLastSaved = time(0);
    }

    return true;
}


/////////////////////////////////////////////////
/// \brief This member function retouches two
/// dimensional data (using a specialized filter
/// class instance).
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::retouch2D(const VectorIndex& _vLine, const VectorIndex& _vCol)
{
    bool bMarkModified = false;

    for (long long int i = _vLine.front(); i <= _vLine.last(); i++)
    {
        for (long long int j = _vCol.front(); j <= _vCol.last(); j++)
        {
            if (isnan(dMemTable[i][j]))
            {
                Boundary _boundary = findValidBoundary(_vLine, _vCol, i, j);
                NumeRe::RetouchRegion _region(_boundary.rows-1, _boundary.cols-1, med(VectorIndex(_boundary.rf(), _boundary.re()), VectorIndex(_boundary.cf(), _boundary.ce())).real());

                long long int l,r,t,b;

                // Find the correct boundary to be used instead of the
                // one outside of the range (if one of the indices is on
                // any of the four boundaries
                l = _boundary.cf() < _vCol.front() ? _boundary.ce() : _boundary.cf();
                r = _boundary.ce() > _vCol.last() ? _boundary.cf() : _boundary.ce();
                t = _boundary.rf() < _vLine.front() ? _boundary.re() : _boundary.rf();
                b = _boundary.re() > _vLine.last() ? _boundary.rf() : _boundary.re();

                _region.setBoundaries(readRealMem(VectorIndex(_boundary.rf(), _boundary.re()), VectorIndex(l)),
                                      readRealMem(VectorIndex(_boundary.rf(), _boundary.re()), VectorIndex(r)),
                                      readRealMem(VectorIndex(t), VectorIndex(_boundary.cf(), _boundary.ce())),
                                      readRealMem(VectorIndex(b), VectorIndex(_boundary.cf(), _boundary.ce())));

                for (long long int _n = _boundary.rf()+1; _n < _boundary.re(); _n++)
                {
                    for (long long int _m = _boundary.cf()+1; _m < _boundary.ce(); _m++)
                        dMemTable[_n][_m] = _region.retouch(_n - _boundary.rf() - 1, _m - _boundary.cf() - 1, dMemTable[_n][_m], med(VectorIndex(_n-1, _n+1), VectorIndex(_m-1, _m+1)).real());
                }

                bMarkModified = true;
            }
        }
    }

    if (bMarkModified && bIsSaved)
    {
        bIsSaved = false;
        nLastSaved = time(0);
    }

    return true;
}


/////////////////////////////////////////////////
/// \brief This method is a wrapper for
/// detecting, whether a row or column does only
/// contain valid values (no NaNs).
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::onlyValidValues(const VectorIndex& _vLine, const VectorIndex& _vCol) const
{
    return num(_vLine, _vCol) == cnt(_vLine, _vCol);
}


/////////////////////////////////////////////////
/// \brief This member function finds the
/// smallest possible boundary around a set of
/// invalid values to be used as boundary values
/// for retouching the values.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \param i long long int
/// \param j long long int
/// \return RetouchBoundary
///
/////////////////////////////////////////////////
Boundary Memory::findValidBoundary(const VectorIndex& _vLine, const VectorIndex& _vCol, long long int i, long long int j) const
{
    Boundary _boundary(i-1, j-1, 2, 2);

    bool reEvaluateBoundaries = true;

    while (reEvaluateBoundaries)
    {
        reEvaluateBoundaries = false;

        if (!onlyValidValues(VectorIndex(_boundary.rf(), _boundary.re()), VectorIndex(_boundary.cf())) && _boundary.cf() > _vCol.front())
        {
            _boundary.m--;
            _boundary.cols++;
            reEvaluateBoundaries = true;
        }

        if (!onlyValidValues(VectorIndex(_boundary.rf(), _boundary.re()), VectorIndex(_boundary.ce())) && _boundary.ce() < _vCol.last())
        {
            _boundary.cols++;
            reEvaluateBoundaries = true;
        }

        if (!onlyValidValues(VectorIndex(_boundary.rf()), VectorIndex(_boundary.cf(), _boundary.ce())) && _boundary.rf() > _vLine.front())
        {
            _boundary.n--;
            _boundary.rows++;
            reEvaluateBoundaries = true;
        }

        if (!onlyValidValues(VectorIndex(_boundary.re()), VectorIndex(_boundary.cf(), _boundary.ce())) && _boundary.re() < _vLine.last())
        {
            _boundary.rows++;
            reEvaluateBoundaries = true;
        }
    }

    return _boundary;
}


/////////////////////////////////////////////////
/// \brief This private member function realizes
/// the application of a smoothing window to 1D
/// data sets.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \param i size_t
/// \param j size_t
/// \param _filter NumeRe::Filter*
/// \param smoothLines bool
/// \return void
///
/////////////////////////////////////////////////
void Memory::smoothingWindow1D(const VectorIndex& _vLine, const VectorIndex& _vCol, size_t i, size_t j, NumeRe::Filter* _filter, bool smoothLines)
{
    auto sizes = _filter->getWindowSize();

    double sum = 0.0;

    // Apply the filter to the data
    for (size_t n = 0; n < sizes.first; n++)
    {
        if (!_filter->isConvolution())
            writeData(_vLine[i+n*(!smoothLines)], _vCol[j+n*smoothLines], _filter->apply(n, 0, readMem(_vLine[i+n*(!smoothLines)], _vCol[j+n*smoothLines])));
        else
            sum += _filter->apply(n, 0, readMem(_vLine[i+n*(!smoothLines)], _vCol[j+n*smoothLines]));
    }

    // If the filter is a convolution, store the new value here
    if (_filter->isConvolution())
        writeData(_vLine[i + sizes.first/2*(!smoothLines)], _vCol[j + sizes.first/2*smoothLines], sum);
}


/////////////////////////////////////////////////
/// \brief This private member function realizes
/// the application of a smoothing window to 2D
/// data sets.
///
/// \param _vLine const VectorIndex&
/// \param _vCol const VectorIndex&
/// \param i size_t
/// \param j size_t
/// \param _filter NumeRe::Filter*
/// \return void
///
/////////////////////////////////////////////////
void Memory::smoothingWindow2D(const VectorIndex& _vLine, const VectorIndex& _vCol, size_t i, size_t j, NumeRe::Filter* _filter)
{
    auto sizes = _filter->getWindowSize();

    double sum = 0.0;

    // Apply the filter to the data
    for (size_t n = 0; n < sizes.first; n++)
    {
        for (size_t m = 0; m < sizes.second; m++)
        {
            if (!_filter->isConvolution())
                writeData(_vLine[i+n], _vCol[j+m], _filter->apply(n, m, readMem(_vLine[i+n], _vCol[j+m])));
            else
                sum += _filter->apply(n, m, readMem(_vLine[i+n], _vCol[j+m]));
        }
    }

    // If the filter is a convolution, store the new value here
    if (_filter->isConvolution())
        writeData(_vLine[i + sizes.first/2], _vCol[j + sizes.second/2], sum);
}


/////////////////////////////////////////////////
/// \brief This member function smoothes the data
/// described by the passed VectorIndex indices
/// using the passed FilterSettings to construct
/// the corresponding filter.
///
/// \param _vLine VectorIndex
/// \param _vCol VectorIndex
/// \param _settings NumeRe::FilterSettings
/// \param Direction AppDir
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::smooth(VectorIndex _vLine, VectorIndex _vCol, NumeRe::FilterSettings _settings, AppDir Direction)
{
    bool bUseAppendedZeroes = false;

    // Avoid the border cases
    if (!bValidData)
        throw SyntaxError(SyntaxError::NO_CACHED_DATA, "smooth", SyntaxError::invalid_position);

    if (!_vLine.isValid() || !_vCol.isValid())
        throw SyntaxError(SyntaxError::INVALID_INDEX, "smooth", SyntaxError::invalid_position, _vLine.to_string() + ", " + _vCol.to_string());

    // Evaluate the indices
    if (_vLine.isOpenEnd())
        bUseAppendedZeroes = true;

    // Force the index ranges
    _vLine.setRange(0, nLines-1);
    _vCol.setRange(0, nCols-1);

    // Change the predefined application directions, if it's needed
    if ((Direction == ALL || Direction == GRID) && _vLine.size() < 4)
        Direction = LINES;

    if ((Direction == ALL || Direction == GRID) && _vCol.size() < 4)
        Direction = COLS;

    // Check the order
    if ((_settings.row >= nLines && Direction == COLS) || (_settings.col >= nCols && Direction == LINES) || ((_settings.row >= nLines || _settings.col >= nCols) && (Direction == ALL || Direction == GRID)))
        throw SyntaxError(SyntaxError::CANNOT_SMOOTH_CACHE, "smooth", SyntaxError::invalid_position);

    // Get the appended zeros
    if (bUseAppendedZeroes)
    {
        long long int nMax = 0;

        for (size_t j = 0; j < _vCol.size(); j++)
        {
            if (nMax < nLines - nAppendedZeroes[_vCol[j]] - 1)
                nMax = nLines - nAppendedZeroes[_vCol[j]] - 1;
        }

        _vLine.setRange(0, nMax);
    }

    // If the application direction is equal to GRID, then the first two columns
    // should be evaluted separately, because they contain the axis values
    if (Direction == GRID)
    {
        // Will never return false
        if (bUseAppendedZeroes)
        {
            if (!smooth(_vLine, VectorIndex(_vCol[0]), _settings, COLS) || !smooth(_vLine, VectorIndex(_vCol[1]), _settings, COLS))
                return false;
        }
        else
        {
            if (!smooth(_vLine, _vCol.subidx(0, 2), _settings, COLS))
                return false;
        }

        _vCol = _vCol.subidx(2);
    }

    // The first job is to simply remove invalid values and then smooth the
    // framing points of the data section
    if (Direction == ALL || Direction == GRID)
    {
        // Retouch everything
        Memory::retouch(_vLine, _vCol, ALL);

        //Memory::smooth(_vLine, VectorIndex(_vCol.front()), _settings, COLS);
        //Memory::smooth(_vLine, VectorIndex(_vCol.last()), _settings, COLS);
        //Memory::smooth(VectorIndex(_vLine.front()), _vCol, _settings, LINES);
        //Memory::smooth(VectorIndex(_vLine.last()), _vCol, _settings, LINES);

        if (_settings.row == 1u && _settings.col != 1u)
            _settings.row = _settings.col;
        else if (_settings.row != 1u && _settings.col == 1u)
            _settings.col = _settings.row;
    }
    else
    {
        _settings.row = std::max(_settings.row, _settings.col);
        _settings.col = 1u;
    }

    if (isnan(_settings.alpha))
        _settings.alpha = 1.0;

    // Apply the actual smoothing of the data
    if (Direction == LINES)
    {
        // Create a filter from the filter settings
        std::unique_ptr<NumeRe::Filter> _filterPtr(NumeRe::createFilter(_settings));

        // Update the sizes, because they might be
        // altered by the filter constructor
        auto sizes = _filterPtr.get()->getWindowSize();
        _settings.row = sizes.first;

        // Pad the beginning and the of the vector with multiple copies
        _vCol.prepend(vector<long long int>(_settings.row/2+1, _vCol.front()));
        _vCol.append(vector<long long int>(_settings.row/2+1, _vCol.last()));

        // Smooth the lines
        for (size_t i = 0; i < _vLine.size(); i++)
        {
            for (size_t j = 1; j < _vCol.size() - _settings.row-1; j++)
            {
                smoothingWindow1D(_vLine, _vCol, i, j, _filterPtr.get(), true);
            }
        }
    }
    else if (Direction == COLS)
    {
        // Create a filter from the settings
        std::unique_ptr<NumeRe::Filter> _filterPtr(NumeRe::createFilter(_settings));

        // Update the sizes, because they might be
        // altered by the filter constructor
        auto sizes = _filterPtr.get()->getWindowSize();
        _settings.row = sizes.first;

        // Pad the beginning and end of the vector with multiple copies
        _vLine.prepend(vector<long long int>(_settings.row/2+1, _vLine.front()));
        _vLine.append(vector<long long int>(_settings.row/2+1, _vLine.last()));

        // Smooth the columns
        for (size_t j = 0; j < _vCol.size(); j++)
        {
            for (size_t i = 1; i < _vLine.size() - _settings.row-1; i++)
            {
                smoothingWindow1D(_vLine, _vCol, i, j, _filterPtr.get(), false);
            }
        }
    }
    else if ((Direction == ALL || Direction == GRID) && _vLine.size() > 2 && _vCol.size() > 2)
    {
        // Create a filter from the settings
        std::unique_ptr<NumeRe::Filter> _filterPtr(NumeRe::createFilter(_settings));

        // Update the sizes, because they might be
        // altered by the filter constructor
        auto sizes = _filterPtr.get()->getWindowSize();
        _settings.row = sizes.first;
        _settings.col = sizes.second;

        // Pad the beginning and end of both vectors
        // with a mirrored copy of themselves
        std::vector<long long int> vMirror = _vLine.subidx(1, _settings.row/2+1).getVector();
        _vLine.prepend(vector<long long int>(vMirror.rbegin(), vMirror.rend()));

        vMirror = _vLine.subidx(_vLine.size() - _settings.row/2-2, _settings.row/2+1).getVector();
        _vLine.append(vector<long long int>(vMirror.rbegin(), vMirror.rend()));

        vMirror = _vCol.subidx(1, _settings.col/2+1).getVector();
        _vCol.prepend(vector<long long int>(vMirror.rbegin(), vMirror.rend()));

        vMirror = _vCol.subidx(_vCol.size() - _settings.col/2-2, _settings.row/2+1).getVector();
        _vCol.append(vector<long long int>(vMirror.rbegin(), vMirror.rend()));

        // Smooth the data in two dimensions, if that is reasonable
        // Go through every point
        for (size_t i = 1; i < _vLine.size() - _settings.row-1; i++)
        {
            for (size_t j = 1; j < _vCol.size() - _settings.col-1; j++)
            {
                smoothingWindow2D(_vLine, _vCol, i, j, _filterPtr.get());
            }
        }
    }

    if (bIsSaved)
    {
        bIsSaved = false;
        nLastSaved = time(0);
    }

    return true;
}


/////////////////////////////////////////////////
/// \brief This member function resamples the
/// data described by the passed coordinates
/// using the new samples nSamples.
///
/// \param _vLine VectorIndex
/// \param _vCol VectorIndex
/// \param nSamples unsigned int
/// \param Direction AppDir
/// \return bool
///
/////////////////////////////////////////////////
bool Memory::resample(VectorIndex _vLine, VectorIndex _vCol, unsigned int nSamples, AppDir Direction)
{
    bool bUseAppendedZeroes = false;

    const long long int __nOrigLines = nLines;
    const long long int __nOrigCols = nCols;

    long long int __nLines = nLines;
    long long int __nCols = nCols;

    // Avoid border cases
    if (!bValidData)
        throw SyntaxError(SyntaxError::NO_CACHED_DATA, "resample", SyntaxError::invalid_position);

    if (!nSamples)
        throw SyntaxError(SyntaxError::CANNOT_RESAMPLE_CACHE, "resample", SyntaxError::invalid_position);

    if (!_vLine.isValid() || !_vCol.isValid())
        throw SyntaxError(SyntaxError::INVALID_INDEX, "resample", SyntaxError::invalid_position, _vLine.to_string() + ", " + _vCol.to_string());

    // Evaluate the indices
    if (_vCol.isOpenEnd())
        bUseAppendedZeroes = true;

    _vLine.setRange(0, nLines-1);
    _vLine.linearize();
    _vCol.setRange(0, nCols-1);

    // Change the predefined application directions, if it's needed
    if ((Direction == ALL || Direction == GRID) && _vLine.size() < 4)
        Direction = LINES;

    if ((Direction == ALL || Direction == GRID) && _vCol.size() < 4)
        Direction = COLS;

    // Get the appended zeros
    if (bUseAppendedZeroes)
    {
        long long int nMax = 0;

        for (size_t j = 0; j < _vCol.size(); j++)
        {
            if (nMax < nLines - nAppendedZeroes[_vCol[j]] - 1)
                nMax = nLines - nAppendedZeroes[_vCol[j]] - 1;
        }

        _vLine.setRange(0, nMax);
    }

    // If the application direction is equal to GRID, then the indices should
    // match a sufficiently enough large data array
    if (Direction == GRID)
    {
        if (_vCol.size() - 2 != _vLine.size() && !bUseAppendedZeroes)
            throw SyntaxError(SyntaxError::CANNOT_RESAMPLE_CACHE, "resample", SyntaxError::invalid_position);
        else if (_vCol.size() - 2 != (nLines - nAppendedZeroes[_vCol[1]] - 1) - _vLine.front() && bUseAppendedZeroes)
            throw SyntaxError(SyntaxError::CANNOT_RESAMPLE_CACHE, "resample", SyntaxError::invalid_position);
    }

    // Prepare a pointer to the resampler object
    Resampler* _resampler = nullptr;

    // Create the actual resample object based upon the application direction.
    // Additionally determine the size of the resampling buffer, which might
    // be larger than the current data set
    if (Direction == ALL || Direction == GRID) // 2D
    {
        if (Direction == GRID)
        {
            // Apply the resampling to the first two columns first:
            // These contain the axis values
            if (bUseAppendedZeroes)
            {
                resample(_vLine, VectorIndex(_vCol[0]), nSamples, COLS);
                resample(_vLine, VectorIndex(_vCol[1]), nSamples, COLS);
            }
            else
            {
                // Achsenwerte getrennt resamplen
                resample(_vLine, _vCol.subidx(0, 2), nSamples, COLS);
            }

            // Increment the first column
            _vCol = _vCol.subidx(2);
            _vCol.linearize();

            // Determine the size of the buffer
            if (nSamples > _vLine.size())
                __nLines += nSamples - _vLine.size();

            if (nSamples > _vCol.size())
                __nCols += nSamples - _vCol.size();
        }

        if (bUseAppendedZeroes)
        {
            long long int nMax = 0;

            for (size_t j = 0; j < _vCol.size(); j++)
            {
                if (nMax < nLines - nAppendedZeroes[_vCol[j]] - 1)
                    nMax = nLines - nAppendedZeroes[_vCol[j]] - 1;
            }

            _vLine.setRange(0, nMax);
        }

        // Create the resample object and prepare the needed memory
        _resampler = new Resampler(_vCol.size(), _vLine.size(), nSamples, nSamples, Resampler::BOUNDARY_CLAMP, 1.0, 0.0, "lanczos6");

        // Determine final size (only upscale)
        if (nSamples > _vLine.size() || nSamples > _vCol.size())
            resizeMemory(nLines + nSamples - _vLine.size(), nCols + nSamples - _vCol.size());
    }
    else if (Direction == COLS) // cols
    {
        _vCol.linearize();

        // Create the resample object and prepare the needed memory
        _resampler = new Resampler(_vCol.size(), _vLine.size(), _vCol.size(), nSamples, Resampler::BOUNDARY_CLAMP, 1.0, 0.0, "lanczos6");

        // Determine final size (only upscale)
        if (nSamples > _vLine.size())
		{
            resizeMemory(nLines + nSamples - _vLine.size(), nCols - 1);
			// Determine the size of the buffer
            __nLines += nSamples - _vLine.size();
		}
    }
    else if (Direction == LINES)// lines
    {
        // Create the resample object and prepare the needed memory
        _resampler = new Resampler(_vCol.size(), _vLine.size(), nSamples, _vLine.size(), Resampler::BOUNDARY_CLAMP, 1.0, 0.0, "lanczos6");

        // Determine final size (only upscale)
        if (nSamples > _vCol.size())
		{
            resizeMemory(nLines - 1, nCols + nSamples - _vCol.size());
			// Determine the size of the buffer
            __nCols += nSamples - _vCol.size();
		}
    }

    // Ensure that the resampler was created
    if (!_resampler)
    {
        throw SyntaxError(SyntaxError::INTERNAL_RESAMPLER_ERROR, "resample", SyntaxError::invalid_position);
    }

    // Create and initalize the dynamic memory: resampler buffer
    double** dResampleBuffer = new double*[__nLines];

    for (long long int i = 0; i < __nLines; i++)
    {
        dResampleBuffer[i] = new double[__nCols];

        for (long long int j = 0; j < __nCols; j++)
            dResampleBuffer[i][j] = NAN;
    }

    // resampler output buffer
    const double* dOutputSamples = 0;
    double* dInputSamples = new double[_vCol.size()];
    long long int _ret_line = 0;
    long long int _final_cols = 0;

    // Determine the number of final columns. These will stay constant only in
    // the column application direction
    if (Direction == ALL || Direction == GRID || Direction == LINES)
        _final_cols = nSamples;
    else
        _final_cols = _vCol.size();

    // Copy the whole memory
    for (long long int i = 0; i < __nOrigLines; i++)
    {
        for (long long int j = 0; j < __nOrigCols; j++)
        {
            dResampleBuffer[i][j] = dMemTable[i][j];
        }
    }

    // Resample the data table
    // Apply the resampling linewise
    for (size_t i = 0; i < _vLine.size(); i++)
    {
        for (size_t j = 0; j < _vCol.size(); j++)
        {
            dInputSamples[j] = dMemTable[_vLine[i]][_vCol[j]];
        }

        // If the resampler doesn't accept a further line
        // the buffer is probably full
        if (!_resampler->put_line(dInputSamples))
        {
            if (_resampler->status() != Resampler::STATUS_SCAN_BUFFER_FULL)
            {
                // Obviously not the case
                // Clear the memory and return
                delete _resampler;

                for (long long int _i = 0; _i < __nLines; _i++)
                    delete[] dResampleBuffer[_i];

                delete[] dResampleBuffer;
                delete[] dInputSamples;

                throw SyntaxError(SyntaxError::INTERNAL_RESAMPLER_ERROR, "resample", SyntaxError::invalid_position);
            }
            else if (_resampler->status() == Resampler::STATUS_SCAN_BUFFER_FULL)
            {
                // Free the scan buffer of the resampler by extracting the already resampled lines
                while (true)
                {
                    dOutputSamples = _resampler->get_line();

                    // dOutputSamples will be a nullptr, if no more resampled
                    // lines are available
                    if (!dOutputSamples)
                        break;

                    for (long long int _fin = 0; _fin < _final_cols; _fin++)
                    {
                        if (isnan(dOutputSamples[_fin]))
                        {
                            dResampleBuffer[_vLine.front() + _ret_line][_vCol.front() + _fin] = NAN;
                            continue;
                        }

                        dResampleBuffer[_vLine.front() + _ret_line][_vCol.front() + _fin] = dOutputSamples[_fin];
                    }

                    _ret_line++;
                }

                // Try again to put the current line
                _resampler->put_line(dInputSamples);
            }
        }
    }

    // Clear the input sample memory
    delete[] dInputSamples;

    // Extract the remaining resampled lines from the resampler's memory
    while (true)
    {
        dOutputSamples = _resampler->get_line();

        // dOutputSamples will be a nullptr, if no more resampled
        // lines are available
        if (!dOutputSamples)
            break;

        for (long long int _fin = 0; _fin < _final_cols; _fin++)
        {
            if (isnan(dOutputSamples[_fin]))
            {
                dResampleBuffer[_vLine.front() + _ret_line][_vCol.front() + _fin] = NAN;
                continue;
            }

            dResampleBuffer[_vLine.front() + _ret_line][_vCol.front() + _fin] = dOutputSamples[_fin];
        }

        _ret_line++;
    }

    //_ret_line++;

    // Delete the resampler: it is not used any more
    delete _resampler;

    // Reset the calculated lines and columns
    nCalcCols = -1;
    nCalcLines = -1;

    // Block unter dem resampleten kopieren
    if (_vLine.size() < nSamples && (Direction == ALL || Direction == GRID || Direction == COLS))
    {
        for (long long int i = _vLine.last() + 1; i < __nOrigLines; i++)
        {
            for (size_t j = 0; j < _vCol.size(); j++)
            {
                if (_ret_line + i - (_vLine.last() + 1) + _vLine.front() >= nLines)
                {
                    for (long long int _i = 0; _i < __nLines; _i++)
                        delete[] dResampleBuffer[_i];

                    delete[] dResampleBuffer;

                    throw SyntaxError(SyntaxError::INTERNAL_RESAMPLER_ERROR, "resample", SyntaxError::invalid_position);
                }

                if (isnan(dMemTable[i][_vCol[j]]))
                {
                    dResampleBuffer[_ret_line + i - (_vLine.last() + 1) + _vLine.front()][_vCol[j]] = NAN;
                }
                else
                {
                    dResampleBuffer[_ret_line + i - (_vLine.last() + 1) + _vLine.front()][_vCol[j]] = dMemTable[i][_vCol[j]];
                }
            }
        }
    }
    else if (_vLine.size() > nSamples && (Direction == ALL || Direction == GRID || Direction == COLS))
    {
        for (size_t i = nSamples - 1; i < _vLine.size(); i++)
        {
            for (size_t j = 0; j < _vCol.size(); j++)
            {
                if (_vLine[i] >= nLines)
                {
                    for (long long int _i = 0; _i < __nLines; _i++)
                        delete[] dResampleBuffer[_i];

                    delete[] dResampleBuffer;

                    throw SyntaxError(SyntaxError::INTERNAL_RESAMPLER_ERROR, "resample", SyntaxError::invalid_position);
                }

                dResampleBuffer[_vLine[i]][_vCol[j]] = NAN;
            }
        }
    }

    // Block rechts kopieren
    if (_vCol.size() < nSamples && (Direction == ALL || Direction == GRID || Direction == LINES))
    {
        for (long long int i = 0; i < __nOrigLines; i++)
        {
            for (long long int j = _vCol.last() + 1; j < __nOrigCols; j++)
            {
                if (_final_cols + j - (_vCol.last() + 1) + _vCol.front() >= nCols)
                {
                    for (long long int _i = 0; _i < __nLines; _i++)
                        delete[] dResampleBuffer[_i];

                    delete[] dResampleBuffer;

                    throw SyntaxError(SyntaxError::INTERNAL_RESAMPLER_ERROR, "resample", SyntaxError::invalid_position);
                }

                if (isnan(dMemTable[i][j]))
                {
                    dResampleBuffer[i][_final_cols + j - (_vCol.last() + 1) + _vCol.front()] = NAN;
                }
                else
                {
                    dResampleBuffer[i][_final_cols + j - (_vCol.last() + 1) + _vCol.front()] = dMemTable[i][j];
                }
            }
        }
    }
    else if (_vCol.size() > nSamples && (Direction == ALL || Direction == GRID || Direction == LINES))
    {
        for (size_t i = 0; i < _vLine.size(); i++)
        {
            for (size_t j = nSamples - 1; j < _vCol.size(); j++)
            {
                if (_vCol[j] >= nCols)
                {
                    for (long long int _i = 0; _i < __nLines; _i++)
                        delete[] dResampleBuffer[_i];

                    delete[] dResampleBuffer;

                    throw SyntaxError(SyntaxError::INTERNAL_RESAMPLER_ERROR, "resample", SyntaxError::invalid_position);
                }

                dResampleBuffer[_vLine[i]][_vCol[j]] = NAN;
            }
        }
    }

    // After all data is restored successfully
    // copy the data points from the buffer back to their original state
    for (long long int i = 0; i < nLines; i++)
    {
        if (i >= __nLines)
            break;

        for (long long int j = 0; j < nCols; j++)
        {
            if (j >= __nCols)
                break;

            dMemTable[i][j] = dResampleBuffer[i][j];
        }
    }


    // appended zeroes zaehlen
    for (long long int j = 0; j < nCols; j++)
    {
        for (long long int i = nLines; i >= 0; i--)
        {
            if (i == nLines)
                nAppendedZeroes[j] = 0;
            else if (isnan(dMemTable[i][j]))
                nAppendedZeroes[j]++;
            else
                break;

        }
    }

    // Clear unused memory
    for (long long int i = 0; i < __nLines; i++)
        delete[] dResampleBuffer[i];

    delete[] dResampleBuffer;

    if (bIsSaved)
    {
        bIsSaved = false;
        nLastSaved = time(0);
    }

    return true;
}


