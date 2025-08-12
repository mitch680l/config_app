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
#include <QSerialPortInfo>
#include <QTabWidget>
#include <QFileDialog>
#include <QProgressBar>
#include <QRadioButton>
#include <QButtonGroup>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , serialPort(new SerialPort(this))
    , dataTimer(new QTimer(this))
    , portScanTimer(new QTimer(this))
    , autoClearTimer(new QTimer(this))
    , userScrolling(false)
    , isConnected(false)
    , currentComPort("COM9")
    , currentBaudRate(115200)
    , logFile(nullptr)
    , logBuffer()
    , logFileName("config_gui.log")
    , flushTimer(new QTimer(this))
    , keymgmtTimer(new QTimer(this))
{
    setupUI();
    scanAvailablePorts();
    populateBaudRates();
    
    // Initialize log file
    initializeLogFile();
    
    // Connect serial port signals
    connect(serialPort, &SerialPort::errorOccurred, this, &MainWindow::handleError);
    
    // Set up timer for checking data - optimized for robust line reconstruction
    connect(dataTimer, &QTimer::timeout, this, &MainWindow::checkForData);
    dataTimer->setInterval(2); // Check every 2ms for maximum responsiveness
    
    // Set up timer for periodic port scanning
    connect(portScanTimer, &QTimer::timeout, this, &MainWindow::scanAvailablePorts);
    portScanTimer->setInterval(2000); // Scan every 2 seconds
    portScanTimer->start();
    
    // Set up timer for auto-clearing command output
    connect(autoClearTimer, &QTimer::timeout, this, &MainWindow::autoClearCommandOutput);
    autoClearTimer->setInterval(15000); // Clear every 15 seconds
    autoClearTimer->start();
    
    // Set up timer for flushing incomplete data
    connect(flushTimer, &QTimer::timeout, this, &MainWindow::flushIncompleteData);
    flushTimer->setSingleShot(true);
    
    // Set up timer for key management uploads
    connect(keymgmtTimer, &QTimer::timeout, this, [this]() {
        if (currentPemLine < pemLines.size()) {
            QString line = pemLines[currentPemLine];
            int secTag = mqttRadio->isChecked() ? 42 : 44;
            QString certType = certTypeCombo->currentText().toLower();
            sendKeymgmtLine(line, secTag, certType);
            currentPemLine++;
            updateKeymgmtProgress(currentPemLine, pemLines.size());
        } else {
            keymgmtTimer->stop();
            keymgmtStatus->setText("Upload complete!");
            uploadButton->setEnabled(true);
            abortButton->setEnabled(true); // Keep abort enabled after completion
        }
    });
    
    logMessage("Configuration GUI v1.0", "[INFO] ");
    logMessage("Ready for serial communication", "[INFO] ");
    logMessage("Using Nordic serial terminal patterns", "[INFO] ");
    logMessage("Auto-detecting available serial ports", "[INFO] ");
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
    setGeometry(100, 100, 1200, 800); // Larger window for dual-pane layout
    
    createMenuBar();
    
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    createToolbar();
    createDualPaneInterface();
    
    mainLayout->addWidget(toolbarWidget);
    mainLayout->addWidget(mainSplitter);
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
    toolbarWidget->setFixedHeight(24); // Slightly bigger toolbar height
    QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbarWidget);
    toolbarLayout->setContentsMargins(5, 2, 5, 2); // More margins
    toolbarLayout->setSpacing(8); // More spacing between widgets
    
    // Connection controls
    connectButton = new QPushButton("Connect");
    connectButton->setFixedWidth(69); // 60 * 1.15 = 69
    connectButton->setFixedHeight(20);
    toolbarLayout->addWidget(connectButton);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    
    toolbarLayout->addSpacing(10);
    
    // COM Port selection
    QLabel *comLabel = new QLabel("COM:");
    comLabel->setFixedWidth(29); // 25 * 1.15 = 28.75, rounded to 29
    comLabel->setFixedHeight(20);
    toolbarLayout->addWidget(comLabel);
    
    comPortCombo = new QComboBox;
    comPortCombo->setFixedWidth(69); // 60 * 1.15 = 69
    comPortCombo->setFixedHeight(20);
    toolbarLayout->addWidget(comPortCombo);
    connect(comPortCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, &MainWindow::onComPortChanged);
    
    // Refresh ports button
    refreshPortsButton = new QPushButton("🔄");
    refreshPortsButton->setFixedWidth(23); // 20 * 1.15 = 23
    refreshPortsButton->setFixedHeight(20);
    refreshPortsButton->setToolTip("Refresh available serial ports");
    toolbarLayout->addWidget(refreshPortsButton);
    connect(refreshPortsButton, &QPushButton::clicked, this, &MainWindow::refreshSerialPorts);
    
    toolbarLayout->addSpacing(10);
    
    // Baud rate selection
    QLabel *baudLabel = new QLabel("Baud:");
    baudLabel->setFixedWidth(35); // 30 * 1.15 = 34.5, rounded to 35
    baudLabel->setFixedHeight(20);
    toolbarLayout->addWidget(baudLabel);
    
    baudRateCombo = new QComboBox;
    baudRateCombo->setFixedWidth(69); // 60 * 1.15 = 69
    baudRateCombo->setFixedHeight(20);
    toolbarLayout->addWidget(baudRateCombo);
    connect(baudRateCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, &MainWindow::onBaudRateChanged);
    
    toolbarLayout->addStretch();
    
    // Status indicator
    statusLabel = new QLabel("Disconnected");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");
    statusLabel->setFixedWidth(81); // 70 * 1.15 = 80.5, rounded to 81
    statusLabel->setFixedHeight(20);
    toolbarLayout->addWidget(statusLabel);
}

