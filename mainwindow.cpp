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
    , isLoggedIn(false)
    , loginTimeoutTimer(new QTimer(this))
    , loginRetryCount(0)
    , waitingForLoginTest(false)
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
    
    // Set up timer for login timeout
    connect(loginTimeoutTimer, &QTimer::timeout, this, &MainWindow::handleLoginTimeout);
    loginTimeoutTimer->setSingleShot(true);
    
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
    setGeometry(100, 100, 1200, 800); // Larger window for tab layout
    
    createMenuBar();
    
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    createToolbar();
    createTabInterface();
    
    mainLayout->addWidget(toolbarWidget);
    mainLayout->addWidget(mainTabWidget);
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
    
    // Login button
    QPushButton *loginButton = new QPushButton("Login");
    loginButton->setFixedWidth(60);
    loginButton->setFixedHeight(20);
    loginButton->setToolTip("Login to device (required for most commands)");
    toolbarLayout->addWidget(loginButton);
    connect(loginButton, &QPushButton::clicked, this, &MainWindow::showLoginDialog);
    
    toolbarLayout->addSpacing(10);
    
    // Status indicator
    statusLabel = new QLabel("Disconnected");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");
    statusLabel->setFixedWidth(180); // Increased width further for longer status text
    statusLabel->setFixedHeight(20);
    toolbarLayout->addWidget(statusLabel);
}

void MainWindow::createTabInterface()
{
    mainTabWidget = new QTabWidget;
    
    // Setup all tabs
    setupMenuTab();
    setupSerialTerminalTab();
    setupCommandInterfaceTab();
    setupKeyManagementTab();
    setupConfigTab();
    setupBackupTab();
    
    // Set Menu tab as default (index 0)
    mainTabWidget->setCurrentIndex(0);
}

void MainWindow::setupMenuTab()
{
    menuTab = new QWidget;
    QVBoxLayout *menuLayout = new QVBoxLayout(menuTab);
    
    // Welcome header
    QLabel *welcomeLabel = new QLabel("Configuration GUI");
    welcomeLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #2c3e50; margin: 20px;");
    welcomeLabel->setAlignment(Qt::AlignCenter);
    menuLayout->addWidget(welcomeLabel);
    
    // Description
    QLabel *descriptionLabel = new QLabel("Welcome to the Configuration GUI for Nordic devices.\n"
                                         "This application provides tools for device configuration, "
                                         "certificate management, and serial communication.");
    descriptionLabel->setStyleSheet("font-size: 14px; color: #34495e; margin: 10px;");
    descriptionLabel->setAlignment(Qt::AlignCenter);
    descriptionLabel->setWordWrap(true);
    menuLayout->addWidget(descriptionLabel);
    
    // Available tabs info
    QGroupBox *tabsInfoGroup = new QGroupBox("Available Features");
    QVBoxLayout *tabsInfoLayout = new QVBoxLayout(tabsInfoGroup);
    
    QStringList tabDescriptions = {
        "📡 <b>Serial Terminal:</b> Direct serial communication with the device",
        "💻 <b>Command Interface:</b> Send shell commands and view responses",
        "🔑 <b>Key Management:</b> Upload certificates and keys for secure communication",
        "⚙️ <b>Config:</b> Device configuration settings (coming soon)",
        "💾 <b>Backup:</b> Backup and restore device settings (coming soon)"
    };
    
    for (const QString &desc : tabDescriptions) {
        QLabel *tabLabel = new QLabel(desc);
        tabLabel->setStyleSheet("font-size: 12px; margin: 5px;");
        tabLabel->setTextFormat(Qt::RichText);
        tabsInfoLayout->addWidget(tabLabel);
    }
    
    menuLayout->addWidget(tabsInfoGroup);
    menuLayout->addStretch();
    
    // Version info
    QLabel *versionLabel = new QLabel("Version 1.0 | Nordic Configuration GUI");
    versionLabel->setStyleSheet("font-size: 10px; color: #7f8c8d; margin: 10px;");
    versionLabel->setAlignment(Qt::AlignCenter);
    menuLayout->addWidget(versionLabel);
    
    mainTabWidget->addTab(menuTab, "Menu");
}

