#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QJsonObject>
#include "mainwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QSettings>
#include <QDateTime>
#include <QTimer>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextStream>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QPalette>

namespace {
const QString kAppOrg = "Nexus";
const QString kAppName = "Nexus Launcher";

const QString accent = "#6c8cff";
const QString background = "#090b10";
const QString panel = "#11141b";
const QString panel2 = "#171b23";
const QString text = "#f2f4f8";
const QString muted = "#9aa3b2";
const QString border = "#242a35";

QString keyForPath(int g)
{
    return QString("Game%1Path").arg(g);
}

QString keyForVersion1(int g)
{
    return QString("Game%1Version1").arg(g);
}

QString keyForVersion2(int g)
{
    return QString("Game%1Version2").arg(g);
}

QString commandFromEnv()
{
    const QString env = qEnvironmentVariable("NEXUS_WINE_COMMAND").trimmed();
    return env.isEmpty() ? QStringLiteral("wine") : env;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_updater(this),
      m_discord(this)
{
    setWindowTitle(kAppName);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);
    resize(1120, 700);
    setMinimumSize(980, 620);

    m_launcherStart = QDateTime::currentSecsSinceEpoch();

    buildUi();

    QSettings s(kAppOrg, kAppName);
    constexpr const char kDiscordClientId[] = "1546908541565280349";
    m_discord.setClientId(QString::fromLatin1(kDiscordClientId));
    m_discord.startLauncherPresence();

    connect(&m_pollTimer, &QTimer::timeout, this, &MainWindow::pollGames);
    m_pollTimer.start(2500);
    pollGames();

    selectGame(IW4X);
}

MainWindow::~MainWindow()
{
    m_discord.clear();
}


void MainWindow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && e->position().y() < 52) {
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
        e->accept();
        return;
    }
    QMainWindow::mousePressEvent(e);
}

void MainWindow::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) {
        move(e->globalPosition().toPoint() - m_dragOffset);
        e->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(e);
}

void MainWindow::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_dragging = false;
    QMainWindow::mouseReleaseEvent(e);
}

void MainWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(background));
}