void MainWindow::createDualPaneInterface()
{
    // Create main splitter
    mainSplitter = new QSplitter(Qt::Horizontal);
    
    // Left pane - Serial Terminal
    QGroupBox *terminalGroup = new QGroupBox("Serial Terminal");
    QVBoxLayout *terminalLayout = new QVBoxLayout(terminalGroup);
    
    terminal = new QTextEdit;
    terminal->setReadOnly(false); // Allow text selection and copying
    terminal->setFont(QFont("Consolas", 9));
    terminal->setMinimumHeight(400);
    terminalLayout->addWidget(terminal);
    
    // Connect scrollbar signals to track user scrolling
    connect(terminal->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar *scrollBar = terminal->verticalScrollBar();
        // If user scrolled up (not at bottom), mark as user scrolling
        if (value < scrollBar->maximum()) {
            userScrolling = true;
        } else {
            userScrolling = false;
        }
    });
    
    mainSplitter->addWidget(terminalGroup);
    
    // Right pane - Command Interface with Key Management
    QTabWidget *rightTabWidget = new QTabWidget;
    
    // Command Interface Tab
    QWidget *commandTab = new QWidget;
    QVBoxLayout *commandLayout = new QVBoxLayout(commandTab);
    
    // Command input area at top
    QHBoxLayout *inputLayout = new QHBoxLayout;
    
    commandInput = new QLineEdit;
    commandInput->setPlaceholderText("Enter shell command... (Use ↑/↓ for history)");
    commandInput->installEventFilter(this);
    inputLayout->addWidget(commandInput);
    
    sendButton = new QPushButton("Send");
    sendButton->setFixedWidth(60);
    inputLayout->addWidget(sendButton);
    
    clearCommandButton = new QPushButton("Clear");
    clearCommandButton->setFixedWidth(60);
    clearCommandButton->setToolTip("Clear command output");
    inputLayout->addWidget(clearCommandButton);
    
    connect(commandInput, &QLineEdit::returnPressed, this, &MainWindow::sendCommand);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCommand);
    connect(clearCommandButton, &QPushButton::clicked, this, &MainWindow::clearCommandOutput);
    
    // Command history navigation
    connect(commandInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        currentInput = text;
    });
    
    commandLayout->addLayout(inputLayout);
    
    // Command output area below
    commandOutput = new QTextEdit;
    commandOutput->setReadOnly(true);
    commandOutput->setFont(QFont("Consolas", 9));
    commandOutput->setMinimumHeight(400);
    commandOutput->setStyleSheet("QTextEdit { background-color: #f8f8f8; }");
    commandLayout->addWidget(commandOutput);
    
    rightTabWidget->addTab(commandTab, "Command Interface");
    
    // Key Management Tab
    setupKeyManagementUI();
    rightTabWidget->addTab(keymgmtWidget, "Key Management");
    
    mainSplitter->addWidget(rightTabWidget);
    
    // Set initial splitter sizes (60% left, 40% right)
    mainSplitter->setSizes({600, 400});
}

