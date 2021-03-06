#include "pomfuploader.h"
#include "../uploader/uploader.h"

#include <QtNetwork>
#include <QJsonDocument>
#include <QJsonObject>

PomfUploader::PomfUploader(QObject *parent) : ImageUploader(parent)
{
    mUploaderType = "pomf";
    loadSettings();
}

void PomfUploader::verify(const QString &url, VerificationCallback callback)
{
    QNetworkRequest request(QUrl::fromUserInput(QString("%1/upload.php").arg(url)));

    if (!request.url().isValid()) {
        callback(false);
    }

    QNetworkReply *reply = Uploader::instance()->nam()->get(request);

    connect(reply, &QNetworkReply::finished, [reply, callback] {
        reply->deleteLater();

        const QJsonObject pomfResponse = QJsonDocument::fromJson(reply->readAll()).object();

        if (!pomfResponse.isEmpty() && pomfResponse.contains("success")) {
            callback(true);
        } else {
            callback(false);
        }
    });
}

void PomfUploader::upload(const QString &fileName)
{
    QString pomfUrl = mSettings["pomf_url"].toString();

    if (pomfUrl.isEmpty()) {
        emit error(ImageUploader::HostError, tr("Invalid pomf uploader URL!"), fileName);
        return;
    }

    QUrl url = QUrl::fromUserInput(pomfUrl + "/upload.php");

    QFile *file = new QFile(fileName);

    if (!file->open(QIODevice::ReadOnly)) {
        emit error(ImageUploader::FileError, tr("Unable to read screenshot file"), fileName);
        file->deleteLater();
        return;
    }

    QNetworkRequest request(url);

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(fileName, QMimeDatabase::MatchExtension).name());
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"files[]\"; filename=\"%1\"").arg(QFileInfo(fileName).fileName()));
    imagePart.setBodyDevice(file);

    file->setParent(multiPart);
    multiPart->append(imagePart);

    QNetworkReply *reply = Uploader::instance()->nam()->post(request, multiPart);
    this->setProperty("fileName", fileName);
    multiPart->setParent(reply);

    connect(this , &PomfUploader::cancelRequest, reply, &QNetworkReply::abort);
    connect(this , &PomfUploader::cancelRequest, reply, &QNetworkReply::deleteLater);

    connect(reply, &QNetworkReply::uploadProgress, this, [&](qint64 bytesSent, qint64 bytesTotal) {
        float b = (float) bytesSent / bytesTotal;
        int p = qRound(b * 100);
        setProgress(p);
    });

    connect(reply, &QNetworkReply::finished, this, [&, reply, fileName] {
        const QJsonObject pomfResponse = QJsonDocument::fromJson(reply->readAll()).object();

        if (reply->error() != QNetworkReply::NoError && pomfResponse.isEmpty()) {
            emit error(ImageUploader::NetworkError, tr("Error reaching uploader"), fileName);
            return;
        }

        if (!pomfResponse.contains("success") || !pomfResponse.contains("files")) {
            emit error(ImageUploader::HostError, tr("Invalid response from uploader"), fileName);
            return;
        }

        if (pomfResponse["success"].toBool()) {
            emit uploaded(fileName, pomfResponse["files"].toArray().at(0).toObject()["url"].toString(), "");
        } else {
            QString description;

            if (pomfResponse.contains("description")) {
                description = pomfResponse["description"].toString();
            }

            if (description.isEmpty()) {
                description = tr("Host error");
            }

            emit error(ImageUploader::HostError, description, fileName);
        }
    });
}

void PomfUploader::retry()
{
    upload(property("fileName").toString());
}

void PomfUploader::cancel()
{
    emit cancelRequest();
}

