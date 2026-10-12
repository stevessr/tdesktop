#include "crossgram/wss_socket.h"

#include "base/invoke_queued.h"

#include <QtCore/QtEndian>

namespace MTP::details {

WssSocket::WssSocket(not_null<QThread*> thread, const QUrl &url)
: AbstractSocket(thread)
, _url(url) {
	_transport.moveToThread(thread);
	_transport.onOpen = [=] {
		_connected.fire({});
	};
	_transport.onClose = [=] {
		_disconnected.fire({});
	};
	_transport.onFailure = [=](const char *category) {
		LOG(("CrossGram WSS failure: %1").arg(QString::fromLatin1(category)));
		_error.fire({});
	};
	_transport.onData = [=] {
		if (_notifying) return;
		_notifying = true;
		InvokeQueued(this, [=] {
			_notifying = false;
			if (_transport.readable()) _readyRead.fire({});
		});
	};
}

WssSocket::~WssSocket() {
	_transport.stop();
}

void WssSocket::connectToHost(const QString &, int) {
	_transport.open(_url);
}

bool WssSocket::isGoodStartNonce(bytes::const_span nonce) {
	if (nonce.size() < 8) return false;
	const auto a = qFromUnaligned<uint32>(nonce.data());
	const auto b = qFromUnaligned<uint32>(nonce.data() + 4);
	const auto first = gsl::to_integer<uchar>(nonce.front());
	return first != 0xef && b != 0
		&& a != 0x44414548U && a != 0x54534f50U
		&& a != 0x20544547U && a != 0xeeeeeeeeU
		&& a != 0xddddddddU && a != 0x02010316U;
}

void WssSocket::timedOut() {
	_transport.expire();
}

bool WssSocket::isConnected() {
	return _transport.connected();
}

bool WssSocket::hasBytesAvailable() {
	return _transport.readable();
}

int64 WssSocket::read(bytes::span buffer) {
	return _transport.receive(
		reinterpret_cast<char*>(buffer.data()),
		buffer.size());
}

void WssSocket::write(bytes::const_span prefix, bytes::const_span buffer) {
	_transport.send(
		reinterpret_cast<const char*>(prefix.data()), prefix.size(),
		reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

int32 WssSocket::debugState() {
	return _transport.state();
}

QString WssSocket::debugPostfix() const {
	return u"/crossgram-wss"_q;
}

} // namespace MTP::details
