#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QScrollBar>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QStandardPaths>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , serialPort(new SerialPort(this))
    , dataTimer(new QTimer(this))
    , isConnected(false)
    , currentComPort("COM9")
    , currentBaudRate(115200)
    , logFile(nullptr)
    , logBuffer()
    , logFileName("config_gui.log")
{
    setupUI();
    populateComPorts();
    populateBaudRates();
    
    // Initialize log file
    initializeLogFile();
    
    // Connect serial port signals
    connect(serialPort, &SerialPort::errorOccurred, this, &MainWindow::handleError);
    
    // Set up timer for checking data - very frequent for responsiveness
    connect(dataTimer, &QTimer::timeout, this, &MainWindow::checkForData);
    dataTimer->setInterval(5); // Check every 5ms for maximum responsiveness
    
    logMessage("Configuration GUI v1.0", "[INFO] ");
    logMessage("Ready for serial communication", "[INFO] ");
    logMessage("Using Nordic serial terminal patterns", "[INFO] ");
    logMessage("COM9 for both read and write operations", "[INFO] ");
}

MainWindow::~MainWindow()
{
    if (isConnected) {
        disconnectFromPort();
    }
    
    if (logFile) {
        logFile->close();
        delete logFile;
    }
}

void MainWindow::initializeLogFile()
{
    // Create logs directory if it doesn't exist
    QDir logsDir("logs");
    if (!logsDir.exists()) {
        logsDir.mkpath(".");
    }
    
    logFileName = "logs/" + logFileName;
    logFile = new QFile(logFileName);
    
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(logFile);
        stream << "=== Configuration GUI Log Started: " 
               << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " ===\n";
        stream.flush();
    }
}

void MainWindow::setupUI()
{
    setWindowTitle("Configuration GUI");
    setGeometry(100, 100, 900, 700); // Larger window for better logging
    
    createMenuBar();
    
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    createToolbar();
    createTerminal();
    
    mainLayout->addWidget(toolbarWidget);
    mainLayout->addWidget(terminal);
}

void MainWindow::createMenuBar()
{
    QMenuBar *menuBar = this->menuBar();
    
    // Help menu
    QMenu *helpMenu = menuBar->addMenu("&Help");
    
    // About action
    QAction *aboutAction = new QAction("&About", this);
    aboutAction->setShortcut(QKeySequence::HelpContents);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    helpMenu->addAction(aboutAction);
}

void MainWindow::createToolbar()
{
    toolbarWidget = new QWidget;
    QVBoxLayout *toolbarLayout = new QVBoxLayout(toolbarWidget);
    
    // Top row - Connection controls
    QHBoxLayout *topRow = new QHBoxLayout;
    
    // Connect button
    connectButton = new QPushButton("Connect");
    connectButton->setFixedWidth(80);
    topRow->addWidget(connectButton);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    
    topRow->addSpacing(20);
    
    // COM Port selection (Nordic typically uses COM9)
    QLabel *comLabel = new QLabel("COM Port:");
    topRow->addWidget(comLabel);
    
    comPortCombo = new QComboBox;
    comPortCombo->setFixedWidth(80);
    topRow->addWidget(comPortCombo);
    connect(comPortCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, &MainWindow::onComPortChanged);
    
    topRow->addSpacing(20);
    
    // Baud rate selection
    QLabel *baudLabel = new QLabel("Baud:");
    topRow->addWidget(baudLabel);
    
    baudRateCombo = new QComboBox;
    baudRateCombo->setFixedWidth(80);
    topRow->addWidget(baudRateCombo);
    connect(baudRateCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, &MainWindow::onBaudRateChanged);
    
    topRow->addStretch();
    
    // Status indicator
    statusLabel = new QLabel("Disconnected");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");
    topRow->addWidget(statusLabel);
    
    toolbarLayout->addLayout(topRow);
    
    // Bottom row - Port info
    QHBoxLayout *bottomRow = new QHBoxLayout;
    
    QLabel *infoLabel = new QLabel("Nordic Serial Terminal - Single COM Port");
    infoLabel->setStyleSheet("color: blue; font-style: italic;");
    bottomRow->addWidget(infoLabel);
    
    bottomRow->addStretch();
    
    toolbarLayout->addLayout(bottomRow);
}

