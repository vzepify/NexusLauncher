#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QString>

class DiscordPresence : public QObject
{
    Q_OBJECT
public:
    explicit DiscordPresence(QObject* parent = nullptr);
    void setClientId(const QString& id);
    void startLauncherPresence();
    void setGamePresence(const QString& game, const QString& server);
    void clear();

private slots:
    void tryConnect();

private:
    bool connectSocket();
    bool sendFrame(quint32 opcode, const QByteArray& payload);
    bool sendActivity(const QJsonObject& activity);
    static QByteArray json(const QJsonObject& obj);

    QLocalSocket m_socket;
    QTimer m_reconnectTimer;
    QString m_clientId;
    qint64 m_startTime = 0;
    QString m_activityKind;
};