void MainWindow::scanAvailablePorts()
{
    QStringList availablePorts;
    
    // Get all available serial ports using Qt6 SerialPort
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        availablePorts << info.portName();
    }
    
    // Sort ports numerically (COM1, COM2, etc.)
    std::sort(availablePorts.begin(), availablePorts.end(), [](const QString &a, const QString &b) {
        // Extract numbers from COM port names
        int numA = a.mid(3).toInt();
        int numB = b.mid(3).toInt();
        return numA < numB;
    });
    
    // Update combo box if ports have changed
    QString currentSelection = comPortCombo->currentText();
    QStringList currentPorts;
    for (int i = 0; i < comPortCombo->count(); ++i) {
        currentPorts << comPortCombo->itemText(i);
    }
    
    if (currentPorts != availablePorts) {
        comPortCombo->clear();
        if (!availablePorts.isEmpty()) {
            comPortCombo->addItems(availablePorts);
            
            // Try to maintain current selection if it's still available
            if (!currentSelection.isEmpty() && availablePorts.contains(currentSelection)) {
                comPortCombo->setCurrentText(currentSelection);
            } else {
                comPortCombo->setCurrentIndex(0);
                currentComPort = comPortCombo->currentText();
            }
            
            logMessage(QString("Found %1 available serial port(s): %2")
                      .arg(availablePorts.size())
                      .arg(availablePorts.join(", ")), "[INFO] ");
        } else {
            logMessage("No serial ports found", "[WARNING] ");
        }
    }
}

void MainWindow::populateComPorts()
{
    // This function is now replaced by scanAvailablePorts()
    // Keeping it for backward compatibility
    scanAvailablePorts();
}

void MainWindow::refreshSerialPorts()
{
    logMessage("Manually refreshing serial ports...", "[INFO] ");
    scanAvailablePorts();
}