void MainWindow::setupSerialTerminalTab()
{
    serialTerminalTab = new QWidget;
    QVBoxLayout *terminalLayout = new QVBoxLayout(serialTerminalTab);
    
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
    
    mainTabWidget->addTab(serialTerminalTab, "Serial Terminal");
}

void MainWindow::setupCommandInterfaceTab()
{
    commandInterfaceTab = new QWidget;
    QVBoxLayout *commandLayout = new QVBoxLayout(commandInterfaceTab);
    
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
    
    mainTabWidget->addTab(commandInterfaceTab, "Command Interface");
}

void MainWindow::setupConfigTab()
{
    configTab = new QWidget;
    QVBoxLayout *configLayout = new QVBoxLayout(configTab);
    
    QLabel *comingSoonLabel = new QLabel("Configuration settings will be available in a future update.");
    comingSoonLabel->setStyleSheet("font-size: 16px; color: #7f8c8d; margin: 50px;");
    comingSoonLabel->setAlignment(Qt::AlignCenter);
    configLayout->addWidget(comingSoonLabel);
    
    mainTabWidget->addTab(configTab, "Config");
}

void MainWindow::setupBackupTab()
{
    backupTab = new QWidget;
    QVBoxLayout *backupLayout = new QVBoxLayout(backupTab);
    
    // Header
    QLabel *headerLabel = new QLabel("Backup & Restore");
    headerLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #2c3e50; margin: 10px;");
    headerLabel->setAlignment(Qt::AlignCenter);
    backupLayout->addWidget(headerLabel);
    
    // Explanation
    QLabel *explanationLabel = new QLabel(
        "This section allows you to backup and restore device configurations.\n\n"
        "<b>Slot 0:</b> Primary slot (currently active configuration)\n"
        "<b>Slot 1:</b> Backup slot (stored backup configuration)\n\n"
        "Use <b>Save</b> to backup your current configuration to slot 1.\n"
        "Use <b>Restore</b> to restore the backup configuration to slot 0."
    );
    explanationLabel->setStyleSheet("font-size: 12px; color: #34495e; margin: 10px; padding: 10px; background-color: #f8f9fa; border: 1px solid #dee2e6; border-radius: 5px;");
    explanationLabel->setAlignment(Qt::AlignLeft);
    explanationLabel->setWordWrap(true);
    explanationLabel->setTextFormat(Qt::RichText);
    backupLayout->addWidget(explanationLabel);
    
    backupLayout->addSpacing(20);
    
    // Save button section
    QGroupBox *saveGroup = new QGroupBox("Save Current Configuration");
    QVBoxLayout *saveLayout = new QVBoxLayout(saveGroup);
    
    QLabel *saveLabel = new QLabel("This will copy the current configuration (Slot 0) to the backup slot (Slot 1).");
    saveLabel->setStyleSheet("font-size: 11px; color: #495057; margin: 5px;");
    saveLabel->setWordWrap(true);
    saveLayout->addWidget(saveLabel);
    
    QPushButton *saveButton = new QPushButton("Save Configuration");
    saveButton->setStyleSheet("QPushButton { background-color: #28a745; color: white; border: none; padding: 10px; border-radius: 5px; font-weight: bold; } QPushButton:hover { background-color: #218838; } QPushButton:pressed { background-color: #1e7e34; }");
    saveButton->setFixedHeight(40);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveConfiguration);
    saveLayout->addWidget(saveButton);
    
    backupLayout->addWidget(saveGroup);
    
    backupLayout->addSpacing(20);
    
    // Restore button section
    QGroupBox *restoreGroup = new QGroupBox("Restore Backup Configuration");
    QVBoxLayout *restoreLayout = new QVBoxLayout(restoreGroup);
    
    QLabel *restoreWarningLabel = new QLabel("⚠️ <b>Warning:</b> This will overwrite your current configuration with the backup. This action cannot be undone.");
    restoreWarningLabel->setStyleSheet("font-size: 11px; color: #dc3545; margin: 5px; padding: 8px; background-color: #f8d7da; border: 1px solid #f5c6cb; border-radius: 3px;");
    restoreWarningLabel->setWordWrap(true);
    restoreWarningLabel->setTextFormat(Qt::RichText);
    restoreLayout->addWidget(restoreWarningLabel);
    
    QPushButton *restoreButton = new QPushButton("Restore Configuration");
    restoreButton->setStyleSheet("QPushButton { background-color: #dc3545; color: white; border: none; padding: 10px; border-radius: 5px; font-weight: bold; } QPushButton:hover { background-color: #c82333; } QPushButton:pressed { background-color: #bd2130; }");
    restoreButton->setFixedHeight(40);
    connect(restoreButton, &QPushButton::clicked, this, &MainWindow::restoreConfiguration);
    restoreLayout->addWidget(restoreButton);
    
    backupLayout->addWidget(restoreGroup);
    
    backupLayout->addStretch();
    
    mainTabWidget->addTab(backupTab, "Backup");
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
        logMessage("Establishing connection...", "[INFO] ");
        
        isConnected = true;
        isLoggedIn = false; // Reset login state on new connection
        connectButton->setText("Disconnect");
        statusLabel->setText("Connecting...");
        statusLabel->setStyleSheet("color: orange; font-weight: bold;");
        
        // Start timer for data checking immediately to monitor for "messages dropped"
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
    isLoggedIn = false; // Reset login state on disconnect
    loginTimeoutTimer->stop(); // Stop login timeout timer
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
            
            // Refresh login if we're logged in (extends timeout)
            if (isLoggedIn) {
                refreshLogin();
            }
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
    
    // Check for login response
    checkLoginResponse(data);
    
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
    
    if (!isLoggedIn) {
        QMessageBox::warning(this, "Login Required", 
            "Certificate upload requires authentication. Please login first.");
        showLoginDialog();
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
    
    // Fix: Use "cert" instead of "certificate" for the command
    QString commandType = certType;
    if (commandType.toLower() == "certificate") {
        commandType = "cert";
    }
    QString command = QString("keymgmt put %1 %2 %3\n").arg(secTag).arg(commandType).arg(quotedLine);
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

void MainWindow::setupKeyManagementTab()
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
    
    mainTabWidget->addTab(keymgmtWidget, "Key Management");
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

void MainWindow::saveConfiguration()
{
    if (!isConnected) {
        QMessageBox::warning(this, "Not Connected", "Please connect to the device first.");
        return;
    }
    
    // Show confirmation dialog
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Save Configuration", 
        "This will copy the current configuration (Slot 0) to the backup slot (Slot 1).\n\n"
        "Do you want to proceed?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Send backup command: backup copyinto 0 1
        QString command = "backup copyinto 0 1\n";
        QByteArray data = command.toUtf8();
        serialPort->write(data);
        
        logMessage("Sending backup command: backup copyinto 0 1", "> ");
        logMessage("Configuration backup initiated", "[INFO] ");
        
        // Reset auto-clear timer
        resetAutoClearTimer();
    }
}

