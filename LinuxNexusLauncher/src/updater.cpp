#include "updater.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <functional>

Updater::Updater(QObject* parent) : QObject(parent) {}

QString Updater::tempDir() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                    + "/NexusLauncher";
    QDir().mkpath(d);
    return d;
}

void Updater::checkAndInstall(int gameIndex, const QString& installPath,
                              const QString& currentVersion1, const QString& currentVersion2)
{
    if (installPath.trimmed().isEmpty()) {
        emit finished({false, false, "Set the installation folder first.", {}, {}});
        return;
    }
    if (!QDir(installPath).exists()) {
        emit finished({false, false, "The selected installation folder does not exist.", {}, {}});
        return;
    }
    m_gameIndex = gameIndex;
    m_installPath = QDir::cleanPath(installPath);
    m_current1 = currentVersion1;
    m_current2 = currentVersion2;
    m_result = {};
    emit progress(5, "Checking GitHub for updates...");
    if (m_gameIndex == 0 || m_gameIndex == 1)
        checkS1xOrIw6x();
    else
        checkIw4x();
}

void Updater::fail(const QString& message)
{
    m_result.success = false;
    m_result.message = message;
    emit finished(m_result);
}

void Updater::fetchJson(const QUrl& url, std::function<void(const QJsonObject&)> ok)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "NexusLauncher/1.0");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ok] {
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) {
            fail("Could not reach GitHub: " + reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            fail("GitHub returned an invalid API response.");
            return;
        }
        ok(doc.object());
    });
}

QString Updater::latestTag(const QJsonDocument& doc) const
{
    const QJsonArray arr = doc.array();
    if (arr.isEmpty() || !arr.first().isObject()) return {};
    return arr.first().toObject().value("name").toString();
}

QString Updater::latestReleaseTag(const QJsonDocument& doc) const
{
    return doc.object().value("tag_name").toString();
}

void Updater::checkS1xOrIw6x()
{
    const QString repo = (m_gameIndex == 0) ? "CBServers/s1x-client" : "CBServers/iw6x-client";
    const QString exe = (m_gameIndex == 0) ? "s1x.exe" : "iw6x.exe";

    QNetworkRequest req(QUrl("https://api.github.com/repos/" + repo + "/tags?per_page=1"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "NexusLauncher/1.0");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, exe] {
        const QByteArray data = r->readAll();
        const QString err = r->errorString();
        const auto code = r->error();
        r->deleteLater();

        if (code != QNetworkReply::NoError) {
            fail("Could not reach GitHub: " + err);
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        const QString tag = latestTag(doc);
        if (tag.isEmpty()) {
            fail("No client version tag was found on GitHub.");
            return;
        }

        m_result.version1 = tag;
        if (tag.compare(m_current1, Qt::CaseInsensitive) == 0) {
            m_result = {true, false, exe + " is already up to date (" + tag + ").", tag, {}};
            emit progress(100, m_result.message);
            emit finished(m_result);
            return;
        }

        const QUrl url(m_gameIndex == 0
            ? "https://github.com/CBServers/updater/raw/main/updater/s1x/s1x.exe"
            : "https://github.com/CBServers/updater/raw/main/updater/iw6x/iw6x.exe");

        const QString out = tempDir() + "/" + exe;
        emit progress(20, "Downloading " + exe + "...");
        downloadToFile(url, out, [this, out, exe, tag](bool success, const QString& error) {
            if (!success) { fail("Download failed: " + error); return; }

            emit progress(92, "Installing " + exe + "...");
            const QString dest = m_installPath + "/" + exe;
            QFile::remove(dest);
            if (!QFile::copy(out, dest)) {
                QFile::remove(out);
                fail("The update downloaded but could not be installed. Close the game and try again.");
                return;
            }
            QFile::remove(out);

            m_result = {true, true, exe + " updated to " + tag + ".", tag, {}};
            emit progress(100, m_result.message);
            emit finished(m_result);
        });
    });
}

void Updater::downloadIw4xDll(const QString& tag, bool alsoRaw)
{
    const QString out = tempDir() + "/iw4x.dll";
    emit progress(35, "Downloading iw4x.dll...");
    downloadToFile(QUrl("https://github.com/iw4x/iw4x-client/releases/latest/download/iw4x.dll"),
                   out, [this, out, tag, alsoRaw](bool ok, const QString& err) {
        if (!ok) { fail("The latest IW4x DLL could not be downloaded: " + err); return; }
        emit progress(68, "Installing iw4x.dll...");
        const QString dest = m_installPath + "/iw4x.dll";
        QFile::remove(dest);
        if (!QFile::copy(out, dest)) {
            fail("The IW4x DLL was downloaded but could not be installed. Close IW4x and try again.");
            return;
        }
        QFile::remove(out);
        m_result.version1Installed = true;
        if (alsoRaw)
            downloadIw4xRaw(m_result.version2);
        else {
            m_result = {true, true, "IW4x client DLL updated to " + tag + ".", tag, m_result.version2};
            emit progress(100, m_result.message);
            emit finished(m_result);
        }
    });
}

void Updater::downloadIw4xRaw(const QString& rawTag)
{
    const QString out = tempDir() + "/iw4x-rawfiles-release.zip";
    emit progress(45, "Downloading IW4x rawfiles...");
    downloadToFile(QUrl("https://github.com/iw4x/iw4x-rawfiles/releases/latest/download/release.zip"),
                   out, [this, out, rawTag](bool ok, const QString& err) {
        if (!ok) { fail("The latest IW4x rawfiles archive could not be downloaded: " + err); return; }
        emit progress(78, "Extracting IW4x rawfiles...");
        if (!extractZip(out, m_installPath)) {
            QFile::remove(out);
            fail("The IW4x rawfiles archive downloaded successfully, but extraction failed.");
            return;
        }
        QFile::remove(out);
        m_result.version2Installed = true;
        m_result = {true, true, "IW4x rawfiles updated to " + rawTag + ".", m_result.version1, rawTag};
        emit progress(100, m_result.message);
        emit finished(m_result);
    });
}

void Updater::downloadToFile(const QUrl& url, const QString& filePath,
                             std::function<void(bool, const QString&)> done)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "NexusLauncher/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* r = m_net.get(req);
    QFile* file = new QFile(filePath, r);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        r->abort();
        done(false, "Could not create the temporary download file.");
        delete file;
        return;
    }

    connect(r, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        int p = total > 0 ? int((received * 100) / total) : 0;
        emit progress(p, "Downloading... " + QString::number(p) + "%");
    });
    connect(r, &QNetworkReply::readyRead, this, [r, file] {
        file->write(r->readAll());
    });
    connect(r, &QNetworkReply::finished, this, [r, file, done] {
        file->write(r->readAll());
        file->flush();
        const bool ok = r->error() == QNetworkReply::NoError;
        const QString error = r->errorString();
        file->close();
        delete file;
        r->deleteLater();
        done(ok, error);
    });
}

bool Updater::extractZip(const QString& archive, const QString& destination)
{
    QProcess p;
    p.start("unzip", {"-o", archive, "-d", destination});
    if (!p.waitForStarted(3000))
        return false;
    p.waitForFinished(-1);
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