void MainWindow::populateBaudRates()
{
    baudRateCombo->clear();
    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200"};
    baudRateCombo->addItems(baudRates);
    // Set the current baud rate (115200) as the default selection
    baudRateCombo->setCurrentText("115200");
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
        // Add command to history
        addCommandToHistory(command);
        
        // For Nordic terminal, send command with newline
        QString commandToSend = command + "\n";
        
        QByteArray data = commandToSend.toUtf8();
        qint64 bytesWritten = serialPort->write(data);
        
        if (bytesWritten == data.size()) {
            logMessage(QString("Sent: %1").arg(command), "> ");
            logCommandToOutput(command);
            commandInput->clear();
            
            // Reset auto-clear timer when command is sent
            resetAutoClearTimer();
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
    
    // Remove "dev>" shell prompt (activated when logged in)
    QRegularExpression devPromptRegex(R"(dev>\s*)");
    filtered = filtered.remove(devPromptRegex);
    
    // Remove "login>" shell prompt
    QRegularExpression loginPromptRegex(R"(login>\s*)");
    filtered = filtered.remove(loginPromptRegex);
    
    // Remove other common shell prompts
    QRegularExpression genericPromptRegex(R"([a-zA-Z0-9_-]+:~?\$?\s*)");
    filtered = filtered.remove(genericPromptRegex);
    
    // Remove the 'x' shell line start marker completely
    filtered = filtered.remove('x');
    
    // Remove standalone 'x' characters that are shell markers
    QRegularExpression xMarkerRegex(R"(^x\s*$)");
    filtered = filtered.remove(xMarkerRegex);
    
    // Remove lines that start with 'x' followed by whitespace
    QRegularExpression xStartRegex(R"(^x\s+)");
    filtered = filtered.remove(xStartRegex);
    
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
        
        // Accumulate data for better message handling
        accumulatedData += receivedData;
        
        // Limit accumulated data size to prevent memory issues
        if (accumulatedData.length() > MAX_ACCUMULATED_SIZE) {
            accumulatedData = accumulatedData.right(MAX_ACCUMULATED_SIZE / 2);
        }
        
        // Clean ANSI codes and control sequences
        QString cleanedData = cleanAnsiCodes(accumulatedData);
        
        // Filter out shell prompts
        QString filteredData = filterShellPrompts(cleanedData);
        
        if (!filteredData.isEmpty()) {
            // Replace carriage returns with newlines for proper display
            filteredData = filteredData.replace("\r\n", "\n");
            filteredData = filteredData.replace("\r", "\n");
            
            // Check if we have complete lines (ending with newline)
            if (filteredData.endsWith('\n')) {
                // Process complete lines with robust line reconstruction
                QStringList lines = filteredData.split('\n', Qt::SkipEmptyParts);
                QStringList logLines, commandLines;
                QString currentLine;
                
                for (const QString &line : lines) {
                    QString trimmedLine = line.trimmed();
                    if (trimmedLine.isEmpty()) continue;
                    
                    // Check if this line is too long (likely fragmented)
                    if (trimmedLine.length() > MAX_LINE_LENGTH) {
                        // Split long lines that might be concatenated fragments
                        QStringList fragments = splitLongLine(trimmedLine);
                        for (const QString &fragment : fragments) {
                            if (fragment.isEmpty()) continue;
                            
                            // Check if this fragment is a log message
                            if (isLogMessage(fragment)) {
                                logLines.append(fragment);
                            } else {
                                commandLines.append(fragment);
                            }
                        }
                    } else {
                        // Normal line processing with corruption detection
                        if (isLogMessage(trimmedLine)) {
                            logLines.append(trimmedLine);
                        } else {
                            // Additional check for corrupted log lines without tags
                            if (isLikelyCorruptedLogLine(trimmedLine)) {
                                logLines.append(trimmedLine);
                            } else {
                                commandLines.append(trimmedLine);
                            }
                        }
                    }
                }
                
                // Send log lines to terminal
                if (!logLines.isEmpty()) {
                    logMessage(logLines.join('\n'), "");
                }
                
                // Send command lines to command interface
                if (!commandLines.isEmpty()) {
                    parseCommandOutput(commandLines.join('\n'));
                }
                
                // Clear accumulated data after processing complete lines
                accumulatedData.clear();
                flushTimer->stop(); // Stop flush timer since we processed complete data
            } else {
                // Incomplete message - start flush timer for line reconstruction
                flushTimer->start(LINE_RECONSTRUCTION_TIMEOUT);
            }
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
    
    // Auto-scroll to bottom only if user isn't manually scrolling
    if (!userScrolling) {
        QScrollBar *scrollBar = terminal->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }
}

void MainWindow::parseCommandOutput(const QString &data)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formattedOutput = QString("[%1] %2").arg(timestamp, data.trimmed());
    
    // Add to command output pane
    commandOutput->insertPlainText(formattedOutput + "\n");
    
    // Auto-scroll to bottom
    QScrollBar *scrollBar = commandOutput->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

bool MainWindow::isLogMessage(const QString &line)
{
    QString trimmed = line.trimmed();
    
    // Skip empty lines
    if (trimmed.isEmpty()) {
        return false;
    }
    
    // Check for log level tags - these indicate log messages
    // Pattern like: <inf>, <wrn>, <dbg>, <err>, etc.
    if (trimmed.contains("<inf>") || trimmed.contains("<wrn>") || 
        trimmed.contains("<dbg>") || trimmed.contains("<err>") ||
        trimmed.contains("<nfo>") || trimmed.contains("<warn>") ||
        trimmed.contains("<debug>") || trimmed.contains("<error>")) {
        return true; // Has log level tag, this is a log message
    }
    
    // Check for timestamp patterns with brackets (log messages)
    if (trimmed.contains('[') && trimmed.contains(']')) {
        // Check for timestamp pattern (numbers, dots, commas, colons inside brackets)
        QRegularExpression timestampPattern(R"(\[[0-9.,:]+\])");
        if (timestampPattern.match(trimmed).hasMatch()) {
            return true; // Has timestamp pattern, this is a log message
        }
        
        // Check for [hh:mm:ss] format timestamps specifically
        QRegularExpression timePattern(R"(\[[0-9]{1,2}:[0-9]{2}:[0-9]{2}\])");
        if (timePattern.match(trimmed).hasMatch()) {
            return true; // Has time format timestamp, this is a log message
        }
    }
    
    // Check for lines that start with [hh:mm:ss] followed by log content
    QRegularExpression timeStartPattern(R"(^\[[0-9]{1,2}:[0-9]{2}:[0-9]{2}\])");
    if (timeStartPattern.match(trimmed).hasMatch()) {
        return true; // Line starts with timestamp, this is a log message
    }
    
    // Check for lines that start with [hh:mm:ss] followed by any content (more aggressive)
    if (trimmed.startsWith('[') && trimmed.contains(']') && trimmed.contains(':')) {
        // Check if it's a timestamp pattern at the start
        QRegularExpression timePrefixPattern(R"(^\[[0-9]{1,2}:[0-9]{2}:[0-9]{2}\])");
        if (timePrefixPattern.match(trimmed).hasMatch()) {
            return true; // Starts with timestamp, filter out as log message
        }
    }
    
    // Check for lines that start with [hh:mm:ss] followed by log content (very aggressive)
    // This catches patterns like "[11:34:05] MQTT and LTE 1, 1"
    QRegularExpression timeStartLogPattern(R"(^\[[0-9]{1,2}:[0-9]{2}:[0-9]{2}\].*$)");
    if (timeStartLogPattern.match(trimmed).hasMatch()) {
        return true; // Line starts with timestamp followed by any content, this is a log message
    }
    
    // Check for incomplete timestamp fragments (like [01.04, [01.431,67)
    QRegularExpression fragmentPattern(R"(^\[[0-9.,:]*$)");
    if (fragmentPattern.match(trimmed).hasMatch()) {
        return true; // Incomplete timestamp fragment, this is log noise
    }
    
    // Check for lines that contain timestamp-like fragments anywhere
    QRegularExpression anyFragmentPattern(R"(\[[0-9.,:]{1,10}\])");
    if (anyFragmentPattern.match(trimmed).hasMatch()) {
        return true; // Contains timestamp-like fragment, likely log message
    }
    
    // Check for any line containing a timestamp pattern (very aggressive)
    // This catches any line with [hh:mm:ss] pattern anywhere
    QRegularExpression anyTimePattern(R"(\[[0-9]{1,2}:[0-9]{2}:[0-9]{2}\])");
    if (anyTimePattern.match(trimmed).hasMatch()) {
        return true; // Contains timestamp pattern anywhere, this is a log message
    }
    
    // Check for fragmented log content (single characters or very short fragments)
    // These are often corrupted log messages where timestamp gets separated
    if (trimmed.length() <= 5 && (trimmed.contains("w") || trimmed.contains("d") || 
        trimmed.contains(":") || trimmed.contains("n") || trimmed.contains("f") ||
        trimmed.contains(">") || trimmed.contains(" ") || trimmed.contains("x"))) {
        return true; // Very short fragments, likely corrupted log messages
    }
    
    // Check for single character lines (definitely noise)
    if (trimmed.length() == 1) {
        return true; // Single characters are always noise
    }
    
    // Check for lines that start with common log fragments
    if (trimmed.startsWith("w ") || trimmed.startsWith("d ") || 
        trimmed.startsWith(": ") || trimmed.startsWith("nf> ") ||
        trimmed.startsWith("n ") || trimmed.startsWith("f> ")) {
        return true; // Starts with log fragment, this is a corrupted log message
    }
    
    // Check for shell prompt patterns (not log messages)
    if (trimmed.contains("login>") || trimmed.contains("dev>") || 
        trimmed.contains("uart:~$") || trimmed.contains("$ ")) {
        return false; // Shell prompts are not log messages
    }
    
    // Check for single character lines or very short lines (likely control characters)
    if (trimmed.length() <= 2) {
        return true; // Treat very short lines as log noise
    }
    
    // Everything else is considered a command response (not a log message)
    return false;
}

void MainWindow::selectPemFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Select PEM File", "", "PEM Files (*.pem *.crt *.key *.pem);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        pemFileEdit->setText(fileName);
        processPemFile(fileName);
    }
}