void MainWindow::showLoginDialog()
{
    if (!isConnected) {
        QMessageBox::warning(this, "Not Connected", "Please connect to the device first.");
        return;
    }
    
    // Create custom dialog
    QDialog *loginDialog = new QDialog(this);
    loginDialog->setWindowTitle("Device Login");
    loginDialog->setModal(true);
    loginDialog->setFixedSize(350, 200);
    
    QVBoxLayout *dialogLayout = new QVBoxLayout(loginDialog);
    
    // Title
    QLabel *titleLabel = new QLabel("Enter Device Password");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #2c3e50;");
    titleLabel->setAlignment(Qt::AlignCenter);
    dialogLayout->addWidget(titleLabel);
    
    // Password input
    QLabel *passwordLabel = new QLabel("Password:");
    QLineEdit *passwordInput = new QLineEdit;
    passwordInput->setEchoMode(QLineEdit::Password);
    passwordInput->setPlaceholderText("Enter device password");
    
    QHBoxLayout *inputLayout = new QHBoxLayout;
    inputLayout->addWidget(passwordLabel);
    inputLayout->addWidget(passwordInput);
    dialogLayout->addLayout(inputLayout);
    
    // Status label
    QLabel *statusLabel = new QLabel("Ready to login");
    statusLabel->setStyleSheet("color: blue; font-size: 11px;");
    statusLabel->setAlignment(Qt::AlignCenter);
    dialogLayout->addWidget(statusLabel);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    
    QPushButton *loginButton = new QPushButton("Login");
    QPushButton *retryButton = new QPushButton("Retry");
    QPushButton *closeButton = new QPushButton("Close");
    
    loginButton->setDefault(true);
    retryButton->setEnabled(false);
    
    buttonLayout->addWidget(loginButton);
    buttonLayout->addWidget(retryButton);
    buttonLayout->addWidget(closeButton);
    dialogLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(closeButton, &QPushButton::clicked, loginDialog, &QDialog::reject);
    connect(loginButton, &QPushButton::clicked, [=]() {
        QString password = passwordInput->text();
        if (password.isEmpty()) {
            statusLabel->setText("Please enter a password");
            statusLabel->setStyleSheet("color: red; font-size: 11px;");
            return;
        }
        
        loginButton->setEnabled(false);
        retryButton->setEnabled(false);
        statusLabel->setText("Logging in... Please wait (may take several seconds)");
        statusLabel->setStyleSheet("color: blue; font-size: 11px;");
        
        performLogin(password);
    });
    
    connect(retryButton, &QPushButton::clicked, [=]() {
        QString password = passwordInput->text();
        if (password.isEmpty()) {
            statusLabel->setText("Please enter a password");
            statusLabel->setStyleSheet("color: red; font-size: 11px;");
            return;
        }
        
        retryButton->setEnabled(false);
        statusLabel->setText("Retrying login...");
        statusLabel->setStyleSheet("color: blue; font-size: 11px;");
        
        performLogin(password);
    });
    
    // Store dialog reference for status updates
    loginDialog->setProperty("statusLabel", QVariant::fromValue(statusLabel));
    loginDialog->setProperty("loginButton", QVariant::fromValue(loginButton));
    loginDialog->setProperty("retryButton", QVariant::fromValue(retryButton));
    
    // Show dialog
    loginDialog->exec();
}

