#include "serialport.h"
#include <QTimer>

SerialPort::SerialPort(QObject *parent)
    : QObject(parent)
    , m_handle(INVALID_HANDLE_VALUE)
    , m_isOpen(false)
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

    // Set timeouts - more aggressive for better responsiveness
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 10;           // Reduced from 50
    timeouts.ReadTotalTimeoutConstant = 10;      // Reduced from 50
    timeouts.ReadTotalTimeoutMultiplier = 1;     // Reduced from 10
    timeouts.WriteTotalTimeoutConstant = 1000;   // Increased for writes
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(m_handle, &timeouts)) {
        setErrorString("Failed to set serial port timeouts");
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    m_isOpen = true;
    m_errorString.clear();
    return true;
}

void SerialPort::close()
{
    if (m_isOpen && m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        m_isOpen = false;
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
    if (!WriteFile(m_handle, data.constData(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr)) {
        setErrorString("Failed to write to serial port");
        return -1;
    }

    // For Zephyr shell, add a small delay to ensure command is processed
    if (bytesWritten > 0) {
        Sleep(10); // 10ms delay
    }

    return static_cast<qint64>(bytesWritten);
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

    // Read available data
    char buffer[1024];
    DWORD bytesRead = 0;
    
    // Read in smaller chunks to avoid blocking
    DWORD bytesToRead = (stat.cbInQue > sizeof(buffer)) ? sizeof(buffer) : stat.cbInQue;
    
    if (ReadFile(m_handle, buffer, bytesToRead, &bytesRead, nullptr) && bytesRead > 0) {
        data.append(buffer, static_cast<int>(bytesRead));
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