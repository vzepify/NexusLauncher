#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QButtonGroup>
#include <QPixmap>
#include <QMap>
#include <QPoint>
#include "updater.h"
#include "discordpresence.h"

class GameButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    enum Game { S1X = 0, IW6X = 1, IW4X = 2 };

    void buildUi();
    void buildHome();
    void buildSettings();
    void selectGame(Game g);
    void setPage(int page);
    void browse(Game g);
    void update(Game g);
    void play(Game g);
    void refreshDiscord();
    void pollGames();
    QString installPath(int g) const;
    void savePath(int g, const QString& value);
    QString runningGameServer(Game g, QString* gameExe = nullptr) const;
    QString gameTitle(Game g) const;
    QString gameExe(Game g) const;
    QString wineCommand() const;

    QWidget* m_root = nullptr;
    QWidget* m_sidebar = nullptr;
    QWidget* m_content = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_homePage = nullptr;
    QWidget* m_settingsPage = nullptr;

    QLabel* m_homeHeader = nullptr;
    QLabel* m_homeSubheader = nullptr;
    QLabel* m_background = nullptr;
    QPushButton* m_playButton = nullptr;

    QLineEdit* m_paths[3]{};
    QPushButton* m_browse[3]{};
    QPushButton* m_update[3]{};

    QProgressBar* m_progress = nullptr;
    QLabel* m_updateStatus = nullptr;

    QPushButton* m_gameNav[3]{};
    QPushButton* m_settingsNav = nullptr;
    Game m_selected = IW4X;
    Game m_updateGame = IW4X;
    bool m_settings = false;

    Updater m_updater;
    DiscordPresence m_discord;
    QTimer m_pollTimer;
    qint64 m_launcherStart = 0;
    bool m_dragging = false;
    QPoint m_dragOffset;
    QString m_currentServer;
    int m_currentGame = -1;
};