void MainWindow::processPemFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        keymgmtStatus->setText("Error: Could not open file");
        keymgmtStatus->setStyleSheet("color: red;");
        return;
    }
    
    pemLines.clear();
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        // Keep ALL lines including empty ones to preserve PEM structure
        pemLines.append(line);
    }
    file.close();
    
    if (pemLines.isEmpty()) {
        keymgmtStatus->setText("Error: No valid lines found in PEM file");
        keymgmtStatus->setStyleSheet("color: red;");
        uploadButton->setEnabled(false);
        abortButton->setEnabled(false);
    } else {
        keymgmtStatus->setText(QString("Loaded %1 lines from PEM file").arg(pemLines.size()));
        keymgmtStatus->setStyleSheet("color: green;");
        uploadButton->setEnabled(true);
        abortButton->setEnabled(true); // Enable abort button when file is loaded
        
        // Log the first and last lines for debugging
        if (!pemLines.isEmpty()) {
            logMessage(QString("PEM file structure - First line: '%1'").arg(pemLines.first()), "[DEBUG] ");
            logMessage(QString("PEM file structure - Last line: '%2'").arg(pemLines.last()), "[DEBUG] ");
        }
    }
}

void MainWindow::uploadCertificate()
{
    if (!isConnected) {
        QMessageBox::warning(this, "Not Connected", "Please connect to the device first.");
        return;
    }
    
    if (pemLines.isEmpty()) {
        QMessageBox::warning(this, "No File", "Please select a PEM file first.");
        return;
    }
    
    // Start the upload process
    currentPemLine = 0;
    uploadButton->setEnabled(false);
    abortButton->setEnabled(true); // Keep abort enabled during upload
    uploadProgress->setVisible(true);
    uploadProgress->setMaximum(pemLines.size());
    uploadProgress->setValue(0);
    keymgmtStatus->setText("Starting upload...");
    keymgmtStatus->setStyleSheet("color: blue;");
    
    // Send abort command first
    QString abortCommand = "keymgmt abort\n";
    QByteArray abortData = abortCommand.toUtf8();
    serialPort->write(abortData);
    
    // Reset auto-clear timer when starting upload
    resetAutoClearTimer();
    
    // Start timer for line-by-line upload (500ms delay between lines)
    keymgmtTimer->setInterval(500);
    keymgmtTimer->start();
}

