#include "platform/youtube/YouTubeStreamListClient.h"
#include "platform/youtube/YouTubeChatMessageParser.h"

#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
#include "stream_list.grpc.pb.h"
#include "stream_list.pb.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace {
#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
QString streamErrorCodeName(::grpc::StatusCode code)
{
    switch (code) {
    case ::grpc::StatusCode::OK:
        return QStringLiteral("OK");
    case ::grpc::StatusCode::CANCELLED:
        return QStringLiteral("CANCELLED");
    case ::grpc::StatusCode::INVALID_ARGUMENT:
        return QStringLiteral("INVALID_ARGUMENT");
    case ::grpc::StatusCode::DEADLINE_EXCEEDED:
        return QStringLiteral("DEADLINE_EXCEEDED");
    case ::grpc::StatusCode::NOT_FOUND:
        return QStringLiteral("NOT_FOUND");
    case ::grpc::StatusCode::PERMISSION_DENIED:
        return QStringLiteral("PERMISSION_DENIED");
    case ::grpc::StatusCode::RESOURCE_EXHAUSTED:
        return QStringLiteral("RESOURCE_EXHAUSTED");
    case ::grpc::StatusCode::FAILED_PRECONDITION:
        return QStringLiteral("FAILED_PRECONDITION");
    case ::grpc::StatusCode::ABORTED:
        return QStringLiteral("ABORTED");
    case ::grpc::StatusCode::OUT_OF_RANGE:
        return QStringLiteral("OUT_OF_RANGE");
    case ::grpc::StatusCode::UNIMPLEMENTED:
        return QStringLiteral("UNIMPLEMENTED");
    case ::grpc::StatusCode::INTERNAL:
        return QStringLiteral("INTERNAL");
    case ::grpc::StatusCode::UNAVAILABLE:
        return QStringLiteral("UNAVAILABLE");
    case ::grpc::StatusCode::DATA_LOSS:
        return QStringLiteral("DATA_LOSS");
    case ::grpc::StatusCode::UNAUTHENTICATED:
        return QStringLiteral("UNAUTHENTICATED");
    default:
        return QStringLiteral("UNKNOWN");
    }
}
#endif

QString streamListSupportMessage()
{
#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
    return QStringLiteral("gRPC streamList transport available");
#else
    return QStringLiteral("gRPC streamList transport not built in this binary");
#endif
}
} // namespace

struct YouTubeStreamListClient::Private {
    std::mutex mutex;
#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
    std::shared_ptr<::grpc::ClientContext> context;
#endif
    std::thread worker;
    std::atomic<bool> stopRequested { false };
    std::atomic<quint64> generation { 0 };
    std::atomic<bool> running { false };
};

YouTubeStreamListClient::YouTubeStreamListClient(QObject* parent)
    : QObject(parent)
    , m_d(std::make_unique<Private>())
{
}

YouTubeStreamListClient::~YouTubeStreamListClient()
{
    stop();
}

bool YouTubeStreamListClient::isSupported() const
{
#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
    return true;
#else
    return false;
#endif
}

bool YouTubeStreamListClient::isRunning() const
{
    return m_d->running.load();
}

QString YouTubeStreamListClient::supportDetail() const
{
    return streamListSupportMessage();
}

