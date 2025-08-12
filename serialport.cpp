#include "serialport.h"
#include <QTimer>

SerialPort::SerialPort(QObject *parent)
    : QObject(parent)
    , m_handle(INVALID_HANDLE_VALUE)
    , m_writeHandle(INVALID_HANDLE_VALUE)
    , m_isOpen(false)
    , m_isDualMode(false)
{
}

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::open(const QString &portName, int baudRate)
{
    if (m_isOpen) {
        close();
    }

    // Convert port name to Windows format
    QString winPortName = portName;
    if (!winPortName.startsWith("\\\\.\\")) {
        winPortName = "\\\\.\\" + portName;
    }

    // Open the serial port
    m_handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(winPortName.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_handle == INVALID_HANDLE_VALUE) {
        setErrorString("Failed to open serial port");
        return false;
    }

    // Configure the serial port
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    
    if (!GetCommState(m_handle, &dcb)) {
        setErrorString("Failed to get serial port state");
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set baud rate
    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    if (!SetCommState(m_handle, &dcb)) {
        setErrorString("Failed to set serial port state");
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set timeouts - Optimized for robust line reconstruction
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 5;            // Shorter for faster response
    timeouts.ReadTotalTimeoutConstant = 5;       // Shorter for faster response
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 2000;   // Longer write timeout for Nordic
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(m_handle, &timeouts)) {
        setErrorString("Failed to set serial port timeouts");
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Purge any existing data
    PurgeComm(m_handle, PURGE_TXCLEAR | PURGE_RXCLEAR);

    m_isOpen = true;
    m_isDualMode = false;
    m_errorString.clear();
    return true;
}

bool SerialPort::openDual(const QString &readPort, const QString &writePort, int baudRate)
{
    if (m_isOpen) {
        close();
    }

    // Convert port names to Windows format
    QString readWinPort = readPort;
    QString writeWinPort = writePort;
    if (!readWinPort.startsWith("\\\\.\\")) {
        readWinPort = "\\\\.\\" + readPort;
    }
    if (!writeWinPort.startsWith("\\\\.\\")) {
        writeWinPort = "\\\\.\\" + writePort;
    }

    // Open read port (COM9)
    m_handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(readWinPort.utf16()),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_handle == INVALID_HANDLE_VALUE) {
        setErrorString(QString("Failed to open read port %1").arg(readPort));
        return false;
    }

    // Open write port (COM8)
    m_writeHandle = CreateFileW(
        reinterpret_cast<LPCWSTR>(writeWinPort.utf16()),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_writeHandle == INVALID_HANDLE_VALUE) {
        setErrorString(QString("Failed to open write port %1").arg(writePort));
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Configure read port - optimized for fast reading
    DCB readDcb = {0};
    readDcb.DCBlength = sizeof(DCB);
    
    if (!GetCommState(m_handle, &readDcb)) {
        setErrorString("Failed to get read port state");
        CloseHandle(m_handle);
        CloseHandle(m_writeHandle);
        m_handle = INVALID_HANDLE_VALUE;
        m_writeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    readDcb.BaudRate = baudRate;
    readDcb.ByteSize = 8;
    readDcb.Parity = NOPARITY;
    readDcb.StopBits = ONESTOPBIT;

    if (!SetCommState(m_handle, &readDcb)) {
        setErrorString("Failed to set read port state");
        CloseHandle(m_handle);
        CloseHandle(m_writeHandle);
        m_handle = INVALID_HANDLE_VALUE;
        m_writeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Configure write port
    DCB writeDcb = {0};
    writeDcb.DCBlength = sizeof(DCB);
    
    if (!GetCommState(m_writeHandle, &writeDcb)) {
        setErrorString("Failed to get write port state");
        CloseHandle(m_handle);
        CloseHandle(m_writeHandle);
        m_handle = INVALID_HANDLE_VALUE;
        m_writeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    writeDcb.BaudRate = baudRate;
    writeDcb.ByteSize = 8;
    writeDcb.Parity = NOPARITY;
    writeDcb.StopBits = ONESTOPBIT;

    if (!SetCommState(m_writeHandle, &writeDcb)) {
        setErrorString("Failed to set write port state");
        CloseHandle(m_handle);
        CloseHandle(m_writeHandle);
        m_handle = INVALID_HANDLE_VALUE;
        m_writeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set timeouts for read port - very aggressive for responsiveness
    COMMTIMEOUTS readTimeouts = {0};
    readTimeouts.ReadIntervalTimeout = 1;        // Very short for fast response
    readTimeouts.ReadTotalTimeoutConstant = 1;   // Very short for fast response
    readTimeouts.ReadTotalTimeoutMultiplier = 1;
    readTimeouts.WriteTotalTimeoutConstant = 1000;
    readTimeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(m_handle, &readTimeouts)) {
        setErrorString("Failed to set read port timeouts");
        CloseHandle(m_handle);
        CloseHandle(m_writeHandle);
        m_handle = INVALID_HANDLE_VALUE;
        m_writeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set timeouts for write port
    COMMTIMEOUTS writeTimeouts = {0};
    writeTimeouts.ReadIntervalTimeout = 1000;
    writeTimeouts.ReadTotalTimeoutConstant = 1000;
    writeTimeouts.ReadTotalTimeoutMultiplier = 1;
    writeTimeouts.WriteTotalTimeoutConstant = 2000;
    writeTimeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(m_writeHandle, &writeTimeouts)) {
        setErrorString("Failed to set write port timeouts");
        CloseHandle(m_handle);
        CloseHandle(m_writeHandle);
        m_handle = INVALID_HANDLE_VALUE;
        m_writeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Purge any existing data
    PurgeComm(m_handle, PURGE_RXCLEAR);
    PurgeComm(m_writeHandle, PURGE_TXCLEAR);

    m_isOpen = true;
    m_isDualMode = true;
    m_errorString.clear();
    return true;
}

void SerialPort::close()
{
    if (m_isOpen) {
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
        if (m_writeHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_writeHandle);
            m_writeHandle = INVALID_HANDLE_VALUE;
        }
        m_isOpen = false;
        m_isDualMode = false;
    }
}

bool SerialPort::isOpen() const
{
    return m_isOpen && m_handle != INVALID_HANDLE_VALUE;
}

QString SerialPort::errorString() const
{
    return m_errorString;
}

qint64 SerialPort::write(const QByteArray &data)
{
    if (!m_isOpen || m_handle == INVALID_HANDLE_VALUE) {
        setErrorString("Serial port is not open");
        return -1;
    }

    DWORD bytesWritten = 0;
    DWORD totalBytesWritten = 0;
    const char* buffer = data.constData();
    DWORD bytesToWrite = static_cast<DWORD>(data.size());
    
    // For Nordic terminal, write in smaller chunks
    const DWORD chunkSize = 32; // Smaller chunks for Nordic
    
    while (totalBytesWritten < bytesToWrite) {
        DWORD currentChunk = (bytesToWrite - totalBytesWritten > chunkSize) ? 
                             chunkSize : (bytesToWrite - totalBytesWritten);
        
        if (!WriteFile(m_handle, buffer + totalBytesWritten, currentChunk, &bytesWritten, nullptr)) {
            setErrorString("Failed to write to serial port");
            return -1;
        }
        
        totalBytesWritten += bytesWritten;
        
        // Small delay between chunks for Nordic
        if (totalBytesWritten < bytesToWrite) {
            Sleep(1);
        }
    }

    // Flush the serial port to ensure data is sent immediately
    FlushFileBuffers(m_handle);

    // For Nordic terminal, shorter delay
    if (totalBytesWritten > 0) {
        Sleep(50); // 50ms delay for Nordic terminal
    }

    return static_cast<qint64>(totalBytesWritten);
}

QByteArray SerialPort::readAll()
{
    QByteArray data;
    if (!m_isOpen || m_handle == INVALID_HANDLE_VALUE) {
        return data;
    }

    // Check if there's data available
    DWORD errors;
    COMSTAT stat;
    if (!ClearCommError(m_handle, &errors, &stat)) {
        return data;
    }

    if (stat.cbInQue == 0) {
        return data;
    }

    // Read available data in optimal chunks for robust line reconstruction
    const int maxChunkSize = 8192; // 8KB chunks for better line integrity
    char buffer[maxChunkSize];
    
    while (true) {
        // Check how much data is available
        if (!ClearCommError(m_handle, &errors, &stat)) {
            break;
        }
        
        if (stat.cbInQue == 0) {
            break;
        }
        
        // Read in smaller chunks
        DWORD bytesToRead = (stat.cbInQue > maxChunkSize) ? maxChunkSize : stat.cbInQue;
        DWORD bytesRead = 0;
        
        if (ReadFile(m_handle, buffer, bytesToRead, &bytesRead, nullptr) && bytesRead > 0) {
            data.append(buffer, static_cast<int>(bytesRead));
        } else {
            break;
        }
        
        // Minimal delay to prevent blocking while maintaining line integrity
        if (bytesRead < bytesToRead) {
            Sleep(1);
        }
    }

    return data;
}

bool SerialPort::hasData() const
{
    if (!m_isOpen || m_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD errors;
    COMSTAT stat;
    if (ClearCommError(m_handle, &errors, &stat)) {
        return stat.cbInQue > 0;
    }
    return false;
}

void SerialPort::setErrorString(const QString &error)
{
    m_errorString = error;
    emit errorOccurred(error);
} 