void MainWindow::sendKeymgmtLine(const QString &line, int secTag, const QString &certType)
{
    // Quote the line to handle special characters like dashes
    QString quotedLine = line;
    if (!quotedLine.isEmpty()) {
        // Escape quotes and wrap in quotes to handle shell interpretation
        quotedLine = quotedLine.replace("\"", "\\\"");
        quotedLine = "\"" + quotedLine + "\"";
    }
    
    QString command = QString("keymgmt put %1 %2 %3\n").arg(secTag).arg(certType).arg(quotedLine);
    QByteArray data = command.toUtf8();
    serialPort->write(data);
    
    // Log the command being sent with line content for debugging
    QString logLine = line;
    if (logLine.isEmpty()) {
        logLine = "(empty line)";
    }
    logMessage(QString("Sent keymgmt line %1/%2: %3").arg(currentPemLine + 1).arg(pemLines.size()).arg(logLine), "> ");
    
    // Reset auto-clear timer when keymgmt command is sent
    resetAutoClearTimer();
}

void MainWindow::updateKeymgmtProgress(int current, int total)
{
    uploadProgress->setValue(current);
    keymgmtStatus->setText(QString("Uploading... %1/%2 lines").arg(current).arg(total));
}

void MainWindow::abortUpload()
{
    bool wasUploading = keymgmtTimer->isActive();
    
    // Stop the upload timer if it's running
    if (wasUploading) {
        keymgmtTimer->stop();
    }
    
    // Send abort command to clear the buffer
    QString abortCommand = "keymgmt abort\n";
    QByteArray abortData = abortCommand.toUtf8();
    serialPort->write(abortData);
    
    // Reset UI state
    uploadButton->setEnabled(true);
    abortButton->setEnabled(true); // Keep abort enabled
    uploadProgress->setVisible(false);
    
    if (wasUploading) {
        keymgmtStatus->setText("Upload aborted");
        keymgmtStatus->setStyleSheet("color: orange;");
        logMessage("Certificate upload aborted by user", "[INFO] ");
    } else {
        keymgmtStatus->setText("Buffer cleared");
        keymgmtStatus->setStyleSheet("color: blue;");
        logMessage("Key management buffer cleared", "[INFO] ");
    }
    
    // Reset auto-clear timer
    resetAutoClearTimer();
}

void MainWindow::addCommandToHistory(const QString &command)
{
    // Don't add empty commands or duplicates of the last command
    if (command.isEmpty() || (commandHistory.size() > 0 && commandHistory.last() == command)) {
        return;
    }
    
    // Add to history
    commandHistory.append(command);
    
    // Limit history to 100 commands
    if (commandHistory.size() > 100) {
        commandHistory.removeFirst();
    }
    
    // Reset history index to end
    historyIndex = commandHistory.size();
}

void MainWindow::navigateCommandHistory(int direction)
{
    if (commandHistory.isEmpty()) {
        return;
    }
    
    if (direction < 0) {
        // Go back in history (up arrow)
        if (historyIndex > 0) {
            historyIndex--;
            commandInput->setText(commandHistory[historyIndex]);
        }
    } else {
        // Go forward in history (down arrow)
        if (historyIndex < commandHistory.size() - 1) {
            historyIndex++;
            commandInput->setText(commandHistory[historyIndex]);
        } else if (historyIndex == commandHistory.size() - 1) {
            // At the end of history, show current input
            historyIndex = commandHistory.size();
            commandInput->setText(currentInput);
        }
    }
    
    // Move cursor to end of text
    commandInput->setCursorPosition(commandInput->text().length());
}