void YouTubeStreamListClient::start(const QString& accessToken,
                                    const QString& liveChatId,
                                    const QString& pageToken)
{
    stop();

    if (accessToken.trimmed().isEmpty()) {
        emit streamFailed(QStringLiteral("YT_STREAM_TOKEN_MISSING"),
            QStringLiteral("streamList access token is empty."));
        return;
    }
    if (liveChatId.trimmed().isEmpty()) {
        emit streamFailed(QStringLiteral("YT_STREAM_LIVECHAT_ID_MISSING"),
            QStringLiteral("streamList liveChatId is empty."));
        return;
    }

#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
    const quint64 generation = m_d->generation.fetch_add(1) + 1;
    m_d->stopRequested.store(false);
    m_d->running.store(true);
    emit started();

    m_d->worker = std::thread([this, generation, accessToken, liveChatId, pageToken]() {
        const auto finishStopped = [this, generation]() {
            if (m_d->generation.load() != generation) {
                return;
            }
            m_d->running.store(false);
            emit stopped();
        };

        ::grpc::SslCredentialsOptions sslOptions;
        auto channel = ::grpc::CreateChannel(QStringLiteral("dns:///youtube.googleapis.com:443").toStdString(),
            ::grpc::SslCredentials(sslOptions));
        auto stub = ::youtube::api::v3::V3DataLiveChatMessageService::NewStub(channel);
        if (!stub) {
            if (m_d->generation.load() == generation) {
                m_d->running.store(false);
                emit streamFailed(QStringLiteral("YT_STREAM_STUB_CREATE_FAILED"),
                    QStringLiteral("Failed to create streamList gRPC stub."));
                emit stopped();
            }
            return;
        }

        auto context = std::make_shared<::grpc::ClientContext>();
        context->AddMetadata("authorization", QStringLiteral("Bearer %1").arg(accessToken.trimmed()).toStdString());
        {
            std::lock_guard<std::mutex> lock(m_d->mutex);
            m_d->context = context;
        }

        ::youtube::api::v3::LiveChatMessageListRequest request;
        request.set_live_chat_id(liveChatId.trimmed().toStdString());
        request.set_max_results(20);
        request.add_part("id");
        request.add_part("snippet");
        request.add_part("authorDetails");
        if (!pageToken.trimmed().isEmpty()) {
            request.set_page_token(pageToken.trimmed().toStdString());
        }

        QString offlineReason;
        auto reader = stub->StreamList(context.get(), request);
        ::youtube::api::v3::LiveChatMessageListResponse response;
        while (!m_d->stopRequested.load() && reader && reader->Read(&response)) {
            if (m_d->generation.load() != generation) {
                break;
            }

            emit responseObserved(response.items_size(),
                !response.next_page_token().empty(),
                !response.offline_at().empty());

            QVector<UnifiedChatMessage> messages;
            messages.reserve(response.items_size());
            for (const auto& item : response.items()) {
                const UnifiedChatMessage msg = parseYouTubeChatMessageProto(item);
                if (!msg.messageId.trimmed().isEmpty()) {
                    messages.push_back(msg);
                }
            }
            if (!messages.isEmpty()) {
                emit messagesReceived(messages);
            }
            if (!response.next_page_token().empty()) {
                emit streamCheckpoint(QString::fromStdString(response.next_page_token()));
            }
            if (!response.offline_at().empty()) {
                offlineReason = QStringLiteral("offlineAt=%1").arg(QString::fromStdString(response.offline_at()));
            }
        }

        ::grpc::Status status = reader ? reader->Finish() : ::grpc::Status(::grpc::StatusCode::UNKNOWN, "reader unavailable");
        {
            std::lock_guard<std::mutex> lock(m_d->mutex);
            m_d->context.reset();
        }

        if (m_d->generation.load() != generation) {
            m_d->running.store(false);
            emit stopped();
            return;
        }
        if (m_d->stopRequested.load() || status.error_code() == ::grpc::StatusCode::CANCELLED) {
            finishStopped();
            return;
        }

        m_d->running.store(false);
        if (status.ok()) {
            emit streamEnded(offlineReason.isEmpty() ? QStringLiteral("stream completed") : offlineReason);
            emit stopped();
            return;
        }

        const QString code = QStringLiteral("YT_STREAM_%1").arg(streamErrorCodeName(status.error_code()));
        const QString detail = QStringLiteral("%1 (%2)")
                                   .arg(QString::fromStdString(status.error_message()),
                                       streamErrorCodeName(status.error_code()));
        emit streamFailed(code, detail.trimmed());
        emit stopped();
    });
#else
    Q_UNUSED(pageToken)
    emit streamFailed(QStringLiteral("YT_STREAM_UNAVAILABLE"),
        streamListSupportMessage());
#endif
}

void YouTubeStreamListClient::stop()
{
    m_d->stopRequested.store(true);

#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
    {
        std::lock_guard<std::mutex> lock(m_d->mutex);
        if (m_d->context) {
            m_d->context->TryCancel();
        }
    }
#endif

    if (m_d->worker.joinable()) {
        m_d->worker.join();
    }
    if (m_d->running.exchange(false)) {
        emit stopped();
    }
}