void MainWindow::buildUi()
{
    m_root = new QWidget(this);
    m_root->setObjectName("root");
    m_root->setStyleSheet(QString(
        "#root { background:%1; border:1px solid %2; border-radius:22px; }"
        "QLabel { color:%3; }"
    ).arg(background, border, text));
    setCentralWidget(m_root);

    auto* rootLayout = new QHBoxLayout(m_root);
    rootLayout->setContentsMargins(0,0,0,0);
    rootLayout->setSpacing(0);

    m_sidebar = new QWidget;
    m_sidebar->setFixedWidth(92);
    m_sidebar->setStyleSheet(
        "QWidget { background:#0d1016; border-top-left-radius:22px; border-bottom-left-radius:22px; }"
        "QPushButton { border:none; border-radius:16px; background:#171b23; color:#aab2c0; }"
        "QPushButton:hover { background:#222936; color:#ffffff; }"
        "QPushButton:checked { background:#6c8cff; color:white; }"
    );

    auto* sideLayout = new QVBoxLayout(m_sidebar);
    sideLayout->setContentsMargins(12, 56, 12, 18);
    sideLayout->setSpacing(12);

    auto* brand = new QLabel;
    brand->setFixedSize(68,68);
    brand->setPixmap(QPixmap(":/nexus/hub.png").scaled(68,68,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation));
    brand->setAlignment(Qt::AlignCenter);
    sideLayout->addWidget(brand);

    auto* divider1 = new QFrame;
    divider1->setFrameShape(QFrame::HLine);
    divider1->setStyleSheet("color:#252b35;");
    sideLayout->addWidget(divider1);

    auto makeNav = [this](const QString& resource, const QString& tooltip) {
        auto* b = new QPushButton;
        b->setCheckable(true);
        b->setFixedSize(68,68);
        b->setToolTip(tooltip);
        b->setIcon(QIcon(resource));
        b->setIconSize(QSize(68,68));
        return b;
    };

    m_gameNav[S1X] = makeNav(":/nexus/s1x.png", "S1X");
    m_gameNav[IW6X] = makeNav(":/nexus/iw6x.png", "IW6X");
    m_gameNav[IW4X] = makeNav(":/nexus/iw4x.png", "IW4X");

    for (int i=0;i<3;++i) {
        sideLayout->addWidget(m_gameNav[i]);
        connect(m_gameNav[i], &QPushButton::clicked, this, [this, i]{
            selectGame(static_cast<Game>(i));
        });
    }

    auto* divider2 = new QFrame;
    divider2->setFrameShape(QFrame::HLine);
    divider2->setStyleSheet("color:#252b35;");
    sideLayout->addWidget(divider2);

    m_settingsNav = makeNav(":/nexus/settings.png", "Settings");
    sideLayout->addWidget(m_settingsNav);
    connect(m_settingsNav, &QPushButton::clicked, this, [this] { setPage(1); });

    sideLayout->addStretch();
    rootLayout->addWidget(m_sidebar);

    m_content = new QWidget;
    m_content->setStyleSheet("background:transparent;");
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(20, 48, 20, 20);
    contentLayout->setSpacing(0);

    auto* header = new QWidget;
    header->setFixedHeight(66);
    header->setStyleSheet("background:transparent;");
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16,0,0,0);
    headerLayout->setSpacing(2);

    auto* title = new QLabel(kAppName);
    title->setStyleSheet("font-size:24px;font-weight:600;");
    auto* subtitle = new QLabel("S1X • IW6X • IW4X");
    subtitle->setStyleSheet("font-size:12px;color:" + muted + ";");
    headerLayout->addWidget(title);
    headerLayout->addWidget(subtitle);
    contentLayout->addWidget(header);

    m_stack = new QStackedWidget;
    m_stack->setStyleSheet("QStackedWidget{background:" + panel + ";border:1px solid " + border + ";border-radius:18px;}");
    contentLayout->addWidget(m_stack, 1);

    buildHome();
    buildSettings();

    rootLayout->addWidget(m_content, 1);

    // Keep a clean top-right close/minimize affordance using window buttons.
    auto* topBar = new QWidget(m_root);
    topBar->setGeometry(width()-160, 12, 135, 32);
    topBar->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0,0,0,0);
    topLayout->setSpacing(6);

    auto* minBtn = new QPushButton("—");
    auto* closeBtn = new QPushButton("×");
    for (auto* b : {minBtn, closeBtn}) {
        b->setFixedSize(42,28);
        b->setStyleSheet("QPushButton{background:" + panel2 + ";border:1px solid " + border + ";border-radius:10px;color:" + text + ";font-size:15px;}QPushButton:hover{background:#262d38;}");
    }
    topLayout->addWidget(minBtn);
    topLayout->addWidget(closeBtn);
    connect(minBtn, &QPushButton::clicked, this, &QMainWindow::showMinimized);
    connect(closeBtn, &QPushButton::clicked, this, &QMainWindow::close);

    setMouseTracking(true);
}

void MainWindow::buildHome()
{
    m_homePage = new QWidget;
    auto* lay = new QVBoxLayout(m_homePage);
    lay->setContentsMargins(18,18,18,18);
    lay->setSpacing(12);

    m_homeHeader = new QLabel("Multiplayer");
    m_homeHeader->setStyleSheet("font-size:22px;font-weight:600;");
    m_homeSubheader = new QLabel("Ready to play");
    m_homeSubheader->setStyleSheet("font-size:12px;color:" + muted + ";");

    auto* header = new QWidget;
    auto* hl = new QVBoxLayout(header);
    hl->setContentsMargins(2,0,2,0);
    hl->setSpacing(2);
    hl->addWidget(m_homeHeader);
    hl->addWidget(m_homeSubheader);
    lay->addWidget(header);

    auto* hero = new QWidget;
    hero->setMinimumHeight(450);
    hero->setStyleSheet("background:#0c0f14;border:1px solid " + border + ";border-radius:16px;");

    auto* heroLay = new QVBoxLayout(hero);
    heroLay->setContentsMargins(0,0,0,0);

    m_background = new QLabel;
    m_background->setAlignment(Qt::AlignCenter);
    m_background->setScaledContents(true);
    heroLay->addWidget(m_background,1);

    auto* bottom = new QWidget;
    bottom->setFixedHeight(82);
    bottom->setStyleSheet("background:rgba(10,12,16,230);");
    auto* bl = new QHBoxLayout(bottom);
    bl->setContentsMargins(18,10,18,10);

    auto* left = new QVBoxLayout;
    auto* gameName = new QLabel;
    gameName->setObjectName("homeGameName");
    gameName->setStyleSheet("font-size:18px;font-weight:600;");
    auto* tip = new QLabel("Launch through Nexus Launcher");
    tip->setStyleSheet("font-size:12px;color:" + muted + ";");
    left->addWidget(gameName);
    left->addWidget(tip);
    bl->addLayout(left,1);

    m_playButton = new QPushButton("PLAY");
    m_playButton->setFixedSize(150,48);
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setStyleSheet(
        "QPushButton{background:#6c8cff;color:white;border:none;border-radius:12px;font-weight:700;letter-spacing:1px;}"
        "QPushButton:hover{background:#7a98ff;}"
        "QPushButton:pressed{background:#5a78e8;}"
    );
    bl->addWidget(m_playButton);
    heroLay->addWidget(bottom);

    lay->addWidget(hero,1);

    m_stack->addWidget(m_homePage);

    connect(m_playButton, &QPushButton::clicked, this, [this] { play(m_selected); });
}

