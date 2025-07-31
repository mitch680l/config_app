#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QMenuBar>
#include "serialport.h"

QT_BEGIN_NAMESPACE
class QSerialPortInfo;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void toggleConnection();
    void sendCommand();
    void readData();
    void handleError(const QString &error);
    void onComPortChanged();
    void onBaudRateChanged();
    void checkForData();
    void showAbout();

private:
    void setupUI();
    void createToolbar();
    void createTerminal();
    void createMenuBar();
    void logMessage(const QString &message, const QString &prefix = "[INFO] ");
    void connectToPort();
    void disconnectFromPort();
    void populateComPorts();
    void populateBaudRates();
    QString cleanAnsiCodes(const QString &input);

    // UI Components
    QWidget *centralWidget;
    QWidget *toolbarWidget;
    QTextEdit *terminal;
    QLineEdit *commandInput;
    QPushButton *connectButton;
    QPushButton *sendButton;
    QComboBox *comPortCombo;
    QComboBox *baudRateCombo;
    QLabel *statusLabel;

    // Serial Communication
    SerialPort *serialPort;
    QTimer *dataTimer;
    bool isConnected;
    QString currentComPort;
    int currentBaudRate;
};

#endif // MAINWINDOW_H 