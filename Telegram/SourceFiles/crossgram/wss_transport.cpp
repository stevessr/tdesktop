#include "crossgram/wss_transport.h"

#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslSocket>

#include <algorithm>
#include <cstring>

namespace Crossgram {

WssTransport::WssTransport(QObject *parent) : QObject(parent) {
	_websocket.setParent(this);
	_connectDeadline.setParent(this);
	_writeDeadline.setParent(this);
	_websocket.setProxy(QNetworkProxy::NoProxy);
	_websocket.setReadBufferSize(kQueueLimit);
	_websocket.setMaxAllowedIncomingFrameSize(kQueueLimit);
	_websocket.setMaxAllowedIncomingMessageSize(kQueueLimit);
	_websocket.setOutgoingFrameSize(kChunkLimit);

	auto tls = QSslConfiguration::defaultConfiguration();
	tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
	tls.setProtocol(QSsl::TlsV1_2OrLater);
	_websocket.setSslConfiguration(tls);

	_connectDeadline.setSingleShot(true);
	_writeDeadline.setSingleShot(true);
	connect(&_connectDeadline, &QTimer::timeout, this, [=] {
		terminate("handshake-timeout");
	});
	connect(&_writeDeadline, &QTimer::timeout, this, [=] {
		terminate("send-stalled");
	});
	connect(&_websocket, &QWebSocket::connected, this, [=] {
		if (_stopped) return;
		_connectDeadline.stop();
		if (onOpen) onOpen();
	});
	connect(&_websocket, &QWebSocket::disconnected, this, [=] {
		if (_stopped) return;
		_stopped = true;
		_connectDeadline.stop();
		_writeDeadline.stop();
		_incoming.clear();
		_outgoing.clear();
		if (onClose) onClose();
	});
	connect(&_websocket, &QWebSocket::sslErrors, this, [=] {
		terminate("tls-verification");
	});
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	connect(&_websocket, &QWebSocket::errorOccurred, this, [=](QAbstractSocket::SocketError code) {
#else
	connect(&_websocket, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), this, [=](QAbstractSocket::SocketError code) {
#endif
		terminate(code == QAbstractSocket::HostNotFoundError ? "dns"
			: code == QAbstractSocket::SslHandshakeFailedError ? "tls-handshake"
			: code == QAbstractSocket::ConnectionRefusedError ? "connection-refused"
			: "network-or-upgrade");
	});
	connect(&_websocket, &QWebSocket::textFrameReceived, this, [=] {
		terminate("unexpected-text-frame");
	});
	connect(&_websocket, &QWebSocket::binaryFrameReceived, this, [=](
			const QByteArray &payload, bool) {
		if (_stopped || payload.isEmpty()) return;
		const auto outstanding = _incoming.size() - _consumed;
		if (payload.size() > kQueueLimit - outstanding) {
			terminate("receive-limit");
			return;
		}
		if (_consumed) {
			_incoming.remove(0, _consumed);
			_consumed = 0;
		}
		_incoming.append(payload);
		if (onData) onData();
	});
	connect(&_websocket, &QWebSocket::bytesWritten, this, [=] {
		if (_stopped) return;
		flush();
		if (_websocket.bytesToWrite() || !_outgoing.isEmpty()) {
			_writeDeadline.start(30000);
		} else {
			_writeDeadline.stop();
		}
	});
}

WssTransport::~WssTransport() {
	stop();
}

void WssTransport::open(const QUrl &url) {
	if (_started || _stopped || !url.isValid() || url.scheme() != u"wss") {
		terminate("invalid-endpoint");
		return;
	}
	_started = true;
	_connectDeadline.start(15000);
	_websocket.open(url);
}

void WssTransport::stop() {
	if (_stopped) return;
	_stopped = true;
	_connectDeadline.stop();
	_writeDeadline.stop();
	_websocket.abort();
	_incoming.clear();
	_outgoing.clear();
	_consumed = 0;
}

void WssTransport::expire() {
	terminate("mtproto-timeout");
}

bool WssTransport::connected() const {
	return !_stopped && _websocket.state() == QAbstractSocket::ConnectedState;
}

bool WssTransport::readable() const {
	return _incoming.size() > _consumed;
}

int WssTransport::state() const {
	return _websocket.state();
}

qint64 WssTransport::receive(char *destination, qint64 capacity) {
	if (capacity <= 0 || !readable()) return 0;
	const auto count = std::min<qint64>(capacity, _incoming.size() - _consumed);
	std::memcpy(destination, _incoming.constData() + _consumed, count);
	_consumed += count;
	if (_consumed == _incoming.size()) {
		_incoming.clear();
		_consumed = 0;
	}
	return count;
}

void WssTransport::send(
		const char *header,
		qsizetype headerSize,
		const char *body,
		qsizetype bodySize) {
	if (!connected()) {
		terminate("not-connected");
		return;
	}
	const auto room = kQueueLimit - _outgoing.size() - _websocket.bytesToWrite();
	if (headerSize < 0 || bodySize < 0
		|| headerSize > room || bodySize > room - headerSize) {
		terminate("send-limit");
		return;
	}
	if (headerSize) _outgoing.append(header, headerSize);
	if (bodySize) _outgoing.append(body, bodySize);
	flush();
	if (_stopped) return;
	if (!_outgoing.isEmpty() || _websocket.bytesToWrite()) {
		_writeDeadline.start(30000);
	}
}

void WssTransport::flush() {
	if (!connected()) return;
	while (!_outgoing.isEmpty() && _websocket.bytesToWrite() < kChunkLimit) {
		const auto amount = std::min<qsizetype>(_outgoing.size(), kChunkLimit);
		const auto chunk = _outgoing.left(amount);
		_outgoing.remove(0, amount);
		if (_websocket.sendBinaryMessage(chunk) != amount) {
			terminate("send-failed");
			return;
		}
	}
}

void WssTransport::terminate(const char *category) {
	if (_stopped) return;
	stop();
	if (onFailure) onFailure(category);
}

} // namespace Crossgram