void MainWindow::createTerminal()
{
    // Terminal group box
    QGroupBox *terminalGroup = new QGroupBox("Serial Terminal");
    QVBoxLayout *terminalLayout = new QVBoxLayout(terminalGroup);
    
    // Terminal text area - larger for better logging
    terminal = new QTextEdit;
    terminal->setReadOnly(true);
    terminal->setFont(QFont("Consolas", 9));
    terminal->setMinimumHeight(400); // Larger terminal for scrolling
    terminalLayout->addWidget(terminal);
    
    // Input area
    QHBoxLayout *inputLayout = new QHBoxLayout;
    
    commandInput = new QLineEdit;
    commandInput->setPlaceholderText("Enter command...");
    inputLayout->addWidget(commandInput);
    
    sendButton = new QPushButton("Send");
    sendButton->setFixedWidth(60);
    inputLayout->addWidget(sendButton);
    
    connect(commandInput, &QLineEdit::returnPressed, this, &MainWindow::sendCommand);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCommand);
    
    terminalLayout->addLayout(inputLayout);
    
    // Add terminal to main layout
    centralWidget->layout()->addWidget(terminalGroup);
}

void MainWindow::populateComPorts()
{
    comPortCombo->clear();
    
    // Add common COM ports (Nordic typically uses COM9)
    QStringList comPorts = {"COM9", "COM8", "COM10", "COM11"};
    comPortCombo->addItems(comPorts);
    comPortCombo->setCurrentText(currentComPort);
}

void MainWindow::populateBaudRates()
{
    baudRateCombo->clear();
    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200"};
    baudRateCombo->addItems(baudRates);
    baudRateCombo->setCurrentText(QString::number(currentBaudRate));
}

void MainWindow::onComPortChanged()
{
    currentComPort = comPortCombo->currentText();
    logMessage(QString("Selected COM port: %1").arg(currentComPort), "[INFO] ");
}

void MainWindow::onBaudRateChanged()
{
    bool ok;
    int baudRate = baudRateCombo->currentText().toInt(&ok);
    if (ok) {
        currentBaudRate = baudRate;
    }
}

void MainWindow::toggleConnection()
{
    if (!isConnected) {
        connectToPort();
    } else {
        disconnectFromPort();
    }
}

void MainWindow::connectToPort()
{
    // Use single COM port for Nordic terminal
    bool success = serialPort->open(currentComPort, currentBaudRate);
    
    if (success) {
        logMessage(QString("Connected to %1 at %2 baud").arg(currentComPort, QString::number(currentBaudRate)));
        logMessage("Nordic terminal ready for commands", "[INFO] ");
        
        isConnected = true;
        connectButton->setText("Disconnect");
        statusLabel->setText("Connected");
        statusLabel->setStyleSheet("color: green; font-weight: bold;");
        
        // Start timer for data checking
        dataTimer->start();
    } else {
        QMessageBox::critical(this, "Connection Error",
                            QString("Failed to connect: %1").arg(serialPort->errorString()));
    }
}

void MainWindow::disconnectFromPort()
{
    dataTimer->stop();
    serialPort->close();
    isConnected = false;
    connectButton->setText("Connect");
    statusLabel->setText("Disconnected");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");
    logMessage("Disconnected");
}

void MainWindow::sendCommand()
{
    if (!isConnected) {
        QMessageBox::warning(this, "Not Connected", "Please connect to the device first.");
        return;
    }
    
    QString command = commandInput->text().trimmed();
    if (!command.isEmpty()) {
        // For Nordic terminal, send command with newline
        QString commandToSend = command + "\n";
        
        QByteArray data = commandToSend.toUtf8();
        qint64 bytesWritten = serialPort->write(data);
        
        if (bytesWritten == data.size()) {
            logMessage(QString("Sent: %1").arg(command), "> ");
            commandInput->clear();
        } else {
            logMessage(QString("Send failed: wrote %1 of %2 bytes").arg(QString::number(bytesWritten), QString::number(data.size())), "[ERROR] ");
            QMessageBox::critical(this, "Send Error",
                                QString("Failed to send command: %1").arg(serialPort->errorString()));
        }
    }
}

void MainWindow::checkForData()
{
    if (isConnected && serialPort->hasData()) {
        readData();
    }
}

