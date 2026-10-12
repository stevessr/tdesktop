#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtWebSockets/QWebSocket>

#include <functional>

namespace Crossgram {

class WssTransport final : public QObject {
public:
	explicit WssTransport(QObject *parent = nullptr);
	~WssTransport() override;

	void open(const QUrl &url);
	void stop();
	void expire();
	void send(const char *header, qsizetype headerSize, const char *body, qsizetype bodySize);
	[[nodiscard]] qint64 receive(char *destination, qint64 capacity);
	[[nodiscard]] bool readable() const;
	[[nodiscard]] bool connected() const;
	[[nodiscard]] int state() const;

	std::function<void()> onOpen;
	std::function<void()> onClose;
	std::function<void()> onData;
	std::function<void(const char *)> onFailure;

private:
	void terminate(const char *category);
	void flush();

	static constexpr qint64 kQueueLimit = 8 * 1024 * 1024;
	static constexpr qint64 kChunkLimit = 128 * 1024;

	QWebSocket _websocket;
	QTimer _connectDeadline;
	QTimer _writeDeadline;
	QByteArray _incoming;
	QByteArray _outgoing;
	qsizetype _consumed = 0;
	bool _started = false;
	bool _stopped = false;
};

} // namespace Crossgram