void MainWindow::performLogin(const QString &password)
{
    if (!isConnected) {
        return;
    }
    
    currentPassword = password;
    loginRetryCount = 0;
    waitingForLoginTest = true;
    pendingLoginPassword = password;
    
    // Send the real password first
    QString command = QString("login %1\n").arg(password);
    QByteArray data = command.toUtf8();
    serialPort->write(data);
    
    logMessage(QString("Sending login command (attempt %1/%2)").arg(loginRetryCount + 1).arg(MAX_LOGIN_RETRIES), "> ");
    
    // Start timeout timer
    loginTimeoutTimer->start(LOGIN_RETRY_TIMEOUT_MS);
}

void MainWindow::sendLoginTestCommand()
{
    // First send "login test" to check if already authenticated
    QString testCommand = "login test\n";
    QByteArray testData = testCommand.toUtf8();
    serialPort->write(testData);
    
    logMessage("Sending login test command", "> ");
}

void MainWindow::checkLoginResponse(const QString &response)
{
    QString trimmedResponse = response.trimmed();
    
    // Check for "messages dropped" indicating connection is ready
    if (trimmedResponse.contains("messages dropped", Qt::CaseInsensitive)) {
        logMessage("Connection established - messages dropped detected", "[INFO] ");
        statusLabel->setText("Connected (Login Required)");
        statusLabel->setStyleSheet("color: orange; font-weight: bold;");
        logMessage("Nordic terminal ready for commands", "[INFO] ");
        
        // If we're waiting for login test after sending real password, send it now
        if (waitingForLoginTest && !pendingLoginPassword.isEmpty()) {
            sendLoginTestCommand();
        }
    }
    
    // Check for successful login
    if (trimmedResponse.contains("OK", Qt::CaseInsensitive)) {
        isLoggedIn = true;
        lastLoginTime = QDateTime::currentDateTime();
        loginTimeoutTimer->stop();
        loginTimeoutTimer->start(LOGIN_TIMEOUT_MS); // Start 30-second timeout
        
        logMessage("Login successful", "[INFO] ");
        statusLabel->setText("Connected & Logged In");
        statusLabel->setStyleSheet("color: green; font-weight: bold;");
        
        // Close any open login dialog
        QWidgetList widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : widgets) {
            if (widget->windowTitle() == "Device Login") {
                widget->close();
                break;
            }
        }
    }
    // Check for "Already Logged in" - this is also a success case
    else if (trimmedResponse.contains("Already Logged in", Qt::CaseInsensitive) || 
             trimmedResponse.contains("Already Logged In", Qt::CaseInsensitive)) {
        isLoggedIn = true;
        lastLoginTime = QDateTime::currentDateTime();
        loginTimeoutTimer->stop();
        loginTimeoutTimer->start(LOGIN_TIMEOUT_MS); // Start 30-second timeout
        
        logMessage("Already logged in", "[INFO] ");
        statusLabel->setText("Connected & Logged In");
        statusLabel->setStyleSheet("color: green; font-weight: bold;");
        
        // Close any open login dialog
        QWidgetList widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : widgets) {
            if (widget->windowTitle() == "Device Login") {
                widget->close();
                break;
            }
        }
    }
    // Check for "Already authenticated" from login test command
    else if (trimmedResponse.contains("Already authenticated", Qt::CaseInsensitive)) {
        isLoggedIn = true;
        lastLoginTime = QDateTime::currentDateTime();
        loginTimeoutTimer->stop();
        loginTimeoutTimer->start(LOGIN_TIMEOUT_MS); // Start 30-second timeout
        waitingForLoginTest = false;
        
        logMessage("Already authenticated", "[INFO] ");
        statusLabel->setText("Connected & Logged In");
        statusLabel->setStyleSheet("color: green; font-weight: bold;");
        
        // Close any open login dialog
        QWidgetList widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : widgets) {
            if (widget->windowTitle() == "Device Login") {
                widget->close();
                break;
            }
        }
    }
    // Check for shell prompt or serial input resuming after login test
    else if (waitingForLoginTest && (trimmedResponse.contains("uart:~$", Qt::CaseInsensitive) || 
                                    trimmedResponse.contains("dev>", Qt::CaseInsensitive) ||
                                    trimmedResponse.contains("login>", Qt::CaseInsensitive))) {
        // Serial input has resumed, send the actual login command
        waitingForLoginTest = false;
        
        if (!isLoggedIn && !pendingLoginPassword.isEmpty()) {
            // Send actual login command
            QString command = QString("login %1\n").arg(pendingLoginPassword);
            QByteArray data = command.toUtf8();
            serialPort->write(data);
            
            logMessage(QString("Sending login command (attempt %1/%2)").arg(loginRetryCount + 1).arg(MAX_LOGIN_RETRIES), "> ");
            pendingLoginPassword.clear();
        }
    }
    // Check for "Not Logged In" - this is a failure case
    else if (trimmedResponse.contains("Not Logged In", Qt::CaseInsensitive) || 
             trimmedResponse.contains("Not Logged in", Qt::CaseInsensitive)) {
        loginRetryCount++;
        logMessage(QString("Login failed - Not Logged In (attempt %1/%2)").arg(loginRetryCount).arg(MAX_LOGIN_RETRIES), "[ERROR] ");
        
        // Update dialog status
        updateLoginDialogStatus("Login failed - Not Logged In", "red");
        
        if (loginRetryCount >= MAX_LOGIN_RETRIES) {
            handleLoginTimeout();
        } else {
            // Enable retry button after delay
            QTimer::singleShot(LOGIN_RETRY_TIMEOUT_MS, this, [this]() {
                updateLoginDialogStatus("Login failed - click Retry to try again", "orange");
                enableLoginDialogRetry();
            });
        }
    }
    // Check for other error cases
    else if (trimmedResponse.contains("ERROR", Qt::CaseInsensitive) || 
             trimmedResponse.contains("FAIL", Qt::CaseInsensitive) ||
             trimmedResponse.contains("Invalid", Qt::CaseInsensitive)) {
        loginRetryCount++;
        logMessage(QString("Login failed - %1 (attempt %2/%3)").arg(trimmedResponse).arg(loginRetryCount).arg(MAX_LOGIN_RETRIES), "[ERROR] ");
        
        // Update dialog status
        updateLoginDialogStatus(QString("Login failed - %1").arg(trimmedResponse), "red");
        
        if (loginRetryCount >= MAX_LOGIN_RETRIES) {
            handleLoginTimeout();
        } else {
            // Enable retry button after delay
            QTimer::singleShot(LOGIN_RETRY_TIMEOUT_MS, this, [this]() {
                updateLoginDialogStatus("Login failed - click Retry to try again", "orange");
                enableLoginDialogRetry();
            });
        }
    }
    // If we get here, it might be a partial response or unrelated data
    // We'll let the timeout handle it
}