QString MainWindow::cleanAnsiCodes(const QString &input)
{
    QString cleaned = input;
    
    // Remove ANSI escape sequences
    QRegularExpression ansiRegex(R"(\x1B\[[0-9;]*[a-zA-Z])");
    cleaned = cleaned.remove(ansiRegex);
    
    // Remove terminal control sequences like [8D[J
    QRegularExpression controlRegex(R"(\[[0-9]*[A-Z]\[[0-9]*[A-Z])");
    cleaned = cleaned.remove(controlRegex);
    
    // Remove color codes like [1;33m, [0m, [1;32m
    QRegularExpression colorRegex(R"(\[[0-9;]*m)");
    cleaned = cleaned.remove(colorRegex);
    
    // Remove cursor positioning codes
    QRegularExpression cursorRegex(R"(\x1B\[[0-9]*[ABCD])");
    cleaned = cleaned.remove(cursorRegex);
    
    // Remove other control characters except newlines and carriage returns
    QString result;
    for (QChar ch : cleaned) {
        if (ch == '\n' || ch == '\r' || (ch.unicode() >= 32 && ch.unicode() < 127)) {
            result += ch;
        }
    }
    
    return result;
}

QString MainWindow::filterShellPrompts(const QString &input)
{
    QString filtered = input;
    
    // Remove shell prompts like "uart:~$ " and variations
    QRegularExpression shellPromptRegex(R"(uart:~\$?\s*)");
    filtered = filtered.remove(shellPromptRegex);
    
    // Remove other common shell prompts
    QRegularExpression genericPromptRegex(R"([a-zA-Z0-9_-]+:~?\$?\s*)");
    filtered = filtered.remove(genericPromptRegex);
    
    // Remove empty lines that might be left after filtering
    QStringList lines = filtered.split('\n', Qt::SkipEmptyParts);
    filtered = lines.join('\n');
    
    return filtered;
}

void MainWindow::readData()
{
    const QByteArray data = serialPort->readAll();
    if (!data.isEmpty()) {
        // Convert to string and handle potential encoding issues
        QString receivedData;
        
        // Try UTF-8 first
        receivedData = QString::fromUtf8(data);
        
        // If UTF-8 fails, try other encodings
        if (receivedData.isEmpty() && !data.isEmpty()) {
            receivedData = QString::fromLatin1(data);
        }
        
        // If still empty, use raw bytes
        if (receivedData.isEmpty() && !data.isEmpty()) {
            receivedData = QString::fromLocal8Bit(data);
        }
        
        // Clean ANSI codes and control sequences
        QString cleanedData = cleanAnsiCodes(receivedData);
        
        // Filter out shell prompts
        QString filteredData = filterShellPrompts(cleanedData);
        
        if (!filteredData.isEmpty()) {
            // For Nordic terminal, preserve the format
            // Replace carriage returns with newlines for proper display
            filteredData = filteredData.replace("\r\n", "\n");
            filteredData = filteredData.replace("\r", "\n");
            
            logMessage(filteredData, "");
        }
    }
}

void MainWindow::handleError(const QString &error)
{
    logMessage(QString("Serial Error: %1").arg(error), "[ERROR] ");
    
    if (isConnected) {
        disconnectFromPort();
    }
}

void MainWindow::writeToLogFile(const QString &message)
{
    if (logFile && logFile->isOpen()) {
        QTextStream stream(logFile);
        stream << message << "\n";
        stream.flush();
    }
}

void MainWindow::rotateLogFile()
{
    // Keep only the last MAX_LOG_LINES in memory
    if (logBuffer.size() > MAX_LOG_LINES) {
        logBuffer = logBuffer.mid(logBuffer.size() - MAX_LOG_LINES);
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About Configuration GUI",
        "<h3>Configuration GUI v1.0</h3>"
        "<p>Copyright (c) 2024 - All rights reserved</p>"
        "<p>A simple GUI application for communicating with Nordic devices "
        "via serial connection.</p>"
        "<p><b>Features:</b></p>"
        "<ul>"
        "<li>Nordic serial terminal communication</li>"
        "<li>Single COM port support</li>"
        "<li>Real-time data logging</li>"
        "<li>ANSI code filtering</li>"
        "<li>Shell prompt filtering</li>"
        "<li>Log file with 10,000 line history</li>"
        "</ul>"
        "<p>Built with Qt6 and C++</p>");
}

void MainWindow::logMessage(const QString &message, const QString &prefix)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz"); // Include milliseconds
    QString formattedMessage = QString("%1 %2%3").arg(timestamp, prefix, message);
    
    // Add to terminal
    terminal->insertPlainText(formattedMessage + "\n");
    
    // Add to log buffer
    logBuffer.append(formattedMessage);
    rotateLogFile();
    
    // Write to log file
    writeToLogFile(formattedMessage);
    
    // Auto-scroll to bottom
    QScrollBar *scrollBar = terminal->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
} 