#include "discordpresence.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDateTime>
#include <QCoreApplication>

namespace {
constexpr quint32 OPCODE_HANDSHAKE = 0;
constexpr quint32 OPCODE_FRAME = 1;

QByteArray utf8(const QString& s) { return s.toUtf8(); }
}

DiscordPresence::DiscordPresence(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        if (!m_reconnectTimer.isActive())
            m_reconnectTimer.start();
    });
    m_reconnectTimer.setInterval(3000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &DiscordPresence::tryConnect);
}

void DiscordPresence::setClientId(const QString& id)
{
    m_clientId = id.trimmed();
}

void DiscordPresence::startLauncherPresence()
{
    if (m_activityKind != "launcher") {
        m_startTime = QDateTime::currentSecsSinceEpoch();
        m_activityKind = "launcher";
    } else if (m_startTime == 0) {
        m_startTime = QDateTime::currentSecsSinceEpoch();
    }
    if (connectSocket()) {
        QJsonObject activity{
            {"details", "Nexus Launcher"},
            {"state", "Launcher Open"},
            {"timestamps", QJsonObject{{"start", m_startTime}}},
            {"assets", QJsonObject{{"large_image", "nexus_launcher"},
                                   {"large_text", "Nexus Launcher"}}}
        };
        sendActivity(activity);
    }
}

void DiscordPresence::setGamePresence(const QString& game, const QString& server)
{
    const QString kind = "game:" + game;
    if (m_activityKind != kind) {
        m_startTime = QDateTime::currentSecsSinceEpoch();
        m_activityKind = kind;
    } else if (m_startTime == 0) {
        m_startTime = QDateTime::currentSecsSinceEpoch();
    }
    if (!connectSocket())
        return;

    QJsonObject activity{
        {"details", game + " with Nexus Launcher"},
        {"timestamps", QJsonObject{{"start", m_startTime}}},
        {"assets", QJsonObject{{"large_image", "nexus_launcher"},
                               {"large_text", "Nexus Launcher"}}}
    };
    if (!server.trimmed().isEmpty())
        activity.insert("state", server.trimmed());
    sendActivity(activity);
}

void DiscordPresence::clear()
{
    if (!connectSocket())
        return;
    sendActivity(QJsonObject{});
}

bool DiscordPresence::connectSocket()
{
    if (m_clientId.isEmpty())
        return false;
    if (m_socket.state() == QLocalSocket::ConnectedState)
        return true;

    const QStringList names{
        "discord-ipc-0","discord-ipc-1","discord-ipc-2","discord-ipc-3",
        "discord-ipc-4","discord-ipc-5","discord-ipc-6","discord-ipc-7",
        "discord-ipc-8","discord-ipc-9"
    };
    for (const QString& name : names) {
        m_socket.abort();
        m_socket.connectToServer(name);
        if (m_socket.waitForConnected(250))
        {
            QJsonObject handshake{{"v", 1}, {"client_id", m_clientId}};
            if (sendFrame(OPCODE_HANDSHAKE, json(handshake))) {
                m_reconnectTimer.stop();
                return true;
            }
            m_socket.abort();
        }
    }
    return false;
}

void DiscordPresence::tryConnect()
{
    if (connectSocket())
        startLauncherPresence();
}

bool DiscordPresence::sendFrame(quint32 opcode, const QByteArray& payload)
{
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return false;

    QByteArray frame;
    frame.resize(8);
    frame[0] = char(opcode & 0xff);
    frame[1] = char((opcode >> 8) & 0xff);
    frame[2] = char((opcode >> 16) & 0xff);
    frame[3] = char((opcode >> 24) & 0xff);
    const quint32 len = quint32(payload.size());
    frame[4] = char(len & 0xff);
    frame[5] = char((len >> 8) & 0xff);
    frame[6] = char((len >> 16) & 0xff);
    frame[7] = char((len >> 24) & 0xff);
    frame += payload;
    return m_socket.write(frame) == frame.size() && m_socket.waitForBytesWritten(1000);
}

bool DiscordPresence::sendActivity(const QJsonObject& activity)
{
    QJsonObject args{
        {"pid", int(QCoreApplication::applicationPid())},
        {"activity", activity}
    };
    if (activity.isEmpty())
        args.insert("activity", QJsonValue::Null);
    QJsonObject cmd{
        {"cmd", "SET_ACTIVITY"},
        {"args", args},
        {"nonce", QString::number(QDateTime::currentMSecsSinceEpoch())}
    };
    return sendFrame(OPCODE_FRAME, json(cmd));
}

QByteArray DiscordPresence::json(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