void MainWindow::buildSettings()
{
    m_settingsPage = new QWidget;
    auto* lay = new QVBoxLayout(m_settingsPage);
    lay->setContentsMargins(20,20,20,20);
    lay->setSpacing(12);

    auto* title = new QLabel("Settings");
    title->setStyleSheet("font-size:22px;font-weight:600;");
    lay->addWidget(title);

    auto* note = new QLabel("Choose where each client is installed. Updates are downloaded directly from GitHub.");
    note->setStyleSheet("font-size:12px;color:" + muted + ";");
    note->setWordWrap(true);
    lay->addWidget(note);

    const QString labels[3] = {"S1X", "IW6X", "IW4X"};
    for (int i=0;i<3;++i) {
        auto* row = new QWidget;
        row->setStyleSheet("background:" + panel2 + ";border:1px solid " + border + ";border-radius:12px;");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(14,10,14,10);
        rl->setSpacing(10);

        auto* name = new QLabel(labels[i]);
        name->setFixedWidth(70);
        name->setStyleSheet("font-size:14px;font-weight:600;");

        m_paths[i] = new QLineEdit;
        m_paths[i]->setPlaceholderText("Installation folder");
        m_paths[i]->setText(installPath(i));
        m_paths[i]->setStyleSheet(
            "QLineEdit{background:#0d1016;border:1px solid #2a303b;border-radius:9px;padding:8px;color:" + text + ";}"
            "QLineEdit:focus{border:1px solid #6c8cff;}"
        );
        connect(m_paths[i], &QLineEdit::editingFinished, this, [this,i]{ savePath(i, m_paths[i]->text()); });

        m_browse[i] = new QPushButton("Browse");
        m_update[i] = new QPushButton("Check Updates");
        for (auto* b : {m_browse[i], m_update[i]}) {
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(36);
            b->setStyleSheet(
                "QPushButton{background:#222832;color:" + text + ";border:1px solid #303743;border-radius:9px;padding:0 14px;}"
                "QPushButton:hover{background:#2a313d;}"
                "QPushButton:disabled{color:#697383;background:#171b22;}"
            );
        }

        rl->addWidget(name);
        rl->addWidget(m_paths[i], 1);
        rl->addWidget(m_browse[i]);
        rl->addWidget(m_update[i]);

        lay->addWidget(row);

        connect(m_browse[i], &QPushButton::clicked, this, [this,i]{ browse(static_cast<Game>(i)); });
        connect(m_update[i], &QPushButton::clicked, this, [this,i]{ update(static_cast<Game>(i)); });
    }

    auto* channel = new QLabel("Update Channel  •  GitHub");
    channel->setStyleSheet("margin-top:4px;font-size:13px;color:" + muted + ";");
    lay->addWidget(channel);

    m_updateStatus = new QLabel("Ready");
    m_updateStatus->setStyleSheet("font-size:12px;color:" + muted + ";");
    lay->addWidget(m_updateStatus);

    m_progress = new QProgressBar;
    m_progress->setRange(0,100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    m_progress->setStyleSheet(
        "QProgressBar{background:#202630;border:none;border-radius:4px;}"
        "QProgressBar::chunk{background:#6c8cff;border-radius:4px;}"
    );
    lay->addWidget(m_progress);

    lay->addStretch();

    auto* wine = new QLabel(
        "Linux note: S1X/IW6X/IW4X are Windows clients. Nexus Launcher launches them through Wine. "
        "Set NEXUS_WINE_COMMAND or install the `wine` package to use the default."
    );
    wine->setWordWrap(true);
    wine->setStyleSheet("font-size:11px;color:" + muted + ";padding:8px;");
    lay->addWidget(wine);

    m_stack->addWidget(m_settingsPage);

    connect(&m_updater, &Updater::progress, this, [this](int percent, const QString& status) {
        m_progress->setValue(qBound(0, percent, 100));
        m_updateStatus->setText(status);
    });

    connect(&m_updater, &Updater::finished, this, [this](const NexusUpdateResult& result) {
        for (int i=0;i<3;++i) m_update[i]->setEnabled(true);
        m_updateStatus->setText(result.message);
        m_progress->setValue(result.success ? 100 : 0);
        if (result.success) {
            QSettings s(kAppOrg, kAppName);
            const int g = m_selected;
            if (m_updateGame == m_selected) {
                s.setValue(keyForVersion1(g), result.version1);
                s.setValue(keyForVersion2(g), result.version2);
            }
        }
        if (!result.success)
            QMessageBox::warning(this, "Nexus Launcher", result.message);
    });
}

QString MainWindow::installPath(int g) const
{
    QSettings s(kAppOrg, kAppName);
    return s.value(keyForPath(g)).toString();
}

void MainWindow::savePath(int g, const QString& value)
{
    QSettings s(kAppOrg, kAppName);
    s.setValue(keyForPath(g), QDir::cleanPath(value.trimmed()));
}

void MainWindow::selectGame(Game g)
{
    m_selected = g;
    for (int i=0;i<3;++i) m_gameNav[i]->setChecked(i == static_cast<int>(g));
    m_settingsNav->setChecked(false);
    setPage(0);
}

void MainWindow::setPage(int page)
{
    m_settings = (page == 1);
    m_stack->setCurrentIndex(page);
    for (int i=0;i<3;++i)
        m_gameNav[i]->setChecked(!m_settings && i == static_cast<int>(m_selected));
    m_settingsNav->setChecked(m_settings);
}

void MainWindow::browse(Game g)
{
    const int idx = static_cast<int>(g);
    const QString current = m_paths[idx]->text().trimmed();
    const QString selected = QFileDialog::getExistingDirectory(this, "Select installation folder", current);
    if (!selected.isEmpty()) {
        m_paths[idx]->setText(selected);
        savePath(idx, selected);
    }
}

void MainWindow::update(Game g)
{
    const int idx = static_cast<int>(g);
    savePath(idx, m_paths[idx]->text());

    for (int i=0;i<3;++i) m_update[i]->setEnabled(false);

    QSettings s(kAppOrg, kAppName);
    m_updateGame = g;
    const QString v1 = s.value(keyForVersion1(idx)).toString();
    const QString v2 = s.value(keyForVersion2(idx)).toString();

    m_progress->setValue(1);
    m_updateStatus->setText("Starting update...");
    m_updater.checkAndInstall(idx, m_paths[idx]->text(), v1, v2);
}

QString MainWindow::gameTitle(Game g) const
{
    switch(g) {
    case S1X: return "S1X";
    case IW6X: return "IW6X";
    case IW4X: return "IW4X";
    }
    return "Game";
}

QString MainWindow::gameExe(Game g) const
{
    switch(g) {
    case S1X: return "s1x.exe";
    case IW6X: return "iw6x.exe";
    case IW4X: return "iw4x.exe";
    }
    return {};
}

QString MainWindow::wineCommand() const
{
    return commandFromEnv();
}

void MainWindow::play(Game g)
{
    const QString dir = installPath(static_cast<int>(g));
    if (dir.isEmpty()) {
        setPage(1);
        QMessageBox::information(this, "Nexus Launcher", "Set the installation folder for " + gameTitle(g) + " first.");
        return;
    }

    const QString exePath = QDir(dir).filePath(gameExe(g));
    if (!QFileInfo::exists(exePath)) {
        QMessageBox::warning(this, "Nexus Launcher",
                             "Could not find " + gameExe(g) + " in:\n" + dir +
                             "\n\nUse Check Updates first or verify the install folder.");
        return;
    }

    QString program = wineCommand();
    if (program.contains('/'))
        program = QFileInfo(program).absoluteFilePath();

    if (!QProcess::startDetached(program, {exePath}, dir)) {
        QMessageBox::warning(this, "Nexus Launcher",
                             "Could not launch the client through Wine.\n\n"
                             "Current Wine command: " + program);
        return;
    }

    m_discord.setGamePresence(gameTitle(g) + " Multiplayer", QString());
}

QString MainWindow::runningGameServer(Game g, QString* gameExeOut) const
{
    const QString exe = gameExe(g);
    if (gameExeOut) *gameExeOut = exe;

    QProcess ps;
    ps.start("bash", {"-lc", "pgrep -a -f -- '" + exe + "'"});
    if (!ps.waitForFinished(500))
        return {};

    const QString output = QString::fromLocal8Bit(ps.readAllStandardOutput());
    if (output.trimmed().isEmpty())
        return {};

    // Best-effort server name:
    // 1. xdotool window title if available.
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    QString pid;
    if (!lines.isEmpty()) {
        const QRegularExpression re(R"(^\s*(\d+)\s)");
        const auto match = re.match(lines.first());
        if (match.hasMatch()) pid = match.captured(1);
    }

    if (!pid.isEmpty()) {
        QProcess xdotool;
        xdotool.start("bash", {"-lc",
            "command -v xdotool >/dev/null 2>&1 && "
            "for w in $(xdotool search --pid " + pid + " 2>/dev/null); do "
            "xdotool getwindowname $w 2>/dev/null; done | head -n 1"});
        if (xdotool.waitForFinished(700)) {
            const QString title = QString::fromLocal8Bit(xdotool.readAllStandardOutput()).trimmed();
            if (!title.isEmpty() && !title.contains("Nexus", Qt::CaseInsensitive))
                return title;
        }
    }

    // 2. Search common client logs for a hostname value.
    const QString install = installPath(static_cast<int>(g));
    const QStringList names = {
        "console.log","server.log","iw4x.log","iw6x.log","s1x.log","client.log"
    };
    for (const QString& name : names) {
        QFile f(QDir(install).filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        if (f.size() > 512 * 1024)
            f.seek(f.size() - 512 * 1024);
        const QString data = QString::fromUtf8(f.readAll());
        const QRegularExpression re(R"((?:sv_hostname|server hostname|server name)\s*["=:]\s*"?([^"\r\n]+))",
                                     QRegularExpression::CaseInsensitiveOption);
        auto it = re.globalMatch(data);
        QString found;
        while (it.hasNext()) found = it.next().captured(1).trimmed();
        if (!found.isEmpty())
            return found;
    }
    return {};
}