void MainWindow::resetAutoClearTimer()
{
    // Stop the current timer and restart it
    autoClearTimer->stop();
    autoClearTimer->start(15000); // 15 seconds
}

void MainWindow::logCommandToOutput(const QString &command)
{
    // Add command to command output with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formattedCommand = QString("[%1] > %2").arg(timestamp, command);
    
    commandOutput->insertPlainText(formattedCommand + "\n");
    
    // Auto-scroll to bottom
    QScrollBar *scrollBar = commandOutput->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void MainWindow::clearCommandOutput()
{
    commandOutput->clear();
    logMessage("Command output cleared", "[INFO] ");
}

void MainWindow::autoClearCommandOutput()
{
    // Only auto-clear if there's content in the command output
    if (!commandOutput->toPlainText().isEmpty()) {
        commandOutput->clear();
        logMessage("Command output auto-cleared (15s interval)", "[INFO] ");
    }
}

void MainWindow::flushIncompleteData()
{
    // If we have accumulated data that hasn't been processed, force process it
    if (!accumulatedData.isEmpty()) {
        QString cleanedData = cleanAnsiCodes(accumulatedData);
        QString filteredData = filterShellPrompts(cleanedData);
        
        if (!filteredData.isEmpty()) {
            // Replace carriage returns with newlines for proper display
            filteredData = filteredData.replace("\r\n", "\n");
            filteredData = filteredData.replace("\r", "\n");
            
            // Process whatever we have with better line reconstruction
            QStringList lines = filteredData.split('\n', Qt::SkipEmptyParts);
            QStringList logLines, commandLines;
            
            for (const QString &line : lines) {
                QString trimmedLine = line.trimmed();
                if (trimmedLine.isEmpty()) continue;
                
                // Check if this line is too long (likely fragmented)
                if (trimmedLine.length() > MAX_LINE_LENGTH) {
                    // Split long lines that might be concatenated fragments
                    QStringList fragments = splitLongLine(trimmedLine);
                    for (const QString &fragment : fragments) {
                        if (fragment.isEmpty()) continue;
                        
                        // Check if this fragment is a log message
                        if (isLogMessage(fragment)) {
                            logLines.append(fragment);
                        } else {
                            commandLines.append(fragment);
                        }
                    }
                } else {
                    // Normal line processing
                    if (isLogMessage(trimmedLine)) {
                        logLines.append(trimmedLine);
                    } else {
                        commandLines.append(trimmedLine);
                    }
                }
            }
            
            // Send log lines to terminal
            if (!logLines.isEmpty()) {
                logMessage(logLines.join('\n'), "");
            }
            
            // Send command lines to command interface
            if (!commandLines.isEmpty()) {
                parseCommandOutput(commandLines.join('\n'));
            }
        }
        
        // Clear accumulated data
        accumulatedData.clear();
    }
}

QStringList MainWindow::splitLongLine(const QString &line)
{
    QStringList fragments;
    QString currentFragment;
    
    // Split on common delimiters that indicate separate messages
    QStringList parts = line.split(QRegularExpression(R"(\s{2,}|\|\s*|\]\s*\[|x\s*)"), Qt::SkipEmptyParts);
    
    for (const QString &part : parts) {
        QString trimmedPart = part.trimmed();
        if (trimmedPart.isEmpty()) continue;
        
        // Filter out single characters and very short fragments
        if (trimmedPart.length() <= 2) {
            continue; // Skip single characters and very short fragments
        }
        
        // If this part looks like a complete message, add it as a fragment
        if (trimmedPart.length() <= MAX_LINE_LENGTH && 
            (trimmedPart.contains('[') || trimmedPart.contains('<') || 
             trimmedPart.length() > 10)) {
            fragments.append(trimmedPart);
        } else {
            // Accumulate smaller parts
            if (!currentFragment.isEmpty()) {
                currentFragment += " ";
            }
            currentFragment += trimmedPart;
            
            // If accumulated fragment is long enough, add it
            if (currentFragment.length() >= 20) {
                fragments.append(currentFragment);
                currentFragment.clear();
            }
        }
    }
    
    // Add any remaining fragment (only if it's substantial)
    if (!currentFragment.isEmpty() && currentFragment.length() >= 5) {
        fragments.append(currentFragment);
    }
    
    return fragments;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == commandInput && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        
        if (keyEvent->key() == Qt::Key_Up) {
            navigateCommandHistory(-1); // Go back in history
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            navigateCommandHistory(1);  // Go forward in history
            return true;
        }
    }
    
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setupKeyManagementUI()
{
    keymgmtWidget = new QWidget;
    QVBoxLayout *keymgmtLayout = new QVBoxLayout(keymgmtWidget);
    
    // File selection
    QHBoxLayout *fileLayout = new QHBoxLayout;
    QLabel *fileLabel = new QLabel("PEM File:");
    pemFileEdit = new QLineEdit;
    pemFileEdit->setPlaceholderText("Select PEM certificate/key file...");
    pemFileEdit->setReadOnly(true);
    selectPemButton = new QPushButton("Browse");
    selectPemButton->setFixedWidth(80);
    
    fileLayout->addWidget(fileLabel);
    fileLayout->addWidget(pemFileEdit);
    fileLayout->addWidget(selectPemButton);
    
    connect(selectPemButton, &QPushButton::clicked, this, &MainWindow::selectPemFile);
    
    // Certificate type selection
    QHBoxLayout *typeLayout = new QHBoxLayout;
    QLabel *typeLabel = new QLabel("Type:");
    certTypeCombo = new QComboBox;
    certTypeCombo->addItems({"CA", "Certificate", "Key"});
    
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(certTypeCombo);
    typeLayout->addStretch();
    
    // Security tag selection
    QHBoxLayout *tagLayout = new QHBoxLayout;
    QLabel *tagLabel = new QLabel("Security Tag:");
    mqttRadio = new QRadioButton("MQTT (42)");
    fotaRadio = new QRadioButton("FOTA (44)");
    mqttRadio->setChecked(true);
    
    tagLayout->addWidget(tagLabel);
    tagLayout->addWidget(mqttRadio);
    tagLayout->addWidget(fotaRadio);
    tagLayout->addStretch();
    
    // Upload and abort buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    
    uploadButton = new QPushButton("Upload Certificate");
    uploadButton->setEnabled(false);
    
    abortButton = new QPushButton("Abort Upload");
    abortButton->setEnabled(false);
    
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::uploadCertificate);
    connect(abortButton, &QPushButton::clicked, this, &MainWindow::abortUpload);
    
    buttonLayout->addWidget(uploadButton);
    buttonLayout->addWidget(abortButton);
    
    // Progress bar
    uploadProgress = new QProgressBar;
    uploadProgress->setVisible(false);
    
    // Status label
    keymgmtStatus = new QLabel("Select a PEM file to upload");
    keymgmtStatus->setStyleSheet("color: blue;");
    
    // Add all layouts to main layout
    keymgmtLayout->addLayout(fileLayout);
    keymgmtLayout->addLayout(typeLayout);
    keymgmtLayout->addLayout(tagLayout);
    keymgmtLayout->addLayout(buttonLayout);
    keymgmtLayout->addWidget(uploadProgress);
    keymgmtLayout->addWidget(keymgmtStatus);
    keymgmtLayout->addStretch();
}

