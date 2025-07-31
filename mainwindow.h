#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QWidget>
#include <QGroupBox>
#include <QScrollBar>
#include <QDateTime>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStringList>
#include <QFile>
#include <QTextStream>
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
    void onComPortChanged();
    void onBaudRateChanged();
    void checkForData();
    void showAbout();

private:
    void setupUI();
    void createMenuBar();
    void createToolbar();
    void createTerminal();
    void populateComPorts();
    void populateBaudRates();
    void connectToPort();
    void disconnectFromPort();
    void readData();
    void handleError(const QString &error);
    void logMessage(const QString &message, const QString &prefix = "");
    QString cleanAnsiCodes(const QString &input);
    QString filterShellPrompts(const QString &input);
    void writeToLogFile(const QString &message);
    void rotateLogFile();
    void initializeLogFile();

    SerialPort *serialPort;
    QTimer *dataTimer;
    QWidget *centralWidget;
    QWidget *toolbarWidget;
    QTextEdit *terminal;
    QPushButton *connectButton;
    QPushButton *sendButton;
    QComboBox *comPortCombo;
    QComboBox *baudRateCombo;
    QLineEdit *commandInput;
    QLabel *statusLabel;
    
    bool isConnected;
    QString currentComPort;
    int currentBaudRate;
    
    // Log file functionality
    QFile *logFile;
    QStringList logBuffer;
    static const int MAX_LOG_LINES = 10000;
    QString logFileName;
};

#endif // MAINWINDOW_H 