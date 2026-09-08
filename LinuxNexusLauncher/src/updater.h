#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QString>
#include <functional>

struct NexusUpdateResult
{
    bool success = false;
    bool updated = false;
    QString message;
    QString version1;
    QString version2;
    bool version1Installed = false;
    bool version2Installed = false;
};

class Updater : public QObject
{
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);

    void checkAndInstall(int gameIndex,
                         const QString& installPath,
                         const QString& currentVersion1,
                         const QString& currentVersion2);

signals:
    void progress(int percent, const QString& status);
    void finished(const NexusUpdateResult& result);

private:
    QString tempDir() const;
    void fail(const QString& message);
    void fetchJson(const QUrl& url, std::function<void(const QJsonObject&)> ok);
    QString latestTag(const QJsonDocument& doc) const;
    QString latestReleaseTag(const QJsonDocument& doc) const;
    void checkS1xOrIw6x();
    void checkIw4x();
    void downloadIw4xDll(const QString& tag, bool alsoRaw);
    void downloadIw4xRaw(const QString& rawTag);
    void downloadToFile(const QUrl& url, const QString& filePath,
                        std::function<void(bool, const QString&)> done);
    bool extractZip(const QString& archive, const QString& destination);

    QNetworkAccessManager m_net;
    int m_gameIndex = -1;
    QString m_installPath;
    QString m_current1;
    QString m_current2;
    NexusUpdateResult m_result;
};