bool MainWindow::isLikelyCorruptedLogLine(const QString &line)
{
    QString trimmed = line.trimmed();
    
    // Skip empty lines
    if (trimmed.isEmpty()) {
        return false;
    }
    
    // Check for common log message patterns that might have lost their tags
    // These are typical log message content without the log level tags
    
    // Check for MQTT-related content (common in your logs)
    if (trimmed.contains("MQTT", Qt::CaseInsensitive) || 
        trimmed.contains("LTE", Qt::CaseInsensitive) ||
        trimmed.contains("GNSS", Qt::CaseInsensitive) ||
        trimmed.contains("Thread", Qt::CaseInsensitive) ||
        trimmed.contains("ms", Qt::CaseInsensitive)) {
        return true; // Likely a corrupted log message
    }
    
    // Check for timestamp patterns (from terminal program)
    if (trimmed.contains(QRegularExpression(R"(\[[0-9]{1,2}:[0-9]{2}:[0-9]{2}\])"))) {
        return true; // Contains timestamp, likely corrupted log
    }
    
    // Check for lines that look like log content but lack tags
    if (trimmed.contains("publish", Qt::CaseInsensitive) ||
        trimmed.contains("fix", Qt::CaseInsensitive) ||
        trimmed.contains("since", Qt::CaseInsensitive) ||
        trimmed.contains("new", Qt::CaseInsensitive)) {
        return true; // Likely log content without tags
    }
    
    // Check for lines that are too short to be meaningful commands
    if (trimmed.length() < 10 && !trimmed.contains("help", Qt::CaseInsensitive)) {
        return true; // Too short, likely corrupted log fragment
    }
    
    // Everything else is considered a legitimate command response
    return false;
} 