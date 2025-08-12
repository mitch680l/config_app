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
#include <QSerialPortInfo>
#include <QTimer>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QFileDialog>
#include <QProgressBar>
#include <QRadioButton>
#include <QButtonGroup>
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
    void refreshSerialPorts();

private:
    void setupUI();
    void createMenuBar();
    void createToolbar();
    void createDualPaneInterface();
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
    void scanAvailablePorts();
    void parseCommandOutput(const QString &data);
    bool isLogMessage(const QString &line);
    void logCommandToOutput(const QString &command);
    void clearCommandOutput();
    void autoClearCommandOutput();
    void flushIncompleteData();
    QStringList splitLongLine(const QString &line);
    bool isLikelyCorruptedLogLine(const QString &line);
    
    // Key Management functions
    void setupKeyManagementUI();
    void selectPemFile();
    void uploadCertificate();
    void processPemFile(const QString &filePath);
    void sendKeymgmtLine(const QString &line, int secTag, const QString &certType);
    void updateKeymgmtProgress(int current, int total);
    void abortUpload();
    
    // Command history functions
    void addCommandToHistory(const QString &command);
    void navigateCommandHistory(int direction);
    void resetAutoClearTimer();
    bool eventFilter(QObject *obj, QEvent *event) override;

    SerialPort *serialPort;
    QTimer *dataTimer;
    QTimer *portScanTimer;
    QTimer *autoClearTimer;
    bool userScrolling;
    QWidget *centralWidget;
    QWidget *toolbarWidget;
    QTextEdit *terminal;
    QTextEdit *commandOutput;
    QSplitter *mainSplitter;
    QPushButton *connectButton;
    QPushButton *sendButton;
    QPushButton *refreshPortsButton;
    QPushButton *clearCommandButton;
    QComboBox *comPortCombo;
    QComboBox *baudRateCombo;
    QLineEdit *commandInput;
    QLabel *statusLabel;
    
    // Key Management UI elements
    QWidget *keymgmtWidget;
    QLineEdit *pemFileEdit;
    QPushButton *selectPemButton;
    QComboBox *certTypeCombo;
    QRadioButton *mqttRadio;
    QRadioButton *fotaRadio;
    QPushButton *uploadButton;
    QPushButton *abortButton;
    QProgressBar *uploadProgress;
    QLabel *keymgmtStatus;
    QStringList pemLines;
    int currentPemLine;
    QTimer *keymgmtTimer;
    
    // Command history
    QStringList commandHistory;
    int historyIndex;
    QString currentInput;
    
    bool isConnected;
    QString currentComPort;
    int currentBaudRate;
    
    // Log file functionality
    QFile *logFile;
    QStringList logBuffer;
    static const int MAX_LOG_LINES = 10000;
    QString logFileName;
    
    // Enhanced buffer management
    QString accumulatedData;
    static const int MAX_ACCUMULATED_SIZE = 65536; // 64KB buffer (2x larger)
    QTimer *flushTimer;
    static const int FLUSH_TIMEOUT = 25; // 25ms timeout for faster processing
    static const int MAX_LINE_LENGTH = 8192; // 8KB max line length
    static const int LINE_RECONSTRUCTION_TIMEOUT = 100; // 100ms for line reconstruction
};

#endif // MAINWINDOW_H 