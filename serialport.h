#ifndef SERIALPORT_H
#define SERIALPORT_H

#include <QObject>
#include <QString>
#include <windows.h>

class SerialPort : public QObject
{
    Q_OBJECT

public:
    explicit SerialPort(QObject *parent = nullptr);
    ~SerialPort();

    bool open(const QString &portName, int baudRate);
    bool openDual(const QString &readPort, const QString &writePort, int baudRate);
    void close();
    bool isOpen() const;
    QString errorString() const;
    
    qint64 write(const QByteArray &data);
    QByteArray readAll();
    bool hasData() const;

signals:
    void dataReceived();
    void errorOccurred(const QString &error);

private:
    HANDLE m_handle;
    HANDLE m_writeHandle;  // Separate handle for writing
    bool m_isOpen;
    bool m_isDualMode;
    QString m_errorString;
    
    void setErrorString(const QString &error);
};

#endif // SERIALPORT_H 