void MainWindow::handleLoginTimeout()
{
    isLoggedIn = false;
    loginTimeoutTimer->stop();
    
    logMessage("Login timeout - authentication required", "[WARNING] ");
    statusLabel->setText("Connected (Login Required)");
    statusLabel->setStyleSheet("color: orange; font-weight: bold;");
    
    // Update any open login dialog
    QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        if (widget->windowTitle() == "Device Login") {
            QLabel *statusLabel = widget->findChild<QLabel*>();
            if (statusLabel) {
                statusLabel->setText("Login timeout - please retry");
                statusLabel->setStyleSheet("color: red; font-size: 11px;");
            }
            break;
        }
    }
}

bool MainWindow::isLoginRequired(const QString &command)
{
    // Commands that don't require login
    QStringList noLoginCommands = {"backup"};
    
    for (const QString &noLoginCmd : noLoginCommands) {
        if (command.trimmed().toLower().startsWith(noLoginCmd.toLower())) {
            return false;
        }
    }
    
    return true;
}

void MainWindow::refreshLogin()
{
    if (isLoggedIn && !currentPassword.isEmpty()) {
        performLogin(currentPassword);
    }
}

void MainWindow::updateLoginDialogStatus(const QString &message, const QString &color)
{
    // Find and update the login dialog status label
    QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        if (widget->windowTitle() == "Device Login") {
            QLabel *statusLabel = widget->findChild<QLabel*>();
            if (statusLabel) {
                statusLabel->setText(message);
                statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(color));
            }
            break;
        }
    }
}

void MainWindow::enableLoginDialogRetry()
{
    // Find and enable the retry button in the login dialog
    QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        if (widget->windowTitle() == "Device Login") {
            QPushButton *retryButton = widget->findChild<QPushButton*>();
            if (retryButton && retryButton->text() == "Retry") {
                retryButton->setEnabled(true);
            }
            break;
        }
    }
}

void MainWindow::restoreConfiguration()
{
    if (!isConnected) {
        QMessageBox::warning(this, "Not Connected", "Please connect to the device first.");
        return;
    }
    
    // Show warning dialog
    QMessageBox::StandardButton reply = QMessageBox::warning(this, "Restore Configuration", 
        "⚠️ <b>Warning:</b> This will overwrite your current configuration with the backup.\n\n"
        "This action cannot be undone. Are you sure you want to proceed?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Send restore command: backup copyinto 1 0
        QString command = "backup copyinto 1 0\n";
        QByteArray data = command.toUtf8();
        serialPort->write(data);
        
        logMessage("Sending restore command: backup copyinto 1 0", "> ");
        logMessage("Configuration restore initiated", "[INFO] ");
        
        // Reset auto-clear timer
        resetAutoClearTimer();
    }
} 