void MainWindow::refreshDiscord()
{
    for (int i=0;i<3;++i) {
        QString exe;
        const QString server = runningGameServer(static_cast<Game>(i), &exe);
        if (!server.isNull() && !server.isEmpty()) {
            m_currentGame = i;
            m_currentServer = server;
            m_discord.setGamePresence(gameTitle(static_cast<Game>(i)) + " Multiplayer", server);
            return;
        }
    }

    // Also detect a running game even when no server name was obtainable.
    for (int i=0;i<3;++i) {
        QString exe;
        const QString s = runningGameServer(static_cast<Game>(i), &exe);
        if (!s.isEmpty()) continue;
        QProcess probe;
        probe.start("bash", {"-lc", "pgrep -x '" + exe + "' >/dev/null 2>&1 || pgrep -f '" + exe + "' >/dev/null 2>&1"});
        if (probe.waitForFinished(500) && probe.exitCode() == 0) {
            m_currentGame = i;
            m_currentServer.clear();
            m_discord.setGamePresence(gameTitle(static_cast<Game>(i)) + " Multiplayer", {});
            return;
        }
    }

    m_currentGame = -1;
    m_currentServer.clear();
    m_discord.startLauncherPresence();
}

void MainWindow::pollGames()
{
    // Home presentation and Discord presence both follow the selected game.
    const int idx = static_cast<int>(m_selected);
    const QString names[3] = {"S1X", "IW6X", "IW4X"};
    const QString bg[3] = {
        ":/nexus/background_s1x.jpg",
        ":/nexus/background_iw6x.jpg",
        ":/nexus/background_iw4x.jpg"
    };

    if (m_homeHeader) m_homeHeader->setText("Multiplayer — " + names[idx]);
    if (m_homeSubheader) m_homeSubheader->setText("Ready to play with Nexus Launcher");

    if (m_background) {
        QPixmap px(bg[idx]);
        if (!px.isNull())
            m_background->setPixmap(px.scaled(m_background->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }

    if (m_homePage && m_stack->currentWidget() == m_homePage) {
        auto* gameName = m_homePage->findChild<QLabel*>("homeGameName");
        if (gameName) gameName->setText(names[idx]);
    }

    refreshDiscord();
}
