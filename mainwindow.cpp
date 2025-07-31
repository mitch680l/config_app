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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , serialPort(new SerialPort(this))
    , dataTimer(new QTimer(this))
    , isConnected(false)
    , currentComPort("COM9")
    , currentBaudRate(115200)
{
    setupUI();
    populateComPorts();
    populateBaudRates();
    
    // Connect serial port signals
    connect(serialPort, &SerialPort::errorOccurred, this, &MainWindow::handleError);
    
    // Set up timer for checking data - more frequent for better responsiveness
    connect(dataTimer, &QTimer::timeout, this, &MainWindow::checkForData);
    dataTimer->setInterval(10); // Check every 10ms instead of 50ms
    
    logMessage("Configuration GUI v1.0", "[INFO] ");
    logMessage("Ready for serial communication", "[INFO] ");
}

MainWindow::~MainWindow()
{
    if (isConnected) {
        disconnectFromPort();
    }
}

void MainWindow::setupUI()
{
    setWindowTitle("Configuration GUI");
    setGeometry(100, 100, 800, 600);
    
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
    QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbarWidget);
    
    // Connect button
    connectButton = new QPushButton("Connect");
    connectButton->setFixedWidth(80);
    toolbarLayout->addWidget(connectButton);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    
    toolbarLayout->addSpacing(20);
    
    // COM Port selection
    QLabel *comLabel = new QLabel("COM Port:");
    toolbarLayout->addWidget(comLabel);
    
    comPortCombo = new QComboBox;
    comPortCombo->setFixedWidth(80);
    toolbarLayout->addWidget(comPortCombo);
    connect(comPortCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, &MainWindow::onComPortChanged);
    
    toolbarLayout->addSpacing(20);
    
    // Baud rate selection
    QLabel *baudLabel = new QLabel("Baud:");
    toolbarLayout->addWidget(baudLabel);
    
    baudRateCombo = new QComboBox;
    baudRateCombo->setFixedWidth(80);
    toolbarLayout->addWidget(baudRateCombo);
    connect(baudRateCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, &MainWindow::onBaudRateChanged);
    
    toolbarLayout->addStretch();
    
    // Status indicator
    statusLabel = new QLabel("Disconnected");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");
    toolbarLayout->addWidget(statusLabel);
}

void MainWindow::createTerminal()
{
    // Terminal group box
    QGroupBox *terminalGroup = new QGroupBox("Serial Terminal");
    QVBoxLayout *terminalLayout = new QVBoxLayout(terminalGroup);
    
    // Terminal text area
    terminal = new QTextEdit;
    terminal->setReadOnly(true);
    terminal->setFont(QFont("Consolas", 9));
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
    
    // Add common COM ports
    QStringList comPorts = {"COM8", "COM9", "COM10", "COM11"};
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
    if (serialPort->open(currentComPort, currentBaudRate)) {
        isConnected = true;
        connectButton->setText("Disconnect");
        statusLabel->setText("Connected");
        statusLabel->setStyleSheet("color: green; font-weight: bold;");
        logMessage(QString("Connected to %1 at %2 baud").arg(currentComPort).arg(currentBaudRate));
        
        // Start timer for data checking
        dataTimer->start();
    } else {
        QMessageBox::critical(this, "Connection Error",
                            QString("Failed to connect to %1: %2")
                            .arg(currentComPort, serialPort->errorString()));
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
        // For Zephyr shell, we need to send the command with proper line ending
        // Zephyr shell expects \r\n (CRLF) for proper command processing
        QString commandToSend = command + "\r\n";
        
        QByteArray data = commandToSend.toUtf8();
        qint64 bytesWritten = serialPort->write(data);
        
        if (bytesWritten == data.size()) {
            logMessage(QString("Sent: %1").arg(command), "> ");
            commandInput->clear();
        } else {
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
        
        if (!cleanedData.isEmpty()) {
            // For Zephyr shell, we want to preserve the prompt format
            // Replace carriage returns with newlines for proper display
            cleanedData = cleanedData.replace("\r\n", "\n");
            cleanedData = cleanedData.replace("\r", "\n");
            
            logMessage(cleanedData, "");
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

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About Configuration GUI",
        "<h3>Configuration GUI v1.0</h3>"
        "<p>Copyright (c) 2024 - All rights reserved</p>"
        "<p>A simple GUI application for communicating with embedded Zephyr projects "
        "via serial connection.</p>"
        "<p><b>Features:</b></p>"
        "<ul>"
        "<li>Serial terminal communication</li>"
        "<li>Configurable COM port and baud rate</li>"
        "<li>Real-time data logging</li>"
        "<li>ANSI code filtering</li>"
        "</ul>"
        "<p>Built with Qt6 and C++</p>");
}

void MainWindow::logMessage(const QString &message, const QString &prefix)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formattedMessage = QString("%1 %2%3\n").arg(timestamp, prefix, message);
    
    terminal->insertPlainText(formattedMessage);
    
    // Auto-scroll to bottom
    QScrollBar *scrollBar = terminal->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
} 