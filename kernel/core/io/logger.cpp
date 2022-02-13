/*****************************************************************************
    NumeRe: Framework fuer Numerische Rechnungen
    Copyright (C) 2021  Erik Haenel et al.

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

#include "logger.hpp"
#include "../utils/stringtools.hpp"
#include <windows.h>

DetachedLogger g_logger;

typedef BOOL (WINAPI* LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);


/////////////////////////////////////////////////
/// \brief This function returns true, if we're
/// currently running on Win x64.
///
/// \return bool
///
/////////////////////////////////////////////////
bool IsWow64()
{
    BOOL bIsWow64 = false;

    //IsWow64Process is not available on all supported versions of Windows.
    //Use GetModuleHandle to get a handle to the DLL that contains the function
    //and GetProcAddress to get a pointer to the function if available.

    LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(
            GetModuleHandle(TEXT("kernel32")), "IsWow64Process");

    if (NULL != fnIsWow64Process)
    {
        if (!fnIsWow64Process(GetCurrentProcess(), &bIsWow64))
        {
            return false;
        }
    }
    return (bool)bIsWow64;
}













/////////////////////////////////////////////////
/// \brief Empty default constructor.
/////////////////////////////////////////////////
Logger::Logger()
{
    //
}


/////////////////////////////////////////////////
/// \brief Generic constructor. Will open the
/// target file, if possible.
///
/// \param sLogFile const std::string&
///
/////////////////////////////////////////////////
Logger::Logger(const std::string& sLogFile)
{
    open(sLogFile);
}


/////////////////////////////////////////////////
/// \brief Open the target logging file for
/// writing.
///
/// \param sLogFile const std::string&
/// \return bool
///
/// \warning The logger will not validate file
/// paths.
/////////////////////////////////////////////////
bool Logger::open(const std::string& sLogFile)
{
    if (m_logFile.is_open())
        m_logFile.close();

    m_logFile.open(sLogFile, std::ios_base::out | std::ios_base::app | std::ios_base::ate);
    m_sLogFile = sLogFile;

    return m_logFile.good();
}


/////////////////////////////////////////////////
/// \brief Close the logger stream.
///
/// \return void
///
/////////////////////////////////////////////////
void Logger::close()
{
    m_logFile.close();
}


/////////////////////////////////////////////////
/// \brief Check, whether the logger stream is
/// currently open.
///
/// \return bool
///
/////////////////////////////////////////////////
bool Logger::is_open() const
{
    return m_logFile.is_open();
}


/////////////////////////////////////////////////
/// \brief Ensures that the stream is open and
/// tries to re-open it otherwise.
///
/// \return bool
///
/////////////////////////////////////////////////
bool Logger::ensure_open()
{
    if (!m_logFile.is_open() && m_sLogFile.length())
        m_logFile.open(m_sLogFile, std::ios_base::out | std::ios_base::app | std::ios_base::ate);

    return m_logFile.is_open();
}


/////////////////////////////////////////////////
/// \brief Push a message to the logger stream.
/// Will automatically re-open a file, if the
/// stream had been closed.
///
/// \param sMessage const std::string&
/// \return void
///
/////////////////////////////////////////////////
void Logger::push(const std::string& sMessage)
{
    if (ensure_open())
        m_logFile << sMessage;
}


/////////////////////////////////////////////////
/// \brief Push a line to the logger stream. The
/// stream will automatically append the line
/// termination characters.
///
/// \param sMessage const std::string&
/// \return void
///
/////////////////////////////////////////////////
void Logger::push_line(const std::string& sMessage)
{
    if (ensure_open())
        m_logFile << sMessage << std::endl;
}







/////////////////////////////////////////////////
/// \brief DetachedLogger constructor. Sets the
/// default logging level.
///
/// \param lvl Logger::LogLevel
///
/////////////////////////////////////////////////
DetachedLogger::DetachedLogger(Logger::LogLevel lvl) : m_level(lvl)
{
    //
}


/////////////////////////////////////////////////
/// \brief DetachedLogger destructor. Appends a
/// terminating message to the current logfile
/// (if any).
/////////////////////////////////////////////////
DetachedLogger::~DetachedLogger()
{
    // Write an information to the log file
    push_info("SESSION WAS TERMINATED SUCCESSFULLY\n");
}


/////////////////////////////////////////////////
/// \brief Determine, whether this instance is
/// currently buffering or directly writing to a
/// file.
///
/// \return bool
///
/////////////////////////////////////////////////
bool DetachedLogger::is_buffering() const
{
    return !is_open();
}


/////////////////////////////////////////////////
/// \brief Open the log file and push the
/// buffered messages directly to this file.
///
/// \param sLogFile const std::string&
/// \return bool
///
/////////////////////////////////////////////////
bool DetachedLogger::open(const std::string& sLogFile)
{
    bool good = Logger::open(sLogFile);

    if (!good)
        return false;

    for (size_t i = 0; i < m_buffer.size(); i++)
        Logger::push_line(m_buffer[i]);

    m_buffer.clear();

    return good;
}


/////////////////////////////////////////////////
/// \brief Change the logging level or completely
/// disable the logger.
///
/// \param lvl Logger::LogLevel
/// \return void
///
/////////////////////////////////////////////////
void DetachedLogger::setLoggingLevel(Logger::LogLevel lvl)
{
    m_level = lvl;
}


/////////////////////////////////////////////////
/// \brief Push a message to the logger, which
/// is not dependend on the logging level and
/// will be shown without a timestamp.
///
/// \param sInfo const std::string&
/// \return void
///
/////////////////////////////////////////////////
void DetachedLogger::push_info(const std::string& sInfo)
{
    if (is_buffering())
        m_buffer.push_back(sInfo);
    else
        Logger::push_line(sInfo);
}


/////////////////////////////////////////////////
/// \brief A helper function to write the current
/// OS's information to the log file.
///
/// \return void
///
/////////////////////////////////////////////////
void DetachedLogger::write_system_information()
{
    std::string sysInfo;
    // Get the version of the operating system
    OSVERSIONINFOA _osversioninfo;
    _osversioninfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetVersionExA(&_osversioninfo);

    // Create system information to the log file
    sysInfo = "OS: Windows v " + toString((int)_osversioninfo.dwMajorVersion) + "." + toString((int)_osversioninfo.dwMinorVersion) + "." + toString((int)_osversioninfo.dwBuildNumber) + (IsWow64() ? " (64 Bit)" : "");

    push_info(sysInfo);
}


/////////////////////////////////////////////////
/// \brief Push a message with the corresponding
/// logging level to the logger. The message will
/// be prefixed with the millisecond-precise
/// timestamp.
///
/// \param lvl Logger::LogLevel
/// \param sMessage const std::string&
/// \return void
///
/////////////////////////////////////////////////
void DetachedLogger::push_line(Logger::LogLevel lvl, const std::string& sMessage)
{
    if (lvl < m_level)
        return;

    std::string sLevel = toString(sys_time_now(), 0) + "  ";

    switch (lvl)
    {
        case Logger::LVL_DEBUG:
            sLevel += "DEBUG:   ";
            break;
        case Logger::LVL_INFO:
            sLevel += "INFO:    ";
            break;
        case Logger::LVL_CMDLINE:
            sLevel += "CMDLINE: ";
            break;
        case Logger::LVL_WARNING:
            sLevel += "WARNING: ";
            break;
        case Logger::LVL_ERROR:
            sLevel += "ERROR:   ";
            break;
        case Logger::LVL_DISABLED:
            return;
    }

    push_info(sLevel + sMessage);